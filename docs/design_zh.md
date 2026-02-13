# cp::ConsumerProducer 设计文档

## 1. 概述

`cp::ConsumerProducer<Worker>` 是一个 header-only 的 C++14 多线程生产者-消费者组件，
支持双优先级队列、可配置的丢弃策略和线程池。

目标环境: ARM-Linux 嵌入式系统，资源敏感场景。

## 2. 架构

```
Producer Threads            ConsumerProducer              Worker Threads
                         +----------------------+
  AddJob(hi) ---------> |  hi_queue_ (FIFO)    | ------+
                         |                      |       |
  AddJob(lo) ---------> |  lo_queue_ (FIFO)    | --+   |
                         |                      |   |   |
                         |  mutex_ + CVs        |   +---+--> ConsumerLoop()
                         |  atomic stats        |             -> consume_func_()
                         +----------------------+
```

### 2.1 双优先级队列

- `hi_queue_`: 高优先级队列，容量由 `hi_queue_size` 配置
- `lo_queue_`: 低优先级队列，容量由 `queue_size` 配置
- GetJob() 优先从 hi_queue_ 取任务，再从 lo_queue_ 取

### 2.2 丢弃策略

当低优先级队列满时:
- `not_discardable=true` 或高优先级: 阻塞等待空位
- `not_discardable=false`: 尝试丢弃队头（若队头可丢弃），否则丢弃自身

### 2.3 preferred 判定

- 高优先级任务: 始终 preferred
- 低优先级任务: 出队时队列长度 <= `prefer_queue_size` 则 preferred

## 3. 线程安全设计

### 3.1 同步原语

| 原语 | 保护对象 |
|------|----------|
| `mutex_` | 队列操作、CV 谓词相关计数器 |
| `not_empty_cv_` | 消费者等待队列非空 |
| `not_full_cv_` | 生产者等待队列非满 / 暂停恢复 |
| `job_done_cv_` | AddJobWaitDone / FlushAndPause |
| `shutdown_` (atomic) | 关闭标志 |
| `paused_` (atomic) | 暂停标志 |

### 3.2 原子统计优化 (v2.0)

| 字段 | 类型 | 保护方式 | 说明 |
|------|------|----------|------|
| `total_wait_time_` | `atomic<int64_t>` | lock-free | ConsumerLoop 外部累加 |
| `total_process_time_` | `atomic<int64_t>` | lock-free | ConsumerLoop 外部累加 |
| `total_drop_time_` | `atomic<int64_t>` | lock-free | ConsumerLoop 外部累加 |
| `blocked_job_` | `atomic<uint64_t>` | lock-free | EnqueueImpl 无锁递增 |
| `max_queue_length_` | `atomic<uint32_t>` | lock-free | 读无锁，写在 mutex 内 |
| `pid_` | `atomic<int32_t>` | lock-free | 多 worker 写无竞争 |
| `finished_job_` | `uint64_t` | mutex | FlushAndPause CV 谓词 |
| `dropped_job_` | `uint64_t` | mutex | FlushAndPause CV 谓词 |
| `finished_job_id_` | `uint64_t` | mutex | AddJobWaitDone CV 谓词 |
| `added_job_` | `uint64_t` | mutex | FlushAndPause CV 谓词 |

`finished_job_`、`dropped_job_`、`finished_job_id_`、`added_job_` 必须在 mutex 保护下更新，
原因是条件变量的等待-通知模式要求状态变更与通知在同一锁下完成，否则会出现通知丢失。

### 3.3 notify_one 优化

| 条件变量 | 使用场景 | 通知方式 |
|----------|----------|----------|
| `not_empty_cv_` | EnqueueImpl 入队后 | `notify_one` |
| `not_full_cv_` | GetJob 出队后 | `notify_one` |
| `not_full_cv_` | Shutdown/Resume | `notify_all` |
| `job_done_cv_` | ConsumerLoop 正常 | `notify_one` |
| `job_done_cv_` | ConsumerLoop paused | `notify_all` |
| `job_done_cv_` | EnqueueImpl discard | `notify_all` |

## 4. 平台兼容性

Linux 专有 API 通过 `#ifdef __linux__` 隔离:
- `cpu_set_t` / `pthread_setaffinity_np`: CPU 亲和性
- `SCHED_IDLE`: 低优先级调度策略
- `SYS_gettid` / `syscall`: 获取线程 TID
- `CLOCK_MONOTONIC_RAW` / `clock_gettime`: 高精度时钟

非 Linux 平台使用 `std::chrono::steady_clock` 作为时钟源。

## 5. 资源预算

| 资源 | 用量 |
|------|------|
| 堆内存 | `sizeof(Job) * (queue_size + hi_queue_size)` + `sizeof(thread) * worker_num` |
| 线程 | `worker_num` 个消费者线程 |
| 同步原语 | 1 mutex + 3 condition_variable |
| 原子变量 | 2 bool + 5 数组(kMax=2) + 1 uint32 + 1 int32 |
