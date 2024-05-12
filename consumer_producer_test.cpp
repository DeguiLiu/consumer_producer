#include "consumer_producer.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <utility>
// Define your worker class if it's not provided
struct YourWorker {
  int id;
  std::string data;
  // Add necessary members and methods
  void process() {
    // Implement processing logic
    std::cout << "Processing data: " << data << std::endl;
  }
};

int32_t consume_func(std::shared_ptr<YourWorker> in_job, bool prefer) {
  // Implement your consume logic here
  // This function will be called by the ConsumerProducer
  if (in_job != nullptr) {
    in_job->process();  // Process the job
    return 0;           // Return appropriate value based on your logic
  } else {
    return -1;  // Indicate failure if job is nullptr
  }
}

int main() {
  // Define your configuration
  MyTest::ConsumerProducerConfig config;
  config.in_name = "YourConsumerProducer";  // Name of the ConsumerProducer
  config.in_priority = 0;                   // Priority of the threads
  config.in_worker_num = 4;                 // Number of threads to create
  config.in_queue_size = 100;               // Size of the queue for jobs
  config.in_prefer_queue_size = 10;         // Size of the preferred jobs in the low priority queue
  config.in_hi_cp_queue_size = 20;          // Size of the queue for high priority jobs
  config.in_cp_cpu_set_size = 0;            // Size of the CPU set (0 for default)
  config.in_cp_cpuset = nullptr;            // CPU set to bind the threads (nullptr for default)
  config.in_allow_log = true;               // Allow logging

  // Create an instance of ConsumerProducer
  MyTest::ConsumerProducer<YourWorker> consumerProducer(config, consume_func);

  // Start processing jobs
  consumerProducer.start_process();

  // Add some jobs to the queue
  for (int i = 0; i < 50; ++i) {
    std::shared_ptr<YourWorker> job_ptr = std::make_shared<YourWorker>();
    job_ptr->id = i;
    job_ptr->data = "Data " + std::to_string(i);
    consumerProducer.add_job(std::move(job_ptr));  // Transfer ownership
  }

  // Wait for some time or do other operations

  // Shutdown the threads
  consumerProducer.shutdown_threads();

  return 0;
}
