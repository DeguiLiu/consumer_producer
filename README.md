# wp::WorkerPool

[![CI](https://github.com/DeguiLiu/consumer_producer/actions/workflows/ci.yml/badge.svg)](https://github.com/DeguiLiu/consumer_producer/actions)

Multi-worker thread pool built on [mccc](https://github.com/DeguiLiu/mccc) lock-free message bus. C++17.

Header-only. Lock-free ingress (mccc MPSC ring buffer) + lock-free per-worker SPSC queues.
Function pointer handlers. `-fno-exceptions -fno-rtti` compatible.

## Architecture

```
Submit() --> mccc::AsyncBus (lock-free MPSC ring buffer)
                  |
            DispatcherThread (ProcessBatch loop)
                  | round-robin
            Worker[0..N-1] SPSC Queue --> WorkerThread --> Handler
```

## Quick Start

```cpp
#include <wp/worker_pool.hpp>

struct TaskA { int id; };
struct TaskB { float value; };
using MyPayload = std::variant<TaskA, TaskB>;

static void HandleA(const TaskA& t, const mccc::MessageHeader&) { /* ... */ }

wp::Config cfg;
cfg.name = "demo";
cfg.worker_num = 4;

wp::WorkerPool<MyPayload> pool(cfg);
pool.RegisterHandler<TaskA>(HandleA);
pool.Start();
pool.Submit(TaskA{1});
pool.Shutdown();
```

## API Reference

### Config

| Field | Default | Description |
|-------|---------|-------------|
| `name` | "pool" | Instance name (FixedString<32>) |
| `worker_num` | 1 | Number of worker threads |
| `worker_queue_depth` | 1024 | Per-worker SPSC queue depth (rounded to power of 2) |
| `priority` | 0 | Thread priority (>0: SCHED_FIFO, <0: SCHED_IDLE, 0: default) |
| `cpu_set_size` | 0 | CPU affinity set size (Linux only) |
| `cpu_set` | nullptr | CPU affinity set (Linux only) |

### Methods

| Method | Description |
|--------|-------------|
| `RegisterHandler<T>(handler)` | Register function pointer handler for type T |
| `Start()` | Start dispatcher + worker threads |
| `Shutdown()` | Drain all queues, join all threads |
| `Submit(payload, priority)` | Submit job via mccc bus (lock-free, returns bool) |
| `SubmitSync(payload)` | Execute handler synchronously in caller thread |
| `FlushAndPause()` | Drain all pending work, pause accepting new jobs |
| `Resume()` | Resume after pause |
| `GetStats()` | Get dispatched/processed/dropped statistics |
| `WorkerCount()` | Number of worker threads |
| `IsRunning()` / `IsPaused()` | Lifecycle state queries |

### mccc Priority Levels

| Priority | Admission Threshold |
|----------|-------------------|
| `LOW` | Dropped when bus >= 60% full |
| `MEDIUM` | Dropped when bus >= 80% full |
| `HIGH` | Dropped when bus >= 99% full |

## Dependency

WorkerPool depends on [mccc](https://github.com/DeguiLiu/mccc) (fetched automatically via CMake FetchContent).

## Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
ctest --output-on-failure
```

## Performance

Benchmark on Ubuntu 24.04, x86_64, GCC 13, Release build:

| Test | Result |
|------|--------|
| SPSC throughput (100K jobs) | 3602 K jobs/sec |
| 1P x 1C throughput | 3910 K jobs/sec |
| 2P x 2C throughput | 3034 K jobs/sec |
| 4P x 4C throughput | 574 K jobs/sec |
| Enqueue-to-process P50 | 79 us |
| Enqueue-to-process P99 | 361 us |

vs. ConsumerProducer v2.0 (SPSC 1068 K jobs/sec): **3.4x throughput improvement**.

See [docs/benchmark_report.md](docs/benchmark_report.md) for details.

## License

MIT
