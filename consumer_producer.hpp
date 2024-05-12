/*****************************************************************************************
 * File Name: consumer_producer.hpp
 *****************************************************************************************/

#ifndef UTILS_CONSUMER_PRODUCER_H_
#define UTILS_CONSUMER_PRODUCER_H_

#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "fake_log.h"

namespace MyTest {
struct ConsumerProducerConfig {
  std::string in_name;
  int32_t in_priority;
  uint32_t in_worker_num;
  uint32_t in_queue_size;
  uint32_t in_prefer_queue_size;
  uint32_t in_hi_cp_queue_size;
  uint32_t in_cp_cpu_set_size;
  const cpu_set_t *in_cp_cpuset;
  bool in_allow_log;
};
template <typename Worker>
class ConsumerProducer {
 public:
  /**
   * @brief ConsumerProducer process callback function
   * @param job       MyJob to process
   * @param prefer    True if the job is preferred, otherwise false
   */
  using ConsumeFunc = std::function<int32_t(std::shared_ptr<Worker> job, bool prefer)>;

 private:
  enum MyJobPriority : int32_t {
    PRIORITY_LOW = 0,
    PRIORITY_HIGH = 1,
    PRIORITY_MAX = 2,
  };

  class MyJob {
   private:
    std::shared_ptr<Worker> my_job_ptr_;
    int64_t job_timestamp_;
    uint64_t job_job_id_;
    bool job_not_discardable_;

   public:
    MyJob() : my_job_ptr_(nullptr), job_timestamp_(0), job_job_id_(0), job_not_discardable_(false) {
    }
    MyJob(std::shared_ptr<Worker> job_task, int64_t in_timestamp, uint64_t job_id, bool not_discardable)
        : my_job_ptr_(job_task),
          job_timestamp_(in_timestamp),
          job_job_id_(job_id),
          job_not_discardable_(not_discardable) {
    }

   public:
    inline bool is_not_discardable() const {
      return job_not_discardable_;
    }
    inline uint64_t get_job_id() const {
      return job_job_id_;
    }
    inline int64_t get_timestamp() const {
      return job_timestamp_;
    }
    std::shared_ptr<Worker> get_my_job() const {
      return my_job_ptr_;
    }
  };

  class MyCpQueue {
   private:
    std::string cpqueue_name_;
    uint32_t cpqueue_head_{0};
    uint32_t cpqueue_tail_{0};
    bool cpqueue_full_{false};
    bool cpqueue_empty_{true};
    uint32_t cpqueue_queue_size_{0};
    uint32_t cpqueue_count_{0};
    std::vector<MyJob> jobs_;
    bool cp_queue_allow_log_{false};

   public:
    MyCpQueue(const std::string &in_name, uint32_t in_queue_size, bool in_allow_log) {
      cpqueue_name_ = in_name;
      cpqueue_queue_size_ = in_queue_size;
      jobs_.resize(in_queue_size);

      cp_queue_allow_log_ = in_allow_log;
    }

    ~MyCpQueue() {
      if (cp_queue_allow_log_) {
        if (cpqueue_count_ > 0) {
          MY_LOG_ERROR("%s cpqueue_count_ = %d", cpqueue_name_.c_str(), cpqueue_count_);
        }
      }
    }

    bool is_full() const {
      return cpqueue_full_;
    }
    bool cpq_is_empty() const {
      return cpqueue_empty_;
    }
    uint32_t cp_queue_queue_length() const {
      return cpqueue_count_;
    }
    void cpq_add_job(const MyJob &in_job) {
      jobs_[cpqueue_tail_] = in_job;

      cpqueue_tail_++;
      if (cpqueue_tail_ == cpqueue_queue_size_) {
        cpqueue_tail_ = 0;
      }
      if (cpqueue_tail_ == cpqueue_head_) {
        cpqueue_full_ = true;
      }
      cpqueue_empty_ = false;
      cpqueue_count_++;
      if (cp_queue_allow_log_) {
        if (cpqueue_count_ > cpqueue_queue_size_) {
          MY_LOG_PANIC("%s cpqueue_count_ = %u cpqueue_queue_size_ = %u", cpqueue_name_.c_str(), cpqueue_count_,
                       cpqueue_queue_size_);
        }
      }
      return;
    }

    uint32_t pop(MyJob &out_job, bool peek_only = false) {
      uint32_t ret_value = cpqueue_count_;

      if (cpqueue_empty_) {
        if (cp_queue_allow_log_) {
          if (cpqueue_count_ > 0) {
            MY_LOG_PANIC("%s cpqueue_count_ = %u", cpqueue_name_.c_str(), cpqueue_count_);
          }
        }
        ret_value = 0;
      } else {
        if (cp_queue_allow_log_) {
          if (cpqueue_count_ == 0) {
            MY_LOG_PANIC("%s cpqueue_count_ = %u", cpqueue_name_.c_str(), cpqueue_count_);
          }
        } else {
        }
        out_job = jobs_[cpqueue_head_];
        if (peek_only == false) {
          cpqueue_head_++;
          if (cpqueue_head_ == cpqueue_queue_size_) {
            cpqueue_head_ = 0;
          }
          cpqueue_count_--;
          if (cpqueue_head_ == cpqueue_tail_) {
            cpqueue_empty_ = true;
            if (cp_queue_allow_log_) {
              if (cpqueue_count_ != 0) {
                MY_LOG_PANIC("%s cpqueue_count_ = %u", cpqueue_name_.c_str(), cpqueue_count_);
              }
            }
          }
        }
      }
      cpqueue_full_ = false;

      return ret_value;
    }

    inline uint32_t peek(MyJob &out_job) {
      return pop(out_job, true);
    }
  };

 private:
  std::string cp_name_;
  int32_t cp_priority_;
  MyCpQueue cp_queue_;
  std::atomic_bool blocking_;
  std::atomic_bool paused_;
  uint32_t cp_prefer_queue_size_;
  MyCpQueue cp_hi_queue_;
  uint32_t cp_worker_num_;
  std::vector<std::thread> threads_;
  ConsumeFunc cp_consume_func_;
  std::mutex cp_mutex_;
  std::condition_variable cp_not_full_;
  std::condition_variable cp_not_empty_;
  std::condition_variable cp_job_done_cond_;
  std::atomic_int cp_started_;
  std::atomic_bool cp_shutdown_;
  uint32_t cp_cpusetsize_;
  const cpu_set_t *cp_cpuset_ = nullptr;
  bool cp_allow_log_;
  int64_t start_time_;
  int64_t total_wait_time_[PRIORITY_MAX];
  int64_t total_process_time_[PRIORITY_MAX];
  int64_t total_drop_time_[PRIORITY_MAX];
  uint64_t added_job_[PRIORITY_MAX];
  uint64_t finished_job_[PRIORITY_MAX];
  uint64_t blocked_job_[PRIORITY_MAX];
  uint64_t dropped_job_[PRIORITY_MAX];
  uint32_t cp_job_id_[PRIORITY_MAX];
  uint64_t finished_job_id_[PRIORITY_MAX];
  uint32_t max_queue_length_;
  int64_t last_active_time_;
  int64_t last_elapse_time_;
  int32_t cp_pid_{0};

 private:
  /**
   * @brief Get the job id of corresponding priority
   * @param arry_idx Index of the priority
   * @return Return the job id of corresponding priority
   */
  uint32_t assign_job_id_(uint64_t arry_idx) {
    return ++cp_job_id_[arry_idx];
  }
  /**
   * @brief Mark the last finished job id of corresponding priority
   * @param arry_idx Index of the priority
   * @param id  MyJob id to mark
   */
  void update_done_job_id_(uint64_t arry_idx, uint64_t id_in) {
    finished_job_id_[arry_idx] = id_in;
  }
  /**
   * @brief Get a job from high priority queue or low priority queue
   * @param job      Buffer to store the job
   * @param priorty  Buffer to store the priority of the job
   * @return Return the queue size where the job is from
   */
  uint32_t get_job_(MyJob &out_job, MyJobPriority &priority_out) {
    bool exit_flag = false;
    uint32_t ret_value = 0;
    while (exit_flag == false) {
      std::unique_lock<std::mutex> lck(cp_mutex_);
      if (cp_queue_.cpq_is_empty() && cp_hi_queue_.cpq_is_empty() && (cp_shutdown_.load() == false)) {
        cp_not_empty_.wait(lck);
      }
      ret_value = cp_hi_queue_.pop(out_job);
      if (ret_value == 0) {
        priority_out = PRIORITY_LOW;
        ret_value = cp_queue_.pop(out_job);
      } else {
        priority_out = PRIORITY_HIGH;
      }
      lck.unlock();
      if (ret_value != 0) {
        cp_not_full_.notify_all();
        exit_flag = true;
      } else {
        if (cp_shutdown_) {
          cp_not_empty_.notify_all();
          exit_flag = true;
          ret_value = 0;
        } else {
          // do nothing
        }
      }
    }
    return ret_value;
  }
  /**
   * @brief Get the total number of added jobs
   * @return Return the total number of added jobs
   */
  uint64_t added_job_total_() const {
    uint64_t ret_value = 0;
    for (int32_t arry_index = 0; arry_index < PRIORITY_MAX; arry_index++) {
      ret_value += added_job_[arry_index];
    }
    return ret_value;
  }

  /**
   * @brief Get the total number of finished jobs
   * @return Return the total number of finished jobs
   */
  uint64_t finished_job_total_() const {
    uint64_t ret_value = 0;
    for (int32_t arry_index = 0; arry_index < PRIORITY_MAX; arry_index++) {
      ret_value += finished_job_[arry_index];
    }
    return ret_value;
  }
  /**
   * @brief Get the total number of dropped jobs
   * @return Return the total number of dropped jobs
   */
  uint64_t dropped_job_total_() const {
    uint64_t ret_value = 0;
    for (uint32_t arry_index = 0; arry_index < PRIORITY_MAX; arry_index++) {
      ret_value += dropped_job_[arry_index];
    }
    return ret_value;
  }

  static int64_t get_time_ns(clockid_t clk_id) {
    struct timespec spec;
    (void)clock_gettime(clk_id, &spec);
    return spec.tv_sec * 1000000000L + spec.tv_nsec;
  }

  static void set_self_thread_priority(int32_t in_priority) {
    bool go_on = true;
    struct sched_param params;
    struct sched_param current_params;
    int32_t set_policy{0};
    int32_t current_policy{0};
    const pthread_t this_thread = pthread_self();

    int32_t status_ret = pthread_getschedparam(this_thread, &current_policy, &current_params);
    if (status_ret != 0) {
      MY_LOG_ERROR("getschedparam %d", status_ret);
      go_on = false;
    } else {
      MY_LOG_DEBUG("thread current priority is %d (%d), target is %d", current_params.sched_priority, current_policy,
                   in_priority);  // MY_LOG_TRACE
      if (in_priority == 0) {
        go_on = false;
      } else if (in_priority > 0) {
        set_policy = SCHED_FIFO;
        params.sched_priority = current_params.sched_priority + in_priority;
      } else {
        set_policy = SCHED_IDLE;
        params.sched_priority = 0;
      }
    }
    if (go_on) {
      if (params.sched_priority > 99) {
        params.sched_priority = 99;
      }
      if (params.sched_priority < 0) {
        params.sched_priority = 0;
      }
      status_ret = pthread_setschedparam(this_thread, set_policy, &params);
      if (status_ret != 0) {
        MY_LOG_WARN("setschedparam(%d)", params.sched_priority);
        go_on = false;
      }
    }

    if (go_on) {
      status_ret = pthread_getschedparam(this_thread, &current_policy, &current_params);
      if (status_ret != 0) {
        MY_LOG_ERROR("getschedparam 2 %d", status_ret);
      } else {
        if (current_params.sched_priority != params.sched_priority) {
          MY_LOG_ERROR("current priority=%d (%d), target is %d", current_params.sched_priority, current_policy,
                       params.sched_priority);
        } else {
          MY_LOG_INFO("set thread priority to %d (%d)", current_params.sched_priority, current_policy);
        }
      }
    }
  }

 public:
  static void *consumer_thread_func_(ConsumerProducer *in_context);

 public:
  /**
   * @brief ConsumerProducer  ructor.
   *        MyJob in high priority queue will be processed first, and will be marked as preferred.
   *        MyJob in low priority queue will be marked as preferred
   *        if the size of low priority queue is less or equal than prefer_queue_size,
   *        others will be marked as not preferred.
   * @param name              Name of the ConsumerProducer
   * @param priority          MyJobPriority of the threads
   * @param worker_num        Number of threads to create
   * @param consume_func      Process function of the threads
   * @param consume_context   Context of the process function
   * @param queue_size        Size of the queue for jobs
   * @param prefer_queue_size Size of the prefered jobs in the low priority queue
   * @param hi_cp_queue_size  Size of the queue for high priority jobs
   * @param cpusetsize        Size of the CPU set
   * @param cpuset            CPU set to bind the threads
   * @param cp_allow_log_     Allow logging
   */
  explicit ConsumerProducer(const ConsumerProducerConfig &in_config, const ConsumeFunc &in_consume_func);

  ~ConsumerProducer() {
    if (cp_allow_log_) {
      if ((!cp_shutdown_.load()) || paused_.load()) {
        MY_LOG_ERROR("%s shutdown=%d paused=%d started=%d", cp_name_.c_str(), cp_shutdown_.load(), paused_.load(),
                     cp_started_.load());
      }
    }
  }

  /**
   * @brief Add job to the queue
   * @param in                MyJob to add_job
   * @param high_priority     True if the job is high priority, otherwise false.
   * @param job_id_out        Buffer to store job id of the added job
   * @param not_discardable   True if the job is not discardable, otherwise false.
   * @return  Return 0: normal enqueue
   *                 1: block enqueue
   *                 2: give up enqueue
   *                 3: discard head and enqueue
   */
  int32_t add_job_do_(std::shared_ptr<Worker> job, bool high_priority, uint64_t *const job_id_out,
                      bool not_discardable);

  /**
   * @brief Create threads and start processing jobs
   */
  void start_process() {
    cp_started_++;
    cp_shutdown_ = false;
    // creates threads
    threads_.reserve(cp_worker_num_);
    for (uint64_t arry_index = 0; arry_index < cp_worker_num_; arry_index++) {
      threads_.emplace_back(consumer_thread_func_, this);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  /**
   * @brief Add job to the queue
   * @param in_job          MyJob to add_job
   * @param high_priority   True if the job is high priority, otherwise false.
   *                        High priority job will be added to high priority queue,
   *                        otherwise low priority queue.
   *                        If the high priority queue is full, the high priority job will wait until
   *                        the queue is not full.
   * @param not_discardable True if the job is not discardable, otherwise false.
   *                        If the low priority queue is full, and not_discardable is true,
   *                        the low priority job will wait until the queue is not full.
   *                        If the low priority queue is full, and not_discardable is false,
   *                        then will comsume the oldest job if possible, otherwise will cosume the
   *                        current job immediately.
   * @return Return 0: normal enqueue
   *                1: block enqueue
   *                2: give up enqueue
   *                3: discard head and enqueue
   */
  inline void add_job(std::shared_ptr<Worker> in_job, bool high_priority = false, bool not_discardable = false);
  /**
   * @brief Add job to the quque and wait until the job is processed
   * @param in_job        MyJob to add_job
   * @param high_priority True if the job is high priority, otherwise false.
   * @return Return 0: normal enqueue
   *                1: block enqueue
   *                2: give up enqueue
   *                3: discard head and enqueue
   */
  int32_t add_job_wait_done(std::shared_ptr<Worker> in_job, bool high_priority = false);
  /**
   * @brief Shutdown the threads and wait until all jobs are stopped
   */
  void shutdown_threads();
  /**
   * @brief Pause adding jobs to the queue, and wait until all jobs are processed or dropped
   */
  void flush_and_pause();
  /**
   * @brief Resume adding jobs to the queue
   */
  void resume();
  /**
   * @brief Get the normal queue length
   * @return Return the normal queue length
   */
  inline uint64_t queue_length() {
    return cp_queue_.cp_queue_queue_length();
  }
  /**
   * @brief Get the normal queue max length
   * @return Return the normal queue max length
   */
  inline uint64_t max_queue_length() {
    return max_queue_length_;
  }
  /**
   * @brief  Get the total number of dropped jobs
   * @return Return the total number of dropped jobs
   */
  inline uint64_t dropped_job_count();
  /**
   * @brief  Get the total number of blocked jobs
   * @return Return the total number of blocked jobs
   */
  inline uint64_t blocked_job_count();
  /**
   * @brief Print current status
   */
  void print_stats(void);
  /**
   * @brief Get current status string
   * @param str_buf       Buffer to store the status string
   */
  void get_stats_string(std::string &output_buffer);
};

/**
 * @brief ConsumerProducer  ructor.
 *        MyJob in high priority queue will be processed first, and will be marked as preferred.
 *        MyJob in low priority queue will be marked as preferred
 *        if the size of low priority queue is less or equal than prefer_queue_size,
 *        others will be marked as not preferred.
 * @param name              Name of the ConsumerProducer
 * @param priority          MyJobPriority of the threads
 * @param worker_num        Number of threads to create
 * @param consume_func      Process function of the threads
 * @param consume_context   Context of the process function
 * @param queue_size        Size of the queue for jobs
 * @param prefer_queue_size Size of the prefered jobs in the low priority queue
 * @param hi_cp_queue_size  Size of the queue for high priority jobs
 * @param cpusetsize        Size of the CPU set
 * @param cpuset            CPU set to bind the threads
 * @param cp_allow_log_        Allow logging
 */
template <typename Worker>
ConsumerProducer<Worker>::ConsumerProducer(const ConsumerProducerConfig &in_config, const ConsumeFunc &in_consume_func)
    : cp_name_(in_config.in_name),
      cp_priority_(in_config.in_priority),
      cp_queue_(in_config.in_name, in_config.in_queue_size, in_config.in_allow_log),
      cp_prefer_queue_size_(in_config.in_prefer_queue_size),
      cp_hi_queue_(in_config.in_name, in_config.in_hi_cp_queue_size, in_config.in_allow_log),
      cp_worker_num_(in_config.in_worker_num),
      cp_consume_func_(in_consume_func),
      cp_started_(0),
      cp_shutdown_(true),
      cp_cpusetsize_(in_config.in_cp_cpu_set_size),
      cp_cpuset_(in_config.in_cp_cpuset),
      cp_allow_log_(in_config.in_allow_log) {
  if (cp_allow_log_) {
    if (cp_name_.empty()) {
      MY_LOG_PANIC("cp_name_ is empty");
    }
  }
  if (cp_prefer_queue_size_ <= 0) {
    cp_prefer_queue_size_ = in_config.in_queue_size;
    blocking_ = true;
  } else {
    blocking_ = false;
  }
  paused_ = false;
  for (uint64_t index_value = 0; index_value < PRIORITY_MAX; index_value++) {
    total_wait_time_[index_value] = 0;
    total_process_time_[index_value] = 0;
    total_drop_time_[index_value] = 0;
    added_job_[index_value] = 0;
    finished_job_[index_value] = 0;
    blocked_job_[index_value] = 0;
    dropped_job_[index_value] = 0;
    cp_job_id_[index_value] = 0;
    finished_job_id_[index_value] = 0;
  }
  max_queue_length_ = 0;
  start_time_ = ConsumerProducer::get_time_ns(CLOCK_MONOTONIC_RAW);
  last_active_time_ = 0;
  last_elapse_time_ = 0;
}

// template <typename Worker>
// ~ConsumerProducer<Worker>::ConsumerProducer() {
//   if (cp_allow_log_) {
//     if (!cp_shutdown_.load() || paused_.load()) {
//       MY_LOG_ERROR("%s shutdown=%d paused=%d started=%d", cp_name_.c_str(), cp_shutdown_.load(), paused_.load(),
//                      cp_started_.load());
//     }
//   }
// }

/**
 * @brief Add job to the queue
 * @param in                MyJob to add_job
 * @param high_priority     True if the job is high priority, otherwise false.
 * @param job_id_out        Buffer to store job id of the added job
 * @param not_discardable   True if the job is not discardable, otherwise false.
 * @return  Return 0: normal enqueue
 *                 1: block enqueue
 *                 2: give up enqueue
 *                 3: discard head and enqueue
 */
template <typename Worker>
int32_t ConsumerProducer<Worker>::add_job_do_(std::shared_ptr<Worker> in_job, bool high_priority,
                                              uint64_t *const job_id_out, bool not_discardable) {
  bool done = false;
  int32_t ret_value = 0;
  MyCpQueue &queue = high_priority ? cp_hi_queue_ : cp_queue_;
  uint32_t arry_idx;
  bool need_blocking = false;
  const int64_t timestamp = ConsumerProducer::get_time_ns(CLOCK_MONOTONIC_RAW);
  uint64_t job_id = 0;

  if (high_priority) {
    arry_idx = 1;
    need_blocking = true;
  } else {
    arry_idx = 0;
    need_blocking = blocking_ || not_discardable;
  }

  while (done != true) {
    std::unique_lock<std::mutex> lck(cp_mutex_);
    if (paused_) {
      cp_not_full_.wait(lck);
      lck.unlock();
    } else if (queue.is_full()) {
      if (need_blocking) {
        ret_value = 1;
        cp_not_full_.wait(lck);
        lck.unlock();
      } else {
        MyJob discard_job;
        (void)queue.peek(discard_job);
        if (discard_job.is_not_discardable()) {
          // cannot drop the one in the queue
          // have to discard itself
          added_job_[arry_idx]++;
          dropped_job_[arry_idx]++;
          lck.unlock();
          cp_consume_func_(in_job, false);
          // don'Worker need to signal cp_job_done_cond_
          // give up enqueue
          ret_value = 2;
          done = true;
        } else {
          (void)queue.pop(discard_job);
          dropped_job_[arry_idx]++;
          update_done_job_id_(arry_idx, discard_job.get_job_id());
          lck.unlock();
          cp_consume_func_(discard_job.get_my_job(), false);
          cp_job_done_cond_.notify_all();
          // discard the head and enqueue
          ret_value = 3;
        }
      }
    } else {
      job_id = assign_job_id_(arry_idx);
      const MyJob job_new(in_job, timestamp, job_id, not_discardable);
      queue.cpq_add_job(job_new);
      const uint32_t cp_queue_len = cp_queue_.cp_queue_queue_length();
      if (max_queue_length_ < cp_queue_len) {
        max_queue_length_ = cp_queue_len;
      }
      added_job_[arry_idx]++;
      lck.unlock();
      cp_not_empty_.notify_all();
      done = true;
    }
  }
  // only update the block case
  if (ret_value == 1) {
    blocked_job_[arry_idx] += 1;
  }

  if (job_id_out != nullptr) {
    *job_id_out = job_id;
  }
  return ret_value;
}

/**
 * @brief Add job to the queue
 * @param in              MyJob to add_job
 * @param high_priority   True if the job is high priority, otherwise false.
 *                        High priority job will be added to high priority queue,
 *                        otherwise low priority queue.
 *                        If the high priority queue is full, the high priority job will wait until
 *                        the queue is not full.
 * @param not_discardable True if the job is not discardable, otherwise false.
 *                        If the low priority queue is full, and not_discardable is true,
 *                        the low priority job will wait until the queue is not full.
 *                        If the low priority queue is full, and not_discardable is false,
 *                        then will comsume the oldest job if possible, otherwise will cosume the
 *                        current job immediately.
 * @return Return 0: normal enqueue
 *                1: block enqueue
 *                2: give up enqueue
 *                3: discard head and enqueue
 */
template <typename Worker>
inline void ConsumerProducer<Worker>::add_job(std::shared_ptr<Worker> in_job, bool high_priority,
                                              bool not_discardable) {
  const int32_t ret_value = add_job_do_(in_job, high_priority, nullptr, not_discardable);
  if (ret_value != 0) {
    // MY_LOG_WARN("%s ret_value = %d", cp_name_.c_str(), ret_value);
  }
}
/**
 * @brief Add job to the quque and wait until the job is processed
 * @param in_job        MyJob to add_job
 * @param high_priority True if the job is high priority, otherwise false.
 * @return Return 0: normal enqueue
 *                1: block enqueue
 *                2: give up enqueue
 *                3: discard head and enqueue
 */
template <typename Worker>
int32_t ConsumerProducer<Worker>::add_job_wait_done(std::shared_ptr<Worker> in_job, bool high_priority) {
  uint64_t job_id;
  if (cp_allow_log_) {
    if (cp_worker_num_ > 1) {
      MY_LOG_PANIC("%s add_job_until_done only support 1 worker", cp_name_.c_str());
    }
  }
  const int32_t ret_value = add_job_do_(in_job, high_priority, &job_id, true);
  std::unique_lock<std::mutex> lck(cp_mutex_);
  const uint32_t arry_idx = high_priority ? 1U : 0U;
  while (job_id > finished_job_id_[arry_idx]) {
    cp_job_done_cond_.wait(lck);
  }
  return ret_value;
}

/**
 * @brief Pause adding jobs to the queue, and wait until all jobs are processed or dropped
 */
template <typename Worker>
void ConsumerProducer<Worker>::flush_and_pause() {
  std::unique_lock<std::mutex> lck(cp_mutex_);
  if (cp_allow_log_) {
    if (paused_) {
      MY_LOG_PANIC("%s paused_ = %d", cp_name_.c_str(), paused_);
    }
  }
  paused_ = true;
  while (added_job_total_() != (finished_job_total_() + dropped_job_total_())) {
    if (cp_allow_log_) {
      if (added_job_total_() < (finished_job_total_() + dropped_job_total_())) {
        MY_LOG_PANIC("%s added_job_total_() = %lu finished_job_total_() = %lu dropped_job_total_() = %lu",
                     cp_name_.c_str(), added_job_total_(), finished_job_total_(), dropped_job_total_());
      }
    }
    cp_job_done_cond_.wait(lck);
  }
}
/**
 * @brief Resume adding jobs to the queue
 */
template <typename Worker>
void ConsumerProducer<Worker>::resume() {
  if (cp_allow_log_ == true) {
    if (!paused_) {
      MY_LOG_PANIC("%s paused_ = %d", cp_name_.c_str(), paused_);
    }
  }
  paused_ = false;
  // wake up producer
  cp_not_full_.notify_all();
}

/**
 * @brief  Get the total number of dropped jobs
 * @return Return the total number of dropped jobs
 */
template <typename Worker>
inline uint64_t ConsumerProducer<Worker>::dropped_job_count() {
  uint64_t ret_value = 0;
  uint64_t count = 0;
  for (count = 0; count < PRIORITY_MAX; count++) {
    count += dropped_job_[count];
  }
  return count;
}
/**
 * @brief  Get the total number of blocked jobs
 * @return Return the total number of blocked jobs
 */
template <typename Worker>
inline uint64_t ConsumerProducer<Worker>::blocked_job_count() {
  uint64_t ret_value = 0;
  for (uint32_t idx = 0; idx < PRIORITY_MAX; idx++) {
    ret_value += blocked_job_[idx];
  }
  return ret_value;
}

/**
 * @brief Get current status string
 * @param output_buffer       Buffer to store the status string
 */
template <typename Worker>
void ConsumerProducer<Worker>::get_stats_string(std::string &output_buffer) {
  int64_t active_time = 0;
  for (uint32_t arry_index = 0; arry_index < PRIORITY_MAX; arry_index++) {
    if (added_job_[arry_index] == 0) {
      continue;
    }
    output_buffer =
        output_buffer + cp_name_ + " queue#" + std::to_string(arry_index) +
        " added=" + std::to_string(added_job_[arry_index]) + " finished=" + std::to_string(finished_job_[arry_index]) +
        " dropped=" + std::to_string(dropped_job_[arry_index]) +
        " blocked=" + std::to_string(blocked_job_[arry_index]) + " wait=" +
        std::to_string((finished_job_[arry_index] + dropped_job_[arry_index]) > 0
                           ? total_wait_time_[arry_index] / (finished_job_[arry_index] + dropped_job_[arry_index]) /
                                 1000
                           : 0) +
        "us process=" +
        std::to_string(
            finished_job_[arry_index] > 0 ? total_process_time_[arry_index] / finished_job_[arry_index] / 1000 : 0) +
        "us drop=" +
        std::to_string(total_drop_time_[arry_index] > 0 ? total_drop_time_[arry_index] / dropped_job_[arry_index] / 1000
                                                        : 0) +
        "us pid=" + std::to_string(cp_pid_);
    active_time += total_process_time_[arry_index] + total_drop_time_[arry_index];
  }

  const int64_t end_time = ConsumerProducer::get_time_ns(CLOCK_MONOTONIC_RAW);
  const int64_t elapse_time = end_time - start_time_;

  if ((elapse_time > 0) && (active_time > 0)) {
    const int64_t elapse_delta = elapse_time - last_elapse_time_;
    const int64_t active_delta = active_time - last_active_time_;
    double ratio = 0.0;
    if ((elapse_delta > 0) && (active_delta > 0)) {
      ratio = static_cast<double>(active_delta) * 100.0 / static_cast<double>(elapse_delta);
    }
    output_buffer = output_buffer + "elapsed_time=" + std::to_string(elapse_delta / 1000000) + "/" +
                    std::to_string((end_time - start_time_) / 1000000) + "ms " +
                    "active_time=" + std::to_string(active_delta / 1000000) + "/" +
                    std::to_string(active_time / 1000000) + "ms " +
                    "ratio=" + std::to_string(static_cast<int32_t>(ratio * 100)) + "%/" +
                    std::to_string(static_cast<int32_t>((active_time * 100.0) / (end_time - start_time_))) + "%";

    if ((elapse_delta > 0) && (active_delta > 0)) {
      last_elapse_time_ = elapse_time;
      last_active_time_ = active_time;
    }
  }
}

/**
 * @brief Print current status
 */
template <typename Worker>
void ConsumerProducer<Worker>::print_stats(void) {
  std::string str_buf;
  get_stats_string(str_buf);
  MY_LOG_INFO("%s", str_buf.c_str());
}

/**
 * @brief Shutdown the threads and wait until all jobs are stopped
 */
template <typename Worker>
void ConsumerProducer<Worker>::shutdown_threads() {
  print_stats();
  if (cp_allow_log_) {
    if (cp_shutdown_.load()) {
      MY_LOG_PANIC("%s shutdown=%d started=%d", cp_name_.c_str(), cp_shutdown_.load(), cp_started_.load());
    }
  }
  cp_shutdown_ = true;
  cp_not_empty_.notify_all();
  cp_not_full_.notify_all();
  // join threads
  for (std::thread &thread_iterator : threads_) {
    if (thread_iterator.joinable()) {
      thread_iterator.join();
    }
  }
}

template <typename Worker>
void *ConsumerProducer<Worker>::consumer_thread_func_(ConsumerProducer *in_context) {
  if (in_context != nullptr) {
    if ((in_context->cp_cpuset_ != nullptr) && (in_context->cp_cpusetsize_ > 0)) {
      (void)pthread_setaffinity_np(pthread_self(), static_cast<uint64_t>(in_context->cp_cpusetsize_),
                                   in_context->cp_cpuset_);
    }
    in_context->cp_pid_ = static_cast<pid_t>(syscall(SYS_gettid));
    MY_LOG_INFO("thread %s starts. pid=%d target_priority=%d", in_context->cp_name_.c_str(), in_context->cp_pid_,
                in_context->cp_priority_);
    ConsumerProducer::set_self_thread_priority(in_context->cp_priority_);

    while (true) {
      MyJobPriority job_priority;
      MyJob job_task;
      const uint32_t queue_size = in_context->get_job_(job_task, job_priority);
      const int64_t timestamp_poped = ConsumerProducer::get_time_ns(CLOCK_MONOTONIC_RAW);
      bool is_prefered;
      uint64_t arry_idx = 0;
      if (queue_size == 0) {
        break;
      }
      if (job_priority == PRIORITY_LOW) {
        is_prefered = (queue_size <= in_context->cp_prefer_queue_size_);
        arry_idx = 0;
      } else {
        // high priority queue
        is_prefered = true;
        arry_idx = 1;
      }
      in_context->cp_consume_func_(job_task.get_my_job(), is_prefered);
      const auto timestamp_done = ConsumerProducer::get_time_ns(CLOCK_MONOTONIC_RAW);
      std::unique_lock<std::mutex> lck(in_context->cp_mutex_);
      in_context->total_wait_time_[job_priority] += (timestamp_poped - job_task.get_timestamp());
      if (is_prefered) {
        in_context->finished_job_[job_priority]++;
        in_context->total_process_time_[job_priority] += (timestamp_done - timestamp_poped);
      } else {
        in_context->dropped_job_[job_priority]++;
        in_context->total_drop_time_[job_priority] += (timestamp_done - timestamp_poped);
      }
      in_context->update_done_job_id_(arry_idx, job_task.get_job_id());
      lck.unlock();
      in_context->cp_job_done_cond_.notify_all();
      if (in_context->cp_priority_ > 0) {
        std::this_thread::yield();
      }
    }
  }
  return nullptr;
}

}  // namespace MyTest

#endif  // UTILS_CONSUMER_PRODUCER_H_
