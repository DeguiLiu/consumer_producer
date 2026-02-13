# cp::ConsumerProducer

[![CI](https://github.com/DeguiLiu/consumer_producer/actions/workflows/ci.yml/badge.svg)](https://github.com/DeguiLiu/consumer_producer/actions)

Multi-threaded consumer-producer with dual-priority queues for C++14.

Header-only. Single file. Supports priority scheduling, job discard policies, and per-queue statistics.

## Features

- Dual-priority queues (high/low) with high-priority-first consumption
- Configurable worker thread pool
- Job discard policy: discard oldest, discard self, or block
- Per-queue statistics: wait time, process time, drop time
- Atomic statistics for reduced lock contention in hot path
- Thread priority (SCHED_FIFO/SCHED_IDLE) and CPU affinity support (Linux)
- Cross-platform: Linux (GCC) + macOS (Clang)
- `-fno-exceptions -fno-rtti` compatible
- Flush-and-pause / resume for graceful draining

## Quick Start

```cpp
#include <cp/consumer_producer.hpp>

struct MyTask { int id; };

cp::Config cfg;
cfg.name = "demo";
cfg.worker_num = 2;
cfg.queue_size = 32;
cfg.hi_queue_size = 8;

cp::ConsumerProducer<MyTask> cp(cfg,
    [](std::shared_ptr<MyTask> job, bool preferred) -> int32_t {
        // process job
        return 0;
    });

cp.Start();
cp.AddJob(std::make_shared<MyTask>(MyTask{1}));
cp.Shutdown();
```

## API Reference

### Config

| Field | Default | Description |
|-------|---------|-------------|
| `name` | - | Instance name (for logging/stats) |
| `priority` | 0 | Thread priority (>0: SCHED_FIFO, <0: SCHED_IDLE, 0: default) |
| `worker_num` | 1 | Number of consumer threads |
| `queue_size` | 16 | Low-priority queue capacity |
| `prefer_queue_size` | 0 | Jobs preferred when queue length <= this (0 = blocking mode) |
| `hi_queue_size` | 4 | High-priority queue capacity |
| `cpu_set_size` | 0 | CPU affinity set size (Linux only) |
| `cpu_set` | nullptr | CPU affinity set (Linux only) |

### Methods

| Method | Description |
|--------|-------------|
| `Start()` | Start worker threads |
| `Shutdown()` | Stop all workers, wait for completion |
| `AddJob(job, high_priority, not_discardable)` | Enqueue a job |
| `AddJobWaitDone(job, high_priority)` | Enqueue and block until processed |
| `FlushAndPause()` | Drain all pending jobs, pause accepting new ones |
| `Resume()` | Resume after pause |
| `QueueLength()` | Current low-priority queue length |
| `MaxQueueLength()` | Peak queue length observed (lock-free) |
| `DroppedJobCount()` | Total dropped jobs |
| `BlockedJobCount()` | Total blocked enqueues (lock-free) |
| `GetStatsString()` | Formatted stats string |
| `PrintStats()` | Print stats to stdout |

### EnqueueResult

| Value | Description |
|-------|-------------|
| `kNormal` | Normal enqueue |
| `kBlocked` | Blocked then enqueued |
| `kDiscardSelf` | Queue full, self discarded |
| `kDiscardHead` | Queue full, oldest discarded |

## Discard Policy

When the low-priority queue is full:
- `not_discardable=true`: block until space available
- `not_discardable=false`: try to discard the oldest job (if it's discardable), otherwise discard self
- High-priority jobs always block when queue is full

## Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
ctest --output-on-failure
```

## Performance (v2.0)

Benchmark on Ubuntu 24.04, x86_64, GCC 13, Release build:

| Test | Result |
|------|--------|
| SPSC throughput (100K jobs) | 1068 K jobs/sec |
| 4P x 4C throughput | 314 K jobs/sec |
| Enqueue-to-consume P50 | 24 us |
| Enqueue-to-consume P99 | 59 us |

See [docs/benchmark_report.md](docs/benchmark_report.md) for details.

## License

MIT
