#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cp/consumer_producer.hpp>
#include <thread>

struct EdgeJob {
  int value{0};
};

TEST_CASE("Single worker processes all jobs", "[edge]") {
  cp::Config cfg;
  cfg.name = "single_worker";
  cfg.worker_num = 1;
  cfg.queue_size = 8;
  cfg.hi_queue_size = 4;

  std::atomic<int> sum{0};
  cp::ConsumerProducer<EdgeJob> cp(cfg, [&](std::shared_ptr<EdgeJob> job, bool) -> int32_t {
    sum.fetch_add(job->value, std::memory_order_relaxed);
    return 0;
  });

  cp.Start();
  for (int i = 1; i <= 100; ++i) {
    auto job = std::make_shared<EdgeJob>();
    job->value = i;
    cp.AddJob(job, false, true);
  }
  cp.Shutdown();
  REQUIRE(sum.load() == 5050);
}

TEST_CASE("Multiple AddJobWaitDone sequential", "[edge]") {
  cp::Config cfg;
  cfg.name = "multi_wait";
  cfg.worker_num = 2;
  cfg.queue_size = 8;
  cfg.hi_queue_size = 4;

  std::atomic<int> last{0};
  cp::ConsumerProducer<EdgeJob> cp(cfg, [&](std::shared_ptr<EdgeJob> job, bool) -> int32_t {
    last.store(job->value, std::memory_order_relaxed);
    return 0;
  });

  cp.Start();

  for (int i = 1; i <= 5; ++i) {
    auto job = std::make_shared<EdgeJob>();
    job->value = i;
    cp.AddJobWaitDone(job);
    REQUIRE(last.load() == i);
  }

  cp.Shutdown();
}

TEST_CASE("Immediate shutdown after start", "[edge]") {
  cp::Config cfg;
  cfg.name = "fast_shutdown";
  cfg.worker_num = 4;
  cfg.queue_size = 16;
  cfg.hi_queue_size = 4;

  cp::ConsumerProducer<EdgeJob> cp(cfg, [](std::shared_ptr<EdgeJob>, bool) -> int32_t { return 0; });

  cp.Start();
  cp.Shutdown();

  // Should not crash, queue should be clean
  REQUIRE(cp.QueueLength() == 0);
}

TEST_CASE("Shutdown without start", "[edge]") {
  cp::Config cfg;
  cfg.name = "no_start";
  cfg.worker_num = 1;
  cfg.queue_size = 8;
  cfg.hi_queue_size = 4;

  cp::ConsumerProducer<EdgeJob> cp(cfg, [](std::shared_ptr<EdgeJob>, bool) -> int32_t { return 0; });

  // Destructor should handle this gracefully (shutdown_ is already true)
  REQUIRE(cp.MaxQueueLength() == 0);
  REQUIRE(cp.BlockedJobCount() == 0);
}

TEST_CASE("High-priority AddJobWaitDone", "[edge]") {
  cp::Config cfg;
  cfg.name = "hi_wait";
  cfg.worker_num = 1;
  cfg.queue_size = 8;
  cfg.hi_queue_size = 8;

  std::atomic<int> value{0};
  cp::ConsumerProducer<EdgeJob> cp(cfg, [&](std::shared_ptr<EdgeJob> job, bool preferred) -> int32_t {
    REQUIRE(preferred);
    value.store(job->value, std::memory_order_relaxed);
    return 0;
  });

  cp.Start();

  auto job = std::make_shared<EdgeJob>();
  job->value = 42;
  cp.AddJobWaitDone(job, true);
  REQUIRE(value.load() == 42);

  cp.Shutdown();
}
