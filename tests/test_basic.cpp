// Copyright (c) 2024 liudegui. MIT License.

#include "test_types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <wp/worker_pool.hpp>

TEST_CASE("WorkerPool default config", "[basic]") {
  wp::Config cfg;
  CHECK(cfg.worker_num == 1U);
  CHECK(cfg.worker_queue_depth == wp::kDefaultWorkerQueueDepth);
  CHECK(cfg.priority == 0);
}

TEST_CASE("WorkerPool construct and destroy without start", "[basic]") {
  wp::Config cfg;
  cfg.name = "test";
  cfg.worker_num = 2;

  wp::WorkerPool<TestPayload> pool(cfg);
  CHECK_FALSE(pool.IsRunning());
  CHECK_FALSE(pool.IsPaused());
  CHECK(pool.WorkerCount() == 2U);
}

TEST_CASE("WorkerPool start and shutdown", "[basic]") {
  wp::Config cfg;
  cfg.name = "lifecycle";
  cfg.worker_num = 2;
  cfg.worker_queue_depth = 64;

  wp::WorkerPool<TestPayload> pool(cfg);
  pool.RegisterHandler<TaskA>([](const TaskA&, const mccc::MessageHeader&) {});
  pool.Start();
  CHECK(pool.IsRunning());

  pool.Shutdown();
  CHECK_FALSE(pool.IsRunning());
}

TEST_CASE("WorkerPool double start is safe", "[basic]") {
  wp::Config cfg;
  cfg.name = "double-start";

  wp::WorkerPool<TestPayload> pool(cfg);
  pool.RegisterHandler<TaskA>([](const TaskA&, const mccc::MessageHeader&) {});
  pool.Start();
  pool.Start();  // Should not crash or double-create threads
  CHECK(pool.IsRunning());
  pool.Shutdown();
}

TEST_CASE("WorkerPool double shutdown is safe", "[basic]") {
  wp::Config cfg;
  cfg.name = "double-shutdown";

  wp::WorkerPool<TestPayload> pool(cfg);
  pool.RegisterHandler<TaskA>([](const TaskA&, const mccc::MessageHeader&) {});
  pool.Start();
  pool.Shutdown();
  pool.Shutdown();  // Should not crash
  CHECK_FALSE(pool.IsRunning());
}

TEST_CASE("SpscQueue basic push pop", "[basic]") {
  wp::SpscQueue<int32_t> q(4);
  CHECK(q.Empty());
  CHECK(q.Size() == 0U);
  CHECK(q.Capacity() == 4U);

  CHECK(q.TryPush(10));
  CHECK(q.TryPush(20));
  CHECK(q.Size() == 2U);
  CHECK_FALSE(q.Empty());

  int32_t val = 0;
  CHECK(q.TryPop(val));
  CHECK(val == 10);
  CHECK(q.TryPop(val));
  CHECK(val == 20);
  CHECK(q.Empty());
}

TEST_CASE("SpscQueue full and wraparound", "[basic]") {
  wp::SpscQueue<int32_t> q(4);

  // Fill queue
  for (int32_t i = 0; i < 4; ++i) {
    CHECK(q.TryPush(i));
  }
  CHECK_FALSE(q.TryPush(99));  // Full

  // Pop all
  int32_t val = 0;
  for (int32_t i = 0; i < 4; ++i) {
    CHECK(q.TryPop(val));
    CHECK(val == i);
  }
  CHECK_FALSE(q.TryPop(val));  // Empty

  // Wraparound: push again
  for (int32_t i = 10; i < 14; ++i) {
    CHECK(q.TryPush(i));
  }
  for (int32_t i = 10; i < 14; ++i) {
    CHECK(q.TryPop(val));
    CHECK(val == i);
  }
}

TEST_CASE("SpscQueue rounds up to power of 2", "[basic]") {
  wp::SpscQueue<int32_t> q(5);
  CHECK(q.Capacity() == 8U);

  wp::SpscQueue<int32_t> q2(1);
  CHECK(q2.Capacity() == 1U);

  wp::SpscQueue<int32_t> q3(3);
  CHECK(q3.Capacity() == 4U);
}
