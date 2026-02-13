// Copyright (c) 2024 liudegui. MIT License.

#include "test_types.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
#include <vector>
#include <wp/worker_pool.hpp>

TEST_CASE("Multi-producer submit", "[concurrent]") {
  static std::atomic<uint64_t> g_sum{0U};
  g_sum.store(0U);

  wp::Config cfg;
  cfg.name = "mp";
  cfg.worker_num = 2;
  cfg.worker_queue_depth = 1024;

  wp::WorkerPool<TestPayload> pool(cfg);
  pool.RegisterHandler<TaskA>([](const TaskA& t, const mccc::MessageHeader&) {
    g_sum.fetch_add(static_cast<uint64_t>(t.value), std::memory_order_relaxed);
  });

  pool.Start();

  constexpr uint32_t kProducers = 4U;
  constexpr int32_t kJobsPerProducer = 500;

  std::vector<std::thread> producers;
  producers.reserve(kProducers);
  for (uint32_t p = 0U; p < kProducers; ++p) {
    producers.emplace_back([&pool]() {
      for (int32_t i = 0; i < kJobsPerProducer; ++i) {
        while (!pool.Submit(TaskA{0, 1})) {
          std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
      }
    });
  }

  for (auto& t : producers) {
    t.join();
  }

  // Wait for all processing
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  const uint64_t expected = kProducers * kJobsPerProducer;
  while (g_sum.load(std::memory_order_acquire) < expected) {
    if (std::chrono::steady_clock::now() > deadline) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  CHECK(g_sum.load(std::memory_order_acquire) == expected);

  pool.Shutdown();
}

TEST_CASE("Multi-worker processing", "[concurrent]") {
  static std::atomic<int32_t> g_processed{0};
  g_processed.store(0);

  wp::Config cfg;
  cfg.name = "mw";
  cfg.worker_num = 4;
  cfg.worker_queue_depth = 256;

  wp::WorkerPool<TestPayload> pool(cfg);
  pool.RegisterHandler<TaskC>(
      [](const TaskC&, const mccc::MessageHeader&) { g_processed.fetch_add(1, std::memory_order_relaxed); });

  pool.Start();

  constexpr int32_t kJobs = 1000;
  for (int32_t i = 0; i < kJobs; ++i) {
    while (!pool.Submit(TaskC{static_cast<uint64_t>(i)})) {
      std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
  }

  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (g_processed.load(std::memory_order_acquire) < kJobs) {
    if (std::chrono::steady_clock::now() > deadline) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  CHECK(g_processed.load(std::memory_order_acquire) == kJobs);

  pool.Shutdown();
}

TEST_CASE("Priority submit", "[concurrent]") {
  static std::atomic<int32_t> g_high{0};
  static std::atomic<int32_t> g_low{0};
  g_high.store(0);
  g_low.store(0);

  wp::Config cfg;
  cfg.name = "prio";
  cfg.worker_num = 1;
  cfg.worker_queue_depth = 256;

  wp::WorkerPool<TestPayload> pool(cfg);
  pool.RegisterHandler<TaskA>([](const TaskA& t, const mccc::MessageHeader& hdr) {
    if (hdr.priority == mccc::MessagePriority::HIGH) {
      g_high.fetch_add(1, std::memory_order_relaxed);
    } else {
      g_low.fetch_add(1, std::memory_order_relaxed);
    }
    (void)t;
  });

  pool.Start();

  constexpr int32_t kCount = 50;
  for (int32_t i = 0; i < kCount; ++i) {
    pool.Submit(TaskA{i, 0}, mccc::MessagePriority::HIGH);
    pool.Submit(TaskA{i, 0}, mccc::MessagePriority::LOW);
  }

  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (g_high.load() + g_low.load() < kCount * 2) {
    if (std::chrono::steady_clock::now() > deadline) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  CHECK(g_high.load() == kCount);
  // Low priority may be dropped by mccc admission control under load
  CHECK(g_low.load() <= kCount);
  CHECK(g_low.load() + g_high.load() > 0);

  pool.Shutdown();
}
