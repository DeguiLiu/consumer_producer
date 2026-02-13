// basic_example.cpp
// Demonstrates basic ConsumerProducer usage.

#include <cstdio>

#include <cp/consumer_producer.hpp>
#include <memory>
#include <string>

struct Task {
  int id;
  std::string data;
};

int main() {
  cp::Config cfg;
  cfg.name = "demo";
  cfg.worker_num = 2;
  cfg.queue_size = 32;
  cfg.hi_queue_size = 8;

  cp::ConsumerProducer<Task> producer(cfg, [](std::shared_ptr<Task> job, bool preferred) -> int32_t {
    std::printf("[%s] Task %d: %s\n", preferred ? "PREF" : "DROP", job->id, job->data.c_str());
    return 0;
  });

  producer.Start();

  for (int i = 0; i < 20; ++i) {
    auto task = std::make_shared<Task>();
    task->id = i;
    task->data = "payload_" + std::to_string(i);
    producer.AddJob(task);
  }

  producer.Shutdown();
  producer.PrintStats();
  return 0;
}
