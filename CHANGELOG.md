# Changelog

## v2.0.0

### Performance

- Time statistics (wait/process/drop) changed to `std::atomic` with `memory_order_relaxed`, moved out of mutex critical section in ConsumerLoop
- `blocked_job_` and `max_queue_length_` changed to `std::atomic` for lock-free reads
- `job_done_cv_` uses `notify_one` when not paused, `notify_all` only during FlushAndPause (reduces thundering herd)
- `blocked_job_` increment moved out of mutex in EnqueueImpl
- `BlockedJobCount()` and `MaxQueueLength()` are now lock-free reads
- `pid_` changed to `std::atomic<int32_t>` to fix data race with multiple worker threads

### Portability

- Added `#ifdef __linux__` guards for Linux-specific APIs (cpu_set_t, pthread_setaffinity_np, SCHED_IDLE, SYS_gettid, CLOCK_MONOTONIC_RAW)
- Non-Linux platforms use `std::chrono::steady_clock` for NowNs()
- Builds on macOS (Clang) in addition to Linux (GCC)

### Testing

- Upgraded Catch2 v2.13.10 to v3.5.2 (uses Catch2WithMain, catch_discover_tests)
- Deleted test_main.cpp (no longer needed with Catch2 v3)
- Added test_stats.cpp (6 test cases): atomic blocked count, MaxQueueLength, DroppedJobCount, GetStatsString, multi-thread stats consistency, FlushAndPause stats
- Added test_edge_cases.cpp (5 test cases): single worker, multiple AddJobWaitDone, immediate shutdown, no-start destruction, high-priority AddJobWaitDone
- Fixed flaky priority test to be timing-insensitive
- Total: 21 test cases (was 10)

### Build

- Added benchmark example (SPSC throughput, multi-producer, latency distribution, priority scheduling)
- Added .clang-format (Google C++14 style, 120 col)
- Added CPPLINT.cfg
- CI: added sanitizers (ASan, TSan, UBSan), code-quality jobs, -fno-exceptions -fno-rtti verification
- Deleted old root-level files: consumer_producer.hpp, consumer_producer_test.cpp, fake_log.h

## v1.0.0

- Initial release: header-only C++14 consumer-producer with dual-priority queues
- Features: priority scheduling, job discard policies, per-queue statistics, CPU affinity
