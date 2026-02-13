# wp::WorkerPool 设计文档

## 1. 概述

`wp::WorkerPool<PayloadVariant>` 是基于 mccc 消息总线的多 worker 线程池组件。
用于替代 `cp::ConsumerProducer`，提供更高吞吐量和更好的 MISRA C++ 合规性。

目标环境: ARM-Linux 嵌入式系统，资源敏感场景。

## 2. 架构

```
Producer Threads           WorkerPool                     Worker Threads

Submit(TaskA{}) --+
Submit(TaskB{}) --+--> mccc::AsyncBus  --> DispatcherThread --> Worker[0] SPSC --> WorkerLoop --> Handler
Submit(TaskC{}) --+    (lock-free MPSC)    (ProcessBatch)      Worker[1] SPSC --> WorkerLoop --> Handler
                                           (round-robin)       Worker[N] SPSC --> WorkerLoop --> Handler
```

### 2.1 三阶段流水线

1. **Ingress (lock-free MPSC)**: 生产者通过 `Submit()` 调用 `mccc::AsyncBus::PublishWithPriority()`，
   CAS 操作写入环形缓冲区，零锁争用。

2. **Dispatch (single-consumer)**: 专用 dispatcher 线程调用 `ProcessBatch()` 从环形缓冲区
   批量消费消息，round-robin 分发到 per-worker SPSC 队列。

3. **Process (lock-free SPSC)**: 每个 worker 线程从专属 SPSC 队列消费消息，
   通过类型擦除的 dispatch table 调用对应的 handler 函数指针。

### 2.2 优先级准入控制

由 mccc 总线实现，基于队列深度百分比:
- LOW: 队列 >= 60% 满时丢弃
- MEDIUM: 队列 >= 80% 满时丢弃
- HIGH: 队列 >= 99% 满时丢弃

### 2.3 类型擦除 Handler 调度

```cpp
// 编译时为每个 variant 类型生成 trampoline 函数
template <typename T>
static void TypedDispatch(const EnvelopeType& env, void* handler_ptr) noexcept {
    using FuncType = void(*)(const T&, const mccc::MessageHeader&);
    FuncType fn;
    std::memcpy(&fn, &handler_ptr, sizeof(fn));  // 标准合规的类型双关
    const T* data = std::get_if<T>(&env.payload);
    if (data != nullptr) {
        fn(*data, env.header);
    }
}
```

## 3. 线程安全设计

### 3.1 同步原语

| 原语 | 保护对象 |
|------|----------|
| mccc MPSC ring buffer | Ingress: 多生产者无锁写入 |
| SPSC ring buffer (per-worker) | Dispatch -> Worker: 无锁传递 |
| `std::mutex` + `std::condition_variable` (per-worker) | Worker 休眠/唤醒 |
| `shutdown_` (atomic) | 关闭标志 |
| `paused_` (atomic) | 暂停标志 |
| `dispatched_` / `processed_` (atomic) | 统计计数器 |

### 3.2 内存序

| 操作 | 内存序 | 说明 |
|------|--------|------|
| SPSC write_pos_ store | release | 确保数据写入在位置更新前可见 |
| SPSC read_pos_ load | acquire | 确保位置读取在数据读取前完成 |
| dispatched_ / processed_ | release/acquire | FlushAndPause 完整性保证 |
| next_worker_ | relaxed | 仅用于 round-robin，不需要强序 |

## 4. SpscQueue 设计

基于 Lamport 单生产者单消费者队列:

- 容量向上取整为 2 的幂，使用位掩码索引
- write_pos_ 和 read_pos_ 分别在独立 cache line 上，避免伪共享
- 使用 `std::vector<T>` 作为后备存储（构造时一次性分配）
- 无锁操作: `TryPush()` O(1), `TryPop()` O(1)

## 5. 与 ConsumerProducer 对比

| 特性 | ConsumerProducer | WorkerPool |
|------|-----------------|------------|
| Ingress 机制 | mutex + condition_variable | lock-free MPSC (CAS) |
| Worker 队列 | 共享单队列 + mutex | per-worker SPSC (lock-free) |
| 任务类型 | `std::shared_ptr<Worker>` | `std::variant<Types...>` (值类型) |
| 回调类型 | `std::function` | 函数指针 (零开销) |
| 命名 | `std::string` | `mccc::FixedString<32>` |
| C++ 标准 | C++14 | C++17 |
| 热路径堆分配 | shared_ptr (每任务) | 零 (envelope 嵌入 ring buffer) |
| SPSC 吞吐 | 1068 K/s | 3602 K/s |
| 优先级 | 双队列 (high/low) | 三级准入控制 (mccc) |
| 丢弃策略 | 队头丢弃/自身丢弃 | 准入控制丢弃 (bus 层) |

## 6. 资源预算

| 资源 | 用量 |
|------|------|
| 堆内存 | `sizeof(EnvelopeType) * (bus_depth + worker_queue_depth * worker_num)` |
| 线程 | 1 dispatcher + `worker_num` worker |
| 同步原语 | worker_num * (1 mutex + 1 CV) |
| 原子变量 | 4 uint64 + 1 uint32 + 3 bool + SPSC 计数器 * worker_num |
