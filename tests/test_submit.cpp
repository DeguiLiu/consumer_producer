// Copyright (c) 2024 liudegui. MIT License.

#include "test_types.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
#include <wp/worker_pool.hpp>

TEST_CASE("Submit single job", "[submit]") {
  std::atomic<int32_t> received_id{-1};

  wp::Config cfg;
  cfg.name = "submit-1";
  cfg.worker_num = 1;
  cfg.worker_queue_depth = 64;

  wp::WorkerPool<TestPayload> pool(cfg);
  pool.RegisterHandler<TaskA>([](const TaskA& t, const mccc::MessageHeader&) {
    // Handler is called via function pointer; use a global to capture
    // This won't work with captures. Use a different approach.
    (void)t;
  });

  // Use a global atomic for testing
  static std::atomic<int32_t> g_received{-1};
  g_received.store(-1, std::memory_order_relaxed);

  pool.RegisterHandler<TaskA>(
      [](const TaskA& t, const mccc::MessageHeader&) { g_received.store(t.id, std::memory_order_release); });

  pool.Start();
  CHECK(pool.Submit(TaskA{42, 0}));

  // Wait for processing
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (g_received.load(std::memory_order_acquire) != 42) {
    if (std::chrono::steady_clock::now() > deadline) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  CHECK(g_received.load(std::memory_order_acquire) == 42);

  pool.Shutdown();
}

TEST_CASE("Submit multiple job types", "[submit]") {
  static std::atomic<int32_t> g_task_a_count{0};
  static std::atomic<int32_t> g_task_b_count{0};
  g_task_a_count.store(0);
  g_task_b_count.store(0);

  wp::Config cfg;
  cfg.name = "submit-multi";
  cfg.worker_num = 2;
  cfg.worker_queue_depth = 128;

  wp::WorkerPool<TestPayload> pool(cfg);
  pool.RegisterHandler<TaskA>(
      [](const TaskA&, const mccc::MessageHeader&) { g_task_a_count.fetch_add(1, std::memory_order_relaxed); });
  pool.RegisterHandler<TaskB>(
      [](const TaskB&, const mccc::MessageHeader&) { g_task_b_count.fetch_add(1, std::memory_order_relaxed); });

  pool.Start();

  constexpr int32_t kCount = 100;
  for (int32_t i = 0; i < kCount; ++i) {
    pool.Submit(TaskA{i, 0});
    pool.Submit(TaskB{static_cast<float>(i), 0.0f});
  }

  // Wait for processing
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (g_task_a_count.load() < kCount || g_task_b_count.load() < kCount) {
    if (std::chrono::steady_clock::now() > deadline) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  CHECK(g_task_a_count.load() == kCount);
  CHECK(g_task_b_count.load() == kCount);

  pool.Shutdown();
}

TEST_CASE("SubmitSync executes in caller thread", "[submit]") {
  static std::atomic<bool> g_called{false};
  static std::thread::id g_caller_id{};
  g_called.store(false);

  wp::Config cfg;
  cfg.name = "sync";

  wp::WorkerPool<TestPayload> pool(cfg);
  pool.RegisterHandler<TaskA>([](const TaskA&, const mccc::MessageHeader&) {
    g_called.store(true, std::memory_order_release);
    g_caller_id = std::this_thread::get_id();
  });

  // SubmitSync works without Start()
  std::thread::id main_id = std::this_thread::get_id();
  CHECK(pool.SubmitSync(TaskA{1, 0}));
  CHECK(g_called.load(std::memory_order_acquire));
  CHECK(g_caller_id == main_id);
}

TEST_CASE("SubmitSync returns false for unregistered type", "[submit]") {
  wp::Config cfg;
  cfg.name = "sync-noreg";

  wp::WorkerPool<TestPayload> pool(cfg);
  pool.RegisterHandler<TaskA>([](const TaskA&, const mccc::MessageHeader&) {});

  // TaskB has no handler
  CHECK_FALSE(pool.SubmitSync(TaskB{1.0f, 2.0f}));
}

TEST_CASE("Submit returns false when paused", "[submit]") {
  static std::atomic<int32_t> g_count{0};
  g_count.store(0);

  wp::Config cfg;
  cfg.name = "paused-submit";
  cfg.worker_num = 1;
  cfg.worker_queue_depth = 64;

  wp::WorkerPool<TestPayload> pool(cfg);
  pool.RegisterHandler<TaskA>(
      [](const TaskA&, const mccc::MessageHeader&) { g_count.fetch_add(1, std::memory_order_relaxed); });

  pool.Start();
  pool.Submit(TaskA{1, 0});

  // Wait for processing
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (g_count.load() < 1) {
    if (std::chrono::steady_clock::now() > deadline) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  pool.FlushAndPause();
  CHECK(pool.IsPaused());
  CHECK_FALSE(pool.Submit(TaskA{2, 0}));

  pool.Resume();
  CHECK_FALSE(pool.IsPaused());
  CHECK(pool.Submit(TaskA{3, 0}));

  pool.Shutdown();
}
