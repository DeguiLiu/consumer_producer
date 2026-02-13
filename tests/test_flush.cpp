// Copyright (c) 2024 liudegui. MIT License.

#include "test_types.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
#include <wp/worker_pool.hpp>

TEST_CASE("FlushAndPause drains all jobs", "[flush]") {
  static std::atomic<int32_t> g_count{0};
  g_count.store(0);

  wp::Config cfg;
  cfg.name = "flush";
  cfg.worker_num = 2;
  cfg.worker_queue_depth = 128;

  wp::WorkerPool<TestPayload> pool(cfg);
  pool.RegisterHandler<TaskA>(
      [](const TaskA&, const mccc::MessageHeader&) { g_count.fetch_add(1, std::memory_order_relaxed); });

  pool.Start();

  constexpr int32_t kJobs = 200;
  for (int32_t i = 0; i < kJobs; ++i) {
    while (!pool.Submit(TaskA{i, 0})) {
      std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
  }

  pool.FlushAndPause();

  auto stats = pool.GetStats();
  CHECK(stats.dispatched == stats.processed);
  CHECK(pool.IsPaused());

  pool.Resume();
  pool.Shutdown();
}

TEST_CASE("Resume after pause accepts new jobs", "[flush]") {
  static std::atomic<int32_t> g_count{0};
  g_count.store(0);

  wp::Config cfg;
  cfg.name = "resume";
  cfg.worker_num = 1;
  cfg.worker_queue_depth = 64;

  wp::WorkerPool<TestPayload> pool(cfg);
  pool.RegisterHandler<TaskA>(
      [](const TaskA&, const mccc::MessageHeader&) { g_count.fetch_add(1, std::memory_order_relaxed); });

  pool.Start();

  // Submit some jobs
  for (int32_t i = 0; i < 10; ++i) {
    pool.Submit(TaskA{i, 0});
  }

  pool.FlushAndPause();
  CHECK(pool.IsPaused());
  CHECK_FALSE(pool.Submit(TaskA{99, 0}));

  pool.Resume();
  CHECK_FALSE(pool.IsPaused());

  // Submit more after resume
  for (int32_t i = 0; i < 10; ++i) {
    pool.Submit(TaskA{i, 0});
  }

  pool.FlushAndPause();

  auto stats = pool.GetStats();
  CHECK(stats.dispatched == stats.processed);

  pool.Resume();
  pool.Shutdown();
}

TEST_CASE("FlushAndPause with no pending jobs returns immediately", "[flush]") {
  wp::Config cfg;
  cfg.name = "flush-empty";
  cfg.worker_num = 1;

  wp::WorkerPool<TestPayload> pool(cfg);
  pool.RegisterHandler<TaskA>([](const TaskA&, const mccc::MessageHeader&) {});

  pool.Start();

  // No jobs submitted - flush should return quickly
  auto start = std::chrono::steady_clock::now();
  pool.FlushAndPause();
  auto elapsed = std::chrono::steady_clock::now() - start;

  CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() < 500);

  pool.Resume();
  pool.Shutdown();
}
