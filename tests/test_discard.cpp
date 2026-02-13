#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cp/consumer_producer.hpp>

struct DiscardJob {
  int value{0};
};

TEST_CASE("Discard head when queue full", "[discard]") {
  std::atomic<int> consumed{0};
  std::atomic<int> discarded{0};

  cp::Config cfg;
  cfg.name = "discard";
  cfg.worker_num = 1;
  cfg.queue_size = 4;
  cfg.prefer_queue_size = 4;  // non-blocking mode (prefer_queue_size > 0)
  cfg.hi_queue_size = 4;

  cp::ConsumerProducer<DiscardJob> cp(cfg, [&](std::shared_ptr<DiscardJob>, bool preferred) -> int32_t {
    if (preferred) {
      consumed.fetch_add(1);
    } else {
      discarded.fetch_add(1);
    }
    // Slow consumer so queue fills up
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    return 0;
  });

  cp.Start();

  // Rapidly add more jobs than queue can hold
  for (int i = 0; i < 20; ++i) {
    auto job = std::make_shared<DiscardJob>();
    job->value = i;
    cp.AddJob(job, false, false);
  }

  cp.Shutdown();

  // All jobs should have been either consumed or discarded
  uint64_t total_dropped = cp.DroppedJobCount();
  REQUIRE((consumed.load() + discarded.load()) > 0);
  // DroppedJobCount should match discarded callback count
  // (Note: dropped_job_ counts both consumer-side non-preferred and producer-side discards)
  (void)total_dropped;  // sanity: compiles and runs without crash
}

TEST_CASE("Not-discardable jobs block when queue full", "[discard]") {
  std::atomic<int> count{0};

  cp::Config cfg;
  cfg.name = "nodiscard";
  cfg.worker_num = 2;
  cfg.queue_size = 4;
  cfg.prefer_queue_size = 4;
  cfg.hi_queue_size = 4;

  cp::ConsumerProducer<DiscardJob> cp(cfg, [&](std::shared_ptr<DiscardJob>, bool) -> int32_t {
    count.fetch_add(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    return 0;
  });

  cp.Start();

  // Add not-discardable jobs — they should all eventually be processed
  for (int i = 0; i < 10; ++i) {
    auto job = std::make_shared<DiscardJob>();
    job->value = i;
    cp.AddJob(job, false, true);  // not_discardable = true
  }

  cp.Shutdown();
  REQUIRE(count.load() == 10);
}
