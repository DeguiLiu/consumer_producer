// Copyright (c) 2024 liudegui. MIT License.
// Performance benchmark for WorkerPool.

#include <cstdint>
#include <cstdio>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <variant>
#include <vector>
#include <wp/worker_pool.hpp>

struct BenchJob {
  uint64_t seq;
  uint64_t timestamp_ns;
};

using BenchPayload = std::variant<BenchJob>;
using BenchBus = mccc::AsyncBus<BenchPayload>;

static uint64_t NowNs() {
  auto now = std::chrono::steady_clock::now();
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count());
}

// ==========================================================================
// Test 1: SPSC throughput (1 producer, 1 worker)
// ==========================================================================

static void BenchSpsc() {
  static std::atomic<uint64_t> g_count{0U};
  g_count.store(0U);

  BenchBus::Instance().ResetStatistics();

  wp::Config cfg;
  cfg.name = "spsc";
  cfg.worker_num = 1;
  cfg.worker_queue_depth = 4096;

  wp::WorkerPool<BenchPayload> pool(cfg);
  pool.RegisterHandler<BenchJob>(
      [](const BenchJob&, const mccc::MessageHeader&) { g_count.fetch_add(1U, std::memory_order_relaxed); });

  pool.Start();

  constexpr uint64_t kJobs = 100000U;
  auto start = std::chrono::steady_clock::now();

  for (uint64_t i = 0U; i < kJobs; ++i) {
    while (!pool.Submit(BenchJob{i, 0U})) {
      std::this_thread::yield();
    }
  }

  // Wait for all
  while (g_count.load(std::memory_order_acquire) < kJobs) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  auto elapsed = std::chrono::steady_clock::now() - start;
  double sec = std::chrono::duration<double>(elapsed).count();
  double kps = static_cast<double>(kJobs) / sec / 1000.0;

  std::printf("SPSC throughput (100K jobs): %.0f K jobs/sec\n", kps);

  pool.Shutdown();
}

// ==========================================================================
// Test 2: Multi-producer throughput
// ==========================================================================

static void BenchMultiProducer(uint32_t producers, uint32_t workers) {
  static std::atomic<uint64_t> g_count{0U};
  g_count.store(0U);

  BenchBus::Instance().ResetStatistics();

  wp::Config cfg;
  cfg.name = "mp";
  cfg.worker_num = workers;
  cfg.worker_queue_depth = 4096;

  wp::WorkerPool<BenchPayload> pool(cfg);
  pool.RegisterHandler<BenchJob>(
      [](const BenchJob&, const mccc::MessageHeader&) { g_count.fetch_add(1U, std::memory_order_relaxed); });

  pool.Start();

  constexpr uint64_t kJobsPerProducer = 25000U;
  const uint64_t total_jobs = producers * kJobsPerProducer;

  auto start = std::chrono::steady_clock::now();

  std::vector<std::thread> threads;
  threads.reserve(producers);
  for (uint32_t p = 0U; p < producers; ++p) {
    threads.emplace_back([&pool, p]() {
      for (uint64_t i = 0U; i < kJobsPerProducer; ++i) {
        while (!pool.Submit(BenchJob{p * kJobsPerProducer + i, 0U})) {
          std::this_thread::yield();
        }
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  while (g_count.load(std::memory_order_acquire) < total_jobs) {
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  auto elapsed = std::chrono::steady_clock::now() - start;
  double sec = std::chrono::duration<double>(elapsed).count();
  double kps = static_cast<double>(total_jobs) / sec / 1000.0;

  std::printf("%uP x %uC throughput: %.0f K jobs/sec\n", producers, workers, kps);

  pool.Shutdown();
}

// ==========================================================================
// Test 3: End-to-end latency
// ==========================================================================

static void BenchLatency() {
  static std::vector<uint64_t> g_latencies;
  static std::mutex g_mtx;
  g_latencies.clear();
  g_latencies.reserve(50000);

  BenchBus::Instance().ResetStatistics();

  wp::Config cfg;
  cfg.name = "lat";
  cfg.worker_num = 1;
  cfg.worker_queue_depth = 4096;

  wp::WorkerPool<BenchPayload> pool(cfg);
  pool.RegisterHandler<BenchJob>([](const BenchJob& job, const mccc::MessageHeader&) {
    uint64_t now = NowNs();
    uint64_t lat = now - job.timestamp_ns;
    std::lock_guard<std::mutex> lk(g_mtx);
    g_latencies.push_back(lat);
  });

  pool.Start();

  constexpr uint64_t kSamples = 50000U;
  for (uint64_t i = 0U; i < kSamples; ++i) {
    BenchJob job{i, NowNs()};
    while (!pool.Submit(std::move(job))) {
      std::this_thread::yield();
    }
    // Pace: avoid flooding
    if (i % 100 == 0) {
      std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
  }

  pool.FlushAndPause();

  std::sort(g_latencies.begin(), g_latencies.end());
  auto p50 = g_latencies[g_latencies.size() * 50 / 100];
  auto p95 = g_latencies[g_latencies.size() * 95 / 100];
  auto p99 = g_latencies[g_latencies.size() * 99 / 100];
  auto pmax = g_latencies.back();

  std::printf("\nLatency distribution (%lu samples):\n", static_cast<unsigned long>(g_latencies.size()));
  std::printf("  P50:  %lu us\n", static_cast<unsigned long>(p50 / 1000U));
  std::printf("  P95:  %lu us\n", static_cast<unsigned long>(p95 / 1000U));
  std::printf("  P99:  %lu us\n", static_cast<unsigned long>(p99 / 1000U));
  std::printf("  Max:  %lu us\n", static_cast<unsigned long>(pmax / 1000U));

  pool.Resume();
  pool.Shutdown();
}

int main() {
  std::printf("=== WorkerPool Benchmark ===\n\n");

  BenchSpsc();

  std::printf("\nMulti-producer throughput:\n");
  BenchMultiProducer(1, 1);
  BenchMultiProducer(2, 2);
  BenchMultiProducer(4, 4);

  BenchLatency();

  std::printf("\nDone.\n");
  return 0;
}
