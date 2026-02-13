#include <cstdio>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cp/consumer_producer.hpp>
#include <memory>
#include <thread>
#include <vector>

struct BenchJob {
  int64_t enqueue_time{0};
  int id{0};
};

// Prevent compiler from optimizing away
static void DoNotOptimize(void* ptr) {
  asm volatile("" : : "r,m"(ptr) : "memory");
}

// ---------------------------------------------------------------------------
// Test 1: Single-producer single-consumer throughput
// ---------------------------------------------------------------------------
static void BenchSPSC() {
  constexpr int kJobCount = 100000;
  std::atomic<int> consumed{0};

  cp::Config cfg;
  cfg.name = "spsc";
  cfg.worker_num = 1;
  cfg.queue_size = 1024;
  cfg.hi_queue_size = 16;

  cp::ConsumerProducer<BenchJob> cp(cfg, [&](std::shared_ptr<BenchJob> job, bool) -> int32_t {
    DoNotOptimize(job.get());
    consumed.fetch_add(1, std::memory_order_relaxed);
    return 0;
  });

  cp.Start();

  auto start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < kJobCount; ++i) {
    cp.AddJob(std::make_shared<BenchJob>(), false, true);
  }
  cp.Shutdown();
  auto end = std::chrono::high_resolution_clock::now();

  auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
  double jobs_per_sec = static_cast<double>(kJobCount) / (static_cast<double>(ns) / 1e9);
  std::printf("  1P x 1C: %8.2f K jobs/sec\n", jobs_per_sec / 1e3);
}

// ---------------------------------------------------------------------------
// Test 2: Multi-producer throughput
// ---------------------------------------------------------------------------
static void BenchMultiProducer(uint32_t num_producers, uint32_t num_workers) {
  constexpr int kJobsPerProducer = 25000;
  std::atomic<int> consumed{0};

  cp::Config cfg;
  cfg.name = "mp";
  cfg.worker_num = num_workers;
  cfg.queue_size = 1024;
  cfg.hi_queue_size = 64;

  cp::ConsumerProducer<BenchJob> cp(cfg, [&](std::shared_ptr<BenchJob> job, bool) -> int32_t {
    DoNotOptimize(job.get());
    consumed.fetch_add(1, std::memory_order_relaxed);
    return 0;
  });

  cp.Start();

  auto start = std::chrono::high_resolution_clock::now();

  std::vector<std::thread> producers;
  producers.reserve(num_producers);
  for (uint32_t p = 0; p < num_producers; ++p) {
    producers.emplace_back([&]() {
      for (int i = 0; i < kJobsPerProducer; ++i) {
        cp.AddJob(std::make_shared<BenchJob>(), false, true);
      }
    });
  }
  for (auto& t : producers)
    t.join();

  cp.Shutdown();
  auto end = std::chrono::high_resolution_clock::now();

  int total_jobs = static_cast<int>(num_producers) * kJobsPerProducer;
  auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
  double jobs_per_sec = static_cast<double>(total_jobs) / (static_cast<double>(ns) / 1e9);
  std::printf("  %uP x %uC: %8.2f K jobs/sec\n", num_producers, num_workers, jobs_per_sec / 1e3);
}

// ---------------------------------------------------------------------------
// Test 3: Enqueue-to-consume latency distribution
// ---------------------------------------------------------------------------
static void BenchLatencyDistribution() {
  constexpr int kSamples = 50000;
  std::vector<int64_t> latencies(kSamples);
  std::atomic<int> idx{0};

  cp::Config cfg;
  cfg.name = "latency";
  cfg.worker_num = 1;
  cfg.queue_size = 256;
  cfg.hi_queue_size = 16;

  cp::ConsumerProducer<BenchJob> cp(cfg, [&](std::shared_ptr<BenchJob> job, bool) -> int32_t {
    auto now = std::chrono::high_resolution_clock::now();
    int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    int cur = idx.fetch_add(1, std::memory_order_relaxed);
    if (cur < kSamples) {
      latencies[cur] = now_ns - job->enqueue_time;
    }
    return 0;
  });

  cp.Start();

  for (int i = 0; i < kSamples; ++i) {
    auto job = std::make_shared<BenchJob>();
    auto now = std::chrono::high_resolution_clock::now();
    job->enqueue_time = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    job->id = i;
    cp.AddJob(job, false, true);
    // Small delay to avoid overwhelming the queue
    if (i % 100 == 0) {
      std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
  }

  cp.Shutdown();

  int n = idx.load();
  if (n > kSamples)
    n = kSamples;
  std::sort(latencies.begin(), latencies.begin() + n);

  auto percentile = [&](double pct) -> int64_t {
    int p = static_cast<int>(pct / 100.0 * (n - 1));
    return latencies[p];
  };

  std::printf("  P50:  %8lld ns\n", static_cast<long long>(percentile(50)));
  std::printf("  P95:  %8lld ns\n", static_cast<long long>(percentile(95)));
  std::printf("  P99:  %8lld ns\n", static_cast<long long>(percentile(99)));
  std::printf("  Max:  %8lld ns\n", static_cast<long long>(latencies[n - 1]));
}

// ---------------------------------------------------------------------------
// Test 4: Priority scheduling verification
// ---------------------------------------------------------------------------
static void BenchPriorityScheduling() {
  constexpr int kLowJobs = 1000;
  constexpr int kHighJobs = 100;
  std::atomic<int> hi_count{0};
  std::atomic<int> lo_count{0};
  std::atomic<int> hi_preferred{0};

  cp::Config cfg;
  cfg.name = "prio_bench";
  cfg.worker_num = 1;
  cfg.queue_size = 256;
  cfg.hi_queue_size = 64;

  cp::ConsumerProducer<BenchJob> cp(cfg, [&](std::shared_ptr<BenchJob> job, bool preferred) -> int32_t {
    if (job->id >= 10000) {
      hi_count.fetch_add(1, std::memory_order_relaxed);
      if (preferred)
        hi_preferred.fetch_add(1, std::memory_order_relaxed);
    } else {
      lo_count.fetch_add(1, std::memory_order_relaxed);
    }
    return 0;
  });

  cp.Start();

  // Mix low and high priority
  for (int i = 0; i < kLowJobs; ++i) {
    auto job = std::make_shared<BenchJob>();
    job->id = i;
    cp.AddJob(job, false, true);
    if (i % 10 == 0 && i / 10 < kHighJobs) {
      auto hi_job = std::make_shared<BenchJob>();
      hi_job->id = 10000 + i / 10;
      cp.AddJob(hi_job, true);
    }
  }

  cp.Shutdown();

  std::printf("  High-prio processed: %d (preferred: %d)\n", hi_count.load(), hi_preferred.load());
  std::printf("  Low-prio  processed: %d\n", lo_count.load());
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
  std::printf("========================================\n");
  std::printf("  ConsumerProducer Benchmark Report\n");
  std::printf("========================================\n\n");

  std::printf("[1] Single-producer single-consumer (100K jobs)\n");
  BenchSPSC();
  std::printf("\n");

  std::printf("[2] Multi-producer throughput (25K jobs/producer)\n");
  BenchMultiProducer(1, 1);
  BenchMultiProducer(2, 1);
  BenchMultiProducer(4, 1);
  BenchMultiProducer(4, 2);
  BenchMultiProducer(4, 4);
  std::printf("\n");

  std::printf("[3] Enqueue-to-consume latency (50K samples)\n");
  BenchLatencyDistribution();
  std::printf("\n");

  std::printf("[4] Priority scheduling verification\n");
  BenchPriorityScheduling();
  std::printf("\n");

  std::printf("========================================\n");
  return 0;
}
