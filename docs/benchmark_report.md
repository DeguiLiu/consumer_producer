# ConsumerProducer Benchmark Report

Platform: Ubuntu 24.04, x86_64, GCC 13
Build: Release (-O2)

## 1. Single-producer single-consumer throughput

100K jobs, 1 producer thread, 1 consumer thread.

| Metric | Value |
|--------|-------|
| Throughput | 1068 K jobs/sec |

## 2. Multi-producer throughput

25K jobs per producer thread.

| Producers | Consumers | Throughput |
|-----------|-----------|------------|
| 1 | 1 | 1182 K jobs/sec |
| 2 | 1 | 429 K jobs/sec |
| 4 | 1 | 136 K jobs/sec |
| 4 | 2 | 283 K jobs/sec |
| 4 | 4 | 314 K jobs/sec |

Throughput drops with more producers due to mutex contention. Adding more consumers partially recovers throughput.

## 3. Enqueue-to-consume latency

50K samples, 1 producer, 1 consumer.

| Percentile | Latency |
|------------|---------|
| P50 | 24 us |
| P95 | 44 us |
| P99 | 59 us |
| Max | 229 us |

Latency dominated by shared_ptr allocation, mutex contention, and condition variable signaling.

## 4. Priority scheduling

1000 low-priority + 100 high-priority mixed jobs.

| Metric | Value |
|--------|-------|
| High-priority processed | 100 |
| High-priority preferred | 100 |
| Low-priority processed | 1000 |

All high-priority jobs were processed with preferred=true, confirming priority scheduling correctness.

## 5. Optimization impact

v2.0 atomic stats optimization:
- ConsumerLoop critical section reduced from 5+ operations to 2-3 operations
- `BlockedJobCount()` and `MaxQueueLength()` reads are now lock-free
- `blocked_job_` increment in EnqueueImpl is now lock-free (eliminated separate lock acquire)
- Time accumulation (`total_wait_time_`, `total_process_time_`, `total_drop_time_`) moved outside mutex
