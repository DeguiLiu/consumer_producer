# WorkerPool Benchmark Report

Platform: Ubuntu 24.04, x86_64, GCC 13
Build: Release (-O2)

## 1. Single-producer single-consumer throughput

100K jobs, 1 producer thread, 1 worker thread.

| Metric | Value |
|--------|-------|
| Throughput | 3602 K jobs/sec |

## 2. Multi-producer throughput

25K jobs per producer thread.

| Producers | Workers | Throughput |
|-----------|---------|------------|
| 1 | 1 | 3910 K jobs/sec |
| 2 | 2 | 3034 K jobs/sec |
| 4 | 4 | 574 K jobs/sec |

4P x 4C throughput drops due to MPSC CAS contention and round-robin dispatch overhead.

## 3. Enqueue-to-process latency

50K samples, 1 producer, 1 worker.

| Percentile | Latency |
|------------|---------|
| P50 | 79 us |
| P95 | 145 us |
| P99 | 361 us |
| Max | 545 us |

Latency includes: mccc bus publish + ring buffer CAS + ProcessBatch dispatch +
SPSC queue push/pop + worker CV wakeup + handler invocation.

## 4. Comparison with ConsumerProducer v2.0

| Metric | CP v2.0 | WorkerPool | Improvement |
|--------|---------|------------|-------------|
| SPSC throughput | 1068 K/s | 3602 K/s | 3.4x |
| P50 latency | 24 us | 79 us | -3.3x |
| P99 latency | 59 us | 361 us | -6.1x |

WorkerPool achieves significantly higher throughput due to lock-free MPSC ingress.
Latency is higher due to the two-stage queue design (bus -> SPSC) and the 50us
dispatcher sleep interval.

## 5. Architecture impact

- Lock-free MPSC bus eliminates mutex contention on the publish path
- Per-worker SPSC queues eliminate contention between workers
- Round-robin dispatch balances load across workers
- Dispatcher sleep interval (50us) dominates latency at low load
