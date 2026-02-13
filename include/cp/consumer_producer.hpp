#ifndef CP_CONSUMER_PRODUCER_HPP
#define CP_CONSUMER_PRODUCER_HPP

#ifdef __linux__
#include <sched.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#endif

#include <cstdint>
#include <cstdio>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <string>
#include <thread>
#include <vector>

namespace cp {

/// @brief Configuration for ConsumerProducer.
struct Config {
  std::string name;
  int32_t priority{0};
  uint32_t worker_num{1};
  uint32_t queue_size{16};
  uint32_t prefer_queue_size{0};  ///< 0 = all preferred (blocking mode)
  uint32_t hi_queue_size{4};
#ifdef __linux__
  uint32_t cpu_set_size{0};
  const cpu_set_t* cpu_set{nullptr};
#endif
};

/// @brief Multi-threaded consumer-producer with dual-priority queues.
///
/// High-priority jobs are processed first and always marked preferred.
/// Low-priority jobs are preferred when queue length <= prefer_queue_size.
///
/// Performance optimizations:
/// - Time statistics use std::atomic (moved out of mutex critical section)
/// - blocked_job_ / max_queue_length_ use std::atomic (lock-free reads)
/// - job_done_cv_ uses notify_one when not paused (reduced thundering herd)
///
/// @tparam Worker  Job payload type (used via shared_ptr<Worker>).
template <typename Worker>
class ConsumerProducer {
 public:
  using ConsumeFunc = std::function<int32_t(std::shared_ptr<Worker>, bool)>;

  /// @brief Enqueue result codes.
  enum class EnqueueResult : int32_t {
    kNormal = 0,       ///< Normal enqueue
    kBlocked = 1,      ///< Blocked then enqueued
    kDiscardSelf = 2,  ///< Queue full, self discarded
    kDiscardHead = 3,  ///< Queue full, head discarded then enqueued
  };

  explicit ConsumerProducer(const Config& cfg, ConsumeFunc func);
  ~ConsumerProducer();

  // Non-copyable, non-movable
  ConsumerProducer(const ConsumerProducer&) = delete;
  ConsumerProducer& operator=(const ConsumerProducer&) = delete;

  /// @brief Start worker threads.
  void Start();

  /// @brief Shutdown all worker threads (waits for completion).
  void Shutdown();

  /// @brief Add a job to the queue.
  /// @param job            Job payload.
  /// @param high_priority  If true, enqueue to high-priority queue.
  /// @param not_discardable If true, block instead of discarding when full.
  /// @return Enqueue result code.
  EnqueueResult AddJob(std::shared_ptr<Worker> job, bool high_priority = false, bool not_discardable = false);

  /// @brief Add a job and block until it is processed.
  /// @return Enqueue result code.
  EnqueueResult AddJobWaitDone(std::shared_ptr<Worker> job, bool high_priority = false);

  /// @brief Flush all pending jobs and pause accepting new ones.
  void FlushAndPause();

  /// @brief Resume accepting jobs after pause.
  void Resume();

  /// @brief Get current low-priority queue length.
  uint32_t QueueLength() const;

  /// @brief Get peak queue length observed (lock-free).
  uint32_t MaxQueueLength() const;

  /// @brief Get total dropped job count.
  uint64_t DroppedJobCount() const;

  /// @brief Get total blocked job count (lock-free).
  uint64_t BlockedJobCount() const;

  /// @brief Get stats as a formatted string.
  std::string GetStatsString() const;

  /// @brief Print stats to stdout.
  void PrintStats() const;

 private:
  enum Priority : uint32_t {
    kLow = 0,
    kHigh = 1,
    kMax = 2,
  };

  struct Job {
    std::shared_ptr<Worker> payload;
    int64_t timestamp{0};
    uint64_t id{0};
    bool not_discardable{false};
  };

  /// @brief Simple circular queue (not thread-safe, protected by external mutex).
  class CircularQueue {
   public:
    explicit CircularQueue(uint32_t capacity) : capacity_(capacity), jobs_(capacity) {}

    bool IsEmpty() const { return count_ == 0; }
    bool IsFull() const { return count_ == capacity_; }
    uint32_t Count() const { return count_; }

    void Push(const Job& job) {
      jobs_[tail_] = job;
      tail_ = (tail_ + 1) % capacity_;
      ++count_;
    }

    bool Pop(Job& out) {
      if (count_ == 0)
        return false;
      out = jobs_[head_];
      head_ = (head_ + 1) % capacity_;
      --count_;
      return true;
    }

    bool Peek(Job& out) const {
      if (count_ == 0)
        return false;
      out = jobs_[head_];
      return true;
    }

   private:
    uint32_t capacity_;
    uint32_t head_{0};
    uint32_t tail_{0};
    uint32_t count_{0};
    std::vector<Job> jobs_;
  };

  static int64_t NowNs() {
#ifdef __linux__
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
#else
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
#endif
  }

  static void SetThreadPriority(int32_t priority);
  void ConsumerLoop();
  uint32_t GetJob(Job& out, Priority& prio);
  EnqueueResult EnqueueImpl(std::shared_ptr<Worker> job, bool high_priority, uint64_t* job_id_out,
                            bool not_discardable);

  // Config
  std::string name_;
  int32_t priority_;
  uint32_t worker_num_;
  uint32_t prefer_queue_size_;
#ifdef __linux__
  uint32_t cpu_set_size_;
  const cpu_set_t* cpu_set_{nullptr};
#endif
  bool blocking_mode_;

  // Queues (protected by mutex_)
  CircularQueue lo_queue_;
  CircularQueue hi_queue_;

  // Threading
  std::vector<std::thread> threads_;
  ConsumeFunc consume_func_;
  mutable std::mutex mutex_;
  std::condition_variable not_full_cv_;
  std::condition_variable not_empty_cv_;
  std::condition_variable job_done_cv_;
  std::atomic<bool> shutdown_{true};
  std::atomic<bool> paused_{false};
  bool started_{false};

  // Statistics under mutex (condition variable predicates depend on these)
  int64_t start_time_{0};
  uint64_t added_job_[kMax]{};
  uint64_t finished_job_[kMax]{};
  uint64_t dropped_job_[kMax]{};
  uint32_t job_id_[kMax]{};
  uint64_t finished_job_id_[kMax]{};
  // Statistics atomic (lock-free read/write, not part of CV predicates)
  std::atomic<int32_t> pid_{0};
  std::atomic<int64_t> total_wait_time_[kMax];
  std::atomic<int64_t> total_process_time_[kMax];
  std::atomic<int64_t> total_drop_time_[kMax];
  std::atomic<uint64_t> blocked_job_[kMax];
  std::atomic<uint32_t> max_queue_length_{0};
};

// ============================================================================
// Template Implementation
// ============================================================================

template <typename Worker>
ConsumerProducer<Worker>::ConsumerProducer(const Config& cfg, ConsumeFunc func)
    : name_(cfg.name),
      priority_(cfg.priority),
      worker_num_(cfg.worker_num),
      prefer_queue_size_(cfg.prefer_queue_size),
#ifdef __linux__
      cpu_set_size_(cfg.cpu_set_size),
      cpu_set_(cfg.cpu_set),
#endif
      blocking_mode_(cfg.prefer_queue_size == 0),
      lo_queue_(cfg.queue_size),
      hi_queue_(cfg.hi_queue_size),
      consume_func_(std::move(func)),
      start_time_(NowNs()) {
  for (uint32_t i = 0; i < kMax; ++i) {
    total_wait_time_[i].store(0, std::memory_order_relaxed);
    total_process_time_[i].store(0, std::memory_order_relaxed);
    total_drop_time_[i].store(0, std::memory_order_relaxed);
    blocked_job_[i].store(0, std::memory_order_relaxed);
  }
}

template <typename Worker>
ConsumerProducer<Worker>::~ConsumerProducer() {
  if (!shutdown_.load(std::memory_order_relaxed)) {
    Shutdown();
  }
}

template <typename Worker>
void ConsumerProducer<Worker>::Start() {
  shutdown_.store(false, std::memory_order_relaxed);
  started_ = true;
  threads_.reserve(worker_num_);
  for (uint32_t i = 0; i < worker_num_; ++i) {
    threads_.emplace_back(&ConsumerProducer::ConsumerLoop, this);
  }
}

template <typename Worker>
void ConsumerProducer<Worker>::Shutdown() {
  shutdown_.store(true, std::memory_order_relaxed);
  not_empty_cv_.notify_all();
  not_full_cv_.notify_all();
  for (auto& t : threads_) {
    if (t.joinable())
      t.join();
  }
  threads_.clear();
}

template <typename Worker>
typename ConsumerProducer<Worker>::EnqueueResult ConsumerProducer<Worker>::AddJob(std::shared_ptr<Worker> job,
                                                                                  bool high_priority,
                                                                                  bool not_discardable) {
  return EnqueueImpl(std::move(job), high_priority, nullptr, not_discardable);
}

template <typename Worker>
typename ConsumerProducer<Worker>::EnqueueResult ConsumerProducer<Worker>::AddJobWaitDone(std::shared_ptr<Worker> job,
                                                                                          bool high_priority) {
  uint64_t job_id = 0;
  auto result = EnqueueImpl(std::move(job), high_priority, &job_id, true);
  const uint32_t idx = high_priority ? kHigh : kLow;
  std::unique_lock<std::mutex> lk(mutex_);
  job_done_cv_.wait(lk, [&] { return job_id <= finished_job_id_[idx] || shutdown_.load(); });
  return result;
}

template <typename Worker>
typename ConsumerProducer<Worker>::EnqueueResult ConsumerProducer<Worker>::EnqueueImpl(std::shared_ptr<Worker> job,
                                                                                       bool high_priority,
                                                                                       uint64_t* job_id_out,
                                                                                       bool not_discardable) {
  const uint32_t idx = high_priority ? kHigh : kLow;
  const bool need_blocking = high_priority || blocking_mode_ || not_discardable;
  const int64_t timestamp = NowNs();
  uint64_t job_id = 0;
  auto result = EnqueueResult::kNormal;

  while (true) {
    std::unique_lock<std::mutex> lk(mutex_);

    // Paused -- wait for resume
    if (paused_.load(std::memory_order_relaxed)) {
      not_full_cv_.wait(lk, [&] { return !paused_.load(std::memory_order_relaxed) || shutdown_.load(); });
      if (shutdown_.load())
        break;
      continue;
    }

    auto& queue = high_priority ? hi_queue_ : lo_queue_;

    if (!queue.IsFull()) {
      // Normal enqueue
      job_id = ++job_id_[idx];
      queue.Push({job, timestamp, job_id, not_discardable});
      const uint32_t len = lo_queue_.Count();
      uint32_t prev = max_queue_length_.load(std::memory_order_relaxed);
      if (len > prev) {
        max_queue_length_.store(len, std::memory_order_relaxed);
      }
      added_job_[idx]++;
      lk.unlock();
      not_empty_cv_.notify_one();
      break;
    }

    // Queue is full
    if (need_blocking) {
      result = EnqueueResult::kBlocked;
      not_full_cv_.wait(lk, [&] { return !queue.IsFull() || shutdown_.load(); });
      if (shutdown_.load())
        break;
      continue;  // retry after wakeup
    }

    // Try to discard head
    Job head_job;
    if (queue.Peek(head_job) && !head_job.not_discardable) {
      queue.Pop(head_job);
      dropped_job_[idx]++;
      finished_job_id_[idx] = head_job.id;
      lk.unlock();
      consume_func_(head_job.payload, false);
      job_done_cv_.notify_all();
      result = EnqueueResult::kDiscardHead;
      continue;  // retry -- now there's space
    }

    // Cannot discard head -- discard self
    added_job_[idx]++;
    dropped_job_[idx]++;
    lk.unlock();
    consume_func_(job, false);
    result = EnqueueResult::kDiscardSelf;
    break;
  }

  if (result == EnqueueResult::kBlocked) {
    blocked_job_[idx].fetch_add(1, std::memory_order_relaxed);
  }

  if (job_id_out != nullptr) {
    *job_id_out = job_id;
  }
  return result;
}

template <typename Worker>
uint32_t ConsumerProducer<Worker>::GetJob(Job& out, Priority& prio) {
  while (true) {
    std::unique_lock<std::mutex> lk(mutex_);
    not_empty_cv_.wait(lk, [&] { return !hi_queue_.IsEmpty() || !lo_queue_.IsEmpty() || shutdown_.load(); });

    // Try high-priority first
    if (hi_queue_.Pop(out)) {
      prio = kHigh;
      uint32_t count = hi_queue_.Count() + 1;
      lk.unlock();
      not_full_cv_.notify_one();
      return count;
    }

    // Then low-priority
    if (lo_queue_.Pop(out)) {
      prio = kLow;
      uint32_t count = lo_queue_.Count() + 1;
      lk.unlock();
      not_full_cv_.notify_one();
      return count;
    }

    lk.unlock();
    if (shutdown_.load()) {
      not_empty_cv_.notify_all();
      return 0;
    }
  }
}

template <typename Worker>
void ConsumerProducer<Worker>::ConsumerLoop() {
#ifdef __linux__
  if (cpu_set_ != nullptr && cpu_set_size_ > 0) {
    pthread_setaffinity_np(pthread_self(), cpu_set_size_, cpu_set_);
  }
  pid_.store(static_cast<int32_t>(syscall(SYS_gettid)), std::memory_order_relaxed);
#endif
  SetThreadPriority(priority_);

  while (true) {
    Priority prio;
    Job job;
    const uint32_t queue_size = GetJob(job, prio);
    if (queue_size == 0)
      break;

    const int64_t ts_popped = NowNs();
    const bool is_preferred = (prio == kHigh) || (queue_size <= prefer_queue_size_);

    consume_func_(job.payload, is_preferred);

    const int64_t ts_done = NowNs();

    // Atomic time stats (outside lock -- reduces critical section)
    total_wait_time_[prio].fetch_add(ts_popped - job.timestamp, std::memory_order_relaxed);
    if (is_preferred) {
      total_process_time_[prio].fetch_add(ts_done - ts_popped, std::memory_order_relaxed);
    } else {
      total_drop_time_[prio].fetch_add(ts_done - ts_popped, std::memory_order_relaxed);
    }

    // Counters under lock (condition variable correctness for FlushAndPause/AddJobWaitDone)
    {
      std::lock_guard<std::mutex> lk(mutex_);
      if (is_preferred) {
        finished_job_[prio]++;
      } else {
        dropped_job_[prio]++;
      }
      finished_job_id_[prio] = job.id;
    }

    // notify_one normally; notify_all when paused (FlushAndPause may be waiting)
    if (paused_.load(std::memory_order_relaxed)) {
      job_done_cv_.notify_all();
    } else {
      job_done_cv_.notify_one();
    }

    if (priority_ > 0) {
      std::this_thread::yield();
    }
  }
}

template <typename Worker>
void ConsumerProducer<Worker>::FlushAndPause() {
  std::unique_lock<std::mutex> lk(mutex_);
  paused_.store(true, std::memory_order_relaxed);
  auto total_added = [&]() -> uint64_t {
    uint64_t s = 0;
    for (uint32_t i = 0; i < kMax; ++i)
      s += added_job_[i];
    return s;
  };
  auto total_done = [&]() -> uint64_t {
    uint64_t s = 0;
    for (uint32_t i = 0; i < kMax; ++i)
      s += finished_job_[i] + dropped_job_[i];
    return s;
  };
  job_done_cv_.wait(lk, [&] { return total_added() == total_done() || shutdown_.load(); });
}

template <typename Worker>
void ConsumerProducer<Worker>::Resume() {
  paused_.store(false, std::memory_order_relaxed);
  not_full_cv_.notify_all();
}

template <typename Worker>
uint32_t ConsumerProducer<Worker>::QueueLength() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return lo_queue_.Count();
}

template <typename Worker>
uint32_t ConsumerProducer<Worker>::MaxQueueLength() const {
  return max_queue_length_.load(std::memory_order_relaxed);
}

template <typename Worker>
uint64_t ConsumerProducer<Worker>::DroppedJobCount() const {
  std::lock_guard<std::mutex> lk(mutex_);
  uint64_t total = 0;
  for (uint32_t i = 0; i < kMax; ++i)
    total += dropped_job_[i];
  return total;
}

template <typename Worker>
uint64_t ConsumerProducer<Worker>::BlockedJobCount() const {
  uint64_t total = 0;
  for (uint32_t i = 0; i < kMax; ++i) {
    total += blocked_job_[i].load(std::memory_order_relaxed);
  }
  return total;
}

template <typename Worker>
std::string ConsumerProducer<Worker>::GetStatsString() const {
  std::lock_guard<std::mutex> lk(mutex_);
  std::string out;
  char buf[512];
  for (uint32_t i = 0; i < kMax; ++i) {
    if (added_job_[i] == 0)
      continue;
    const uint64_t done = finished_job_[i] + dropped_job_[i];
    const int64_t wait_time = total_wait_time_[i].load(std::memory_order_relaxed);
    const int64_t proc_time = total_process_time_[i].load(std::memory_order_relaxed);
    const int64_t drop_time = total_drop_time_[i].load(std::memory_order_relaxed);
    const uint64_t blocked = blocked_job_[i].load(std::memory_order_relaxed);
    const int64_t avg_wait = done > 0 ? wait_time / static_cast<int64_t>(done) / 1000 : 0;
    const int64_t avg_proc = finished_job_[i] > 0 ? proc_time / static_cast<int64_t>(finished_job_[i]) / 1000 : 0;
    const int64_t avg_drop = dropped_job_[i] > 0 ? drop_time / static_cast<int64_t>(dropped_job_[i]) / 1000 : 0;
    std::snprintf(buf, sizeof(buf),
                  "%s q#%u added=%lu finished=%lu dropped=%lu blocked=%lu "
                  "wait=%ldus proc=%ldus drop=%ldus pid=%d\n",
                  name_.c_str(), i, static_cast<unsigned long>(added_job_[i]),
                  static_cast<unsigned long>(finished_job_[i]), static_cast<unsigned long>(dropped_job_[i]),
                  static_cast<unsigned long>(blocked), static_cast<long>(avg_wait), static_cast<long>(avg_proc),
                  static_cast<long>(avg_drop), pid_.load(std::memory_order_relaxed));
    out += buf;
  }
  return out;
}

template <typename Worker>
void ConsumerProducer<Worker>::PrintStats() const {
  std::string s = GetStatsString();
  if (!s.empty()) {
    std::fprintf(stdout, "%s", s.c_str());
  }
}

template <typename Worker>
void ConsumerProducer<Worker>::SetThreadPriority(int32_t priority) {
  if (priority == 0)
    return;

  const pthread_t self = pthread_self();
  struct sched_param params{};
  int policy = 0;
  if (pthread_getschedparam(self, &policy, &params) != 0)
    return;

  if (priority > 0) {
    policy = SCHED_FIFO;
    params.sched_priority = params.sched_priority + priority;
    if (params.sched_priority > 99)
      params.sched_priority = 99;
  } else {
#ifdef __linux__
    policy = SCHED_IDLE;
    params.sched_priority = 0;
#else
    return;  // SCHED_IDLE not available on non-Linux
#endif
  }
  pthread_setschedparam(self, policy, &params);
}

}  // namespace cp

#endif  // CP_CONSUMER_PRODUCER_HPP
