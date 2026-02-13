// Copyright (c) 2024 liudegui. MIT License.

#include "test_types.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
#include <wp/worker_pool.hpp>

TEST_CASE("Stats track dispatched and processed", "[stats]") {
  static std::atomic<int32_t> g_count{0};
  g_count.store(0);

  wp::Config cfg;
  cfg.name = "stats";
  cfg.worker_num = 2;
  cfg.worker_queue_depth = 128;

  wp::WorkerPool<TestPayload> pool(cfg);
  pool.RegisterHandler<TaskA>(
      [](const TaskA&, const mccc::MessageHeader&) { g_count.fetch_add(1, std::memory_order_relaxed); });

  pool.Start();

  constexpr int32_t kJobs = 100;
  for (int32_t i = 0; i < kJobs; ++i) {
    while (!pool.Submit(TaskA{i, 0})) {
      std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
  }

  pool.FlushAndPause();

  auto stats = pool.GetStats();
  CHECK(stats.dispatched > 0U);
  CHECK(stats.dispatched == stats.processed);
  CHECK(stats.bus_stats.messages_published > 0U);

  pool.Resume();
  pool.Shutdown();
}

TEST_CASE("Stats initial values are zero", "[stats]") {
  wp::Config cfg;
  cfg.name = "stats-init";

  wp::WorkerPool<TestPayload> pool(cfg);
  pool.RegisterHandler<TaskA>([](const TaskA&, const mccc::MessageHeader&) {});

  auto stats = pool.GetStats();
  CHECK(stats.dispatched == 0U);
  CHECK(stats.processed == 0U);
  CHECK(stats.worker_queue_full == 0U);
}

TEST_CASE("Bus stats reflect published messages", "[stats]") {
  wp::Config cfg;
  cfg.name = "bus-stats";
  cfg.worker_num = 1;
  cfg.worker_queue_depth = 128;

  // Reset bus stats before test
  mccc::AsyncBus<TestPayload>::Instance().ResetStatistics();

  wp::WorkerPool<TestPayload> pool(cfg);
  pool.RegisterHandler<TaskA>([](const TaskA&, const mccc::MessageHeader&) {});

  pool.Start();

  constexpr int32_t kJobs = 50;
  int32_t published = 0;
  for (int32_t i = 0; i < kJobs; ++i) {
    if (pool.Submit(TaskA{i, 0})) {
      ++published;
    }
  }

  pool.FlushAndPause();

  auto stats = pool.GetStats();
  CHECK(stats.bus_stats.messages_published >= static_cast<uint64_t>(published));

  pool.Resume();
  pool.Shutdown();
}
