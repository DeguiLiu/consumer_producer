#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cp/consumer_producer.hpp>
#include <thread>
#include <vector>

struct StatsJob {
  int value{0};
};

TEST_CASE("Atomic blocked count accuracy", "[stats]") {
  cp::Config cfg;
  cfg.name = "blocked_stats";
  cfg.worker_num = 1;
  cfg.queue_size = 2;
  cfg.hi_queue_size = 2;

  std::atomic<int> processed{0};
  cp::ConsumerProducer<StatsJob> cp(cfg, [&](std::shared_ptr<StatsJob>, bool) -> int32_t {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    processed.fetch_add(1, std::memory_order_relaxed);
    return 0;
  });

  cp.Start();

  // Fill queue and cause blocking
  for (int i = 0; i < 10; ++i) {
    cp.AddJob(std::make_shared<StatsJob>(), false, true);
  }

  cp.Shutdown();
  REQUIRE(processed.load() == 10);
  // Some jobs should have blocked (queue size is only 2)
  REQUIRE(cp.BlockedJobCount() > 0);
}

TEST_CASE("MaxQueueLength tracks peak", "[stats]") {
  cp::Config cfg;
  cfg.name = "max_queue";
  cfg.worker_num = 1;
  cfg.queue_size = 32;
  cfg.hi_queue_size = 4;

  cp::ConsumerProducer<StatsJob> cp(cfg, [](std::shared_ptr<StatsJob>, bool) -> int32_t {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return 0;
  });

  cp.Start();

  // Rapidly enqueue to build up queue
  for (int i = 0; i < 10; ++i) {
    cp.AddJob(std::make_shared<StatsJob>());
  }

  // Give time for some processing
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  uint32_t max_len = cp.MaxQueueLength();
  REQUIRE(max_len > 0);
  REQUIRE(max_len <= 32);

  cp.Shutdown();
}

TEST_CASE("DroppedJobCount with discard mode", "[stats]") {
  cp::Config cfg;
  cfg.name = "drop_stats";
  cfg.worker_num = 1;
  cfg.queue_size = 2;
  cfg.prefer_queue_size = 2;  // non-blocking mode
  cfg.hi_queue_size = 2;

  cp::ConsumerProducer<StatsJob> cp(cfg, [](std::shared_ptr<StatsJob>, bool) -> int32_t {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return 0;
  });

  cp.Start();

  // Rapidly add many jobs -- some will be discarded
  for (int i = 0; i < 20; ++i) {
    cp.AddJob(std::make_shared<StatsJob>());
  }

  cp.Shutdown();
  REQUIRE(cp.DroppedJobCount() > 0);
}

TEST_CASE("GetStatsString contains name and counts", "[stats]") {
  cp::Config cfg;
  cfg.name = "stats_str";
  cfg.worker_num = 1;
  cfg.queue_size = 8;
  cfg.hi_queue_size = 4;

  cp::ConsumerProducer<StatsJob> cp(cfg, [](std::shared_ptr<StatsJob>, bool) -> int32_t { return 0; });

  cp.Start();
  for (int i = 0; i < 3; ++i) {
    cp.AddJob(std::make_shared<StatsJob>());
  }
  cp.Shutdown();

  std::string stats = cp.GetStatsString();
  REQUIRE_FALSE(stats.empty());
  REQUIRE(stats.find("stats_str") != std::string::npos);
  REQUIRE(stats.find("added=3") != std::string::npos);
}

TEST_CASE("Multi-thread stats consistency", "[stats]") {
  constexpr int kProducers = 4;
  constexpr int kJobsPerProducer = 500;

  cp::Config cfg;
  cfg.name = "mt_stats";
  cfg.worker_num = 2;
  cfg.queue_size = 64;
  cfg.hi_queue_size = 16;

  std::atomic<int> count{0};
  cp::ConsumerProducer<StatsJob> cp(cfg, [&](std::shared_ptr<StatsJob>, bool) -> int32_t {
    count.fetch_add(1, std::memory_order_relaxed);
    return 0;
  });

  cp.Start();

  std::vector<std::thread> producers;
  for (int p = 0; p < kProducers; ++p) {
    producers.emplace_back([&]() {
      for (int i = 0; i < kJobsPerProducer; ++i) {
        cp.AddJob(std::make_shared<StatsJob>(), false, true);
      }
    });
  }
  for (auto& t : producers)
    t.join();

  cp.Shutdown();
  REQUIRE(count.load() == kProducers * kJobsPerProducer);
  // BlockedJobCount should be non-negative (may or may not have blocked)
  uint64_t blocked = cp.BlockedJobCount();
  (void)blocked;  // just verify it doesn't crash
}

TEST_CASE("FlushAndPause stats complete", "[stats]") {
  cp::Config cfg;
  cfg.name = "flush_stats";
  cfg.worker_num = 2;
  cfg.queue_size = 32;
  cfg.hi_queue_size = 8;

  std::atomic<int> processed{0};
  cp::ConsumerProducer<StatsJob> cp(cfg, [&](std::shared_ptr<StatsJob>, bool) -> int32_t {
    processed.fetch_add(1, std::memory_order_relaxed);
    return 0;
  });

  cp.Start();

  for (int i = 0; i < 50; ++i) {
    cp.AddJob(std::make_shared<StatsJob>());
  }

  cp.FlushAndPause();
  REQUIRE(processed.load() == 50);

  std::string stats = cp.GetStatsString();
  REQUIRE(stats.find("added=50") != std::string::npos);

  cp.Resume();
  cp.Shutdown();
}
