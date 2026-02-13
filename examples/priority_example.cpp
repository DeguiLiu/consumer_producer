// priority_example.cpp
// Demonstrates high/low priority queue behavior.

#include <cstdio>

#include <chrono>
#include <cp/consumer_producer.hpp>
#include <memory>
#include <thread>

struct PrioTask {
  int id;
  bool high;
};

int main() {
  cp::Config cfg;
  cfg.name = "prio_demo";
  cfg.worker_num = 1;
  cfg.queue_size = 16;
  cfg.prefer_queue_size = 3;
  cfg.hi_queue_size = 8;

  cp::ConsumerProducer<PrioTask> producer(cfg, [](std::shared_ptr<PrioTask> job, bool preferred) -> int32_t {
    std::printf("[%s] %s task %d\n", preferred ? "PREF" : "NORM", job->high ? "HI" : "LO", job->id);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return 0;
  });

  producer.Start();

  // Add low-priority jobs
  for (int i = 0; i < 5; ++i) {
    auto t = std::make_shared<PrioTask>();
    t->id = i;
    t->high = false;
    producer.AddJob(t, false);
  }

  // Add high-priority jobs — these should be processed first
  for (int i = 100; i < 105; ++i) {
    auto t = std::make_shared<PrioTask>();
    t->id = i;
    t->high = true;
    producer.AddJob(t, true);
  }

  producer.Shutdown();
  producer.PrintStats();
  return 0;
}
