#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cp/consumer_producer.hpp>
#include <thread>

struct SimpleJob {
  int value{0};
};

TEST_CASE("Basic enqueue and consume", "[basic]") {
  std::atomic<int> processed{0};

  cp::Config cfg;
  cfg.name = "basic";
  cfg.worker_num = 1;
  cfg.queue_size = 16;
  cfg.hi_queue_size = 4;

  cp::ConsumerProducer<SimpleJob> cp(cfg, [&](std::shared_ptr<SimpleJob> job, bool) -> int32_t {
    processed.fetch_add(job->value, std::memory_order_relaxed);
    return 0;
  });

  cp.Start();

  for (int i = 1; i <= 10; ++i) {
    auto job = std::make_shared<SimpleJob>();
    job->value = i;
    cp.AddJob(job);
  }

  cp.Shutdown();
  REQUIRE(processed.load() == 55);  // 1+2+...+10
}

TEST_CASE("AddJobWaitDone blocks until processed", "[basic]") {
  std::atomic<int> last_processed{0};

  cp::Config cfg;
  cfg.name = "wait_done";
  cfg.worker_num = 1;
  cfg.queue_size = 8;
  cfg.hi_queue_size = 4;

  cp::ConsumerProducer<SimpleJob> cp(cfg, [&](std::shared_ptr<SimpleJob> job, bool) -> int32_t {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    last_processed.store(job->value, std::memory_order_relaxed);
    return 0;
  });

  cp.Start();

  auto job = std::make_shared<SimpleJob>();
  job->value = 42;
  cp.AddJobWaitDone(job);

  // After AddJobWaitDone returns, the job must have been processed
  REQUIRE(last_processed.load() == 42);

  cp.Shutdown();
}

TEST_CASE("FlushAndPause then Resume", "[basic]") {
  std::atomic<int> count{0};

  cp::Config cfg;
  cfg.name = "flush";
  cfg.worker_num = 2;
  cfg.queue_size = 32;
  cfg.hi_queue_size = 4;

  cp::ConsumerProducer<SimpleJob> cp(cfg, [&](std::shared_ptr<SimpleJob>, bool) -> int32_t {
    count.fetch_add(1, std::memory_order_relaxed);
    return 0;
  });

  cp.Start();

  for (int i = 0; i < 20; ++i) {
    cp.AddJob(std::make_shared<SimpleJob>());
  }

  cp.FlushAndPause();
  REQUIRE(count.load() == 20);

  // Add more after resume
  cp.Resume();
  for (int i = 0; i < 5; ++i) {
    cp.AddJob(std::make_shared<SimpleJob>());
  }

  cp.Shutdown();
  REQUIRE(count.load() == 25);
}

TEST_CASE("Stats reporting", "[basic]") {
  cp::Config cfg;
  cfg.name = "stats";
  cfg.worker_num = 1;
  cfg.queue_size = 8;
  cfg.hi_queue_size = 4;

  cp::ConsumerProducer<SimpleJob> cp(cfg, [](std::shared_ptr<SimpleJob>, bool) -> int32_t { return 0; });

  cp.Start();
  for (int i = 0; i < 5; ++i) {
    cp.AddJob(std::make_shared<SimpleJob>());
  }
  cp.Shutdown();

  std::string stats = cp.GetStatsString();
  REQUIRE_FALSE(stats.empty());
  REQUIRE(stats.find("stats") != std::string::npos);
}
