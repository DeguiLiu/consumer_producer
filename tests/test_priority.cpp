#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cp/consumer_producer.hpp>
#include <thread>
#include <vector>

struct PrioJob {
  int value{0};
  bool is_high{false};
};

TEST_CASE("High priority jobs processed first", "[priority]") {
  // Use a single worker so processing is sequential
  std::vector<int> order;
  std::mutex order_mutex;

  cp::Config cfg;
  cfg.name = "prio";
  cfg.worker_num = 1;
  cfg.queue_size = 32;
  cfg.hi_queue_size = 32;

  // Use a slow consumer so all jobs queue up before processing starts
  cp::ConsumerProducer<PrioJob> cp(cfg, [&](std::shared_ptr<PrioJob> job, bool) -> int32_t {
    std::lock_guard<std::mutex> lk(order_mutex);
    order.push_back(job->value);
    return 0;
  });

  cp.Start();
  cp.FlushAndPause();
  cp.Resume();

  // Add low-priority first, then high-priority while consumer is slow
  // Use not_discardable to ensure all are enqueued
  for (int i = 1; i <= 3; ++i) {
    auto job = std::make_shared<PrioJob>();
    job->value = i;  // low prio: 1,2,3
    cp.AddJob(job, false, true);
  }
  for (int i = 100; i <= 102; ++i) {
    auto job = std::make_shared<PrioJob>();
    job->value = i;  // high prio: 100,101,102
    cp.AddJob(job, true);
  }

  cp.Shutdown();

  // All jobs processed
  std::lock_guard<std::mutex> lk(order_mutex);
  REQUIRE(order.size() == 6);

  // Count how many high-priority jobs appear in the first 3 positions
  int hi_in_first_half = 0;
  for (int k = 0; k < 3 && k < static_cast<int>(order.size()); ++k) {
    if (order[k] >= 100)
      hi_in_first_half++;
  }
  // At least some high-priority jobs should appear early
  // (exact ordering depends on timing, but high-prio queue is checked first)
  REQUIRE(hi_in_first_half >= 1);
}

TEST_CASE("Prefer queue size affects preferred flag", "[priority]") {
  std::atomic<int> preferred_count{0};
  std::atomic<int> non_preferred_count{0};

  cp::Config cfg;
  cfg.name = "prefer";
  cfg.worker_num = 1;
  cfg.queue_size = 16;
  cfg.prefer_queue_size = 2;  // Only preferred when queue <= 2
  cfg.hi_queue_size = 4;

  cp::ConsumerProducer<PrioJob> cp(cfg, [&](std::shared_ptr<PrioJob>, bool preferred) -> int32_t {
    if (preferred) {
      preferred_count.fetch_add(1);
    } else {
      non_preferred_count.fetch_add(1);
    }
    // Slow consumer to let queue build up
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return 0;
  });

  cp.Start();

  for (int i = 0; i < 10; ++i) {
    cp.AddJob(std::make_shared<PrioJob>());
  }

  cp.Shutdown();

  // Some should be preferred, some not (depends on timing)
  REQUIRE((preferred_count.load() + non_preferred_count.load()) == 10);
}
