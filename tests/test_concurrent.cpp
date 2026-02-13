#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cp/consumer_producer.hpp>

struct ConcJob {
  int value{0};
};

TEST_CASE("Multi-worker concurrent processing", "[concurrent]") {
  constexpr int kJobCount = 10000;
  std::atomic<int> total{0};

  cp::Config cfg;
  cfg.name = "concurrent";
  cfg.worker_num = 4;
  cfg.queue_size = 128;
  cfg.hi_queue_size = 16;

  cp::ConsumerProducer<ConcJob> cp(cfg, [&](std::shared_ptr<ConcJob> job, bool) -> int32_t {
    total.fetch_add(job->value, std::memory_order_relaxed);
    return 0;
  });

  cp.Start();

  int64_t expected = 0;
  for (int i = 0; i < kJobCount; ++i) {
    auto job = std::make_shared<ConcJob>();
    job->value = i;
    expected += i;
    cp.AddJob(job, false, true);  // not_discardable to ensure all processed
  }

  cp.Shutdown();
  REQUIRE(total.load() == static_cast<int>(expected));
}

TEST_CASE("Multi-producer threads", "[concurrent]") {
  constexpr int kProducers = 4;
  constexpr int kJobsPerProducer = 1000;
  std::atomic<int> count{0};

  cp::Config cfg;
  cfg.name = "multi_prod";
  cfg.worker_num = 2;
  cfg.queue_size = 64;
  cfg.hi_queue_size = 16;

  cp::ConsumerProducer<ConcJob> cp(cfg, [&](std::shared_ptr<ConcJob>, bool) -> int32_t {
    count.fetch_add(1, std::memory_order_relaxed);
    return 0;
  });

  cp.Start();

  std::vector<std::thread> producers;
  for (int p = 0; p < kProducers; ++p) {
    producers.emplace_back([&]() {
      for (int i = 0; i < kJobsPerProducer; ++i) {
        cp.AddJob(std::make_shared<ConcJob>(), false, true);
      }
    });
  }

  for (auto& t : producers)
    t.join();
  cp.Shutdown();

  REQUIRE(count.load() == kProducers * kJobsPerProducer);
}
