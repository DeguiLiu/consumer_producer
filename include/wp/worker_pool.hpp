/**
 * MIT License
 *
 * Copyright (c) 2024 liudegui
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file worker_pool.hpp
 * @brief WorkerPool - Multi-worker thread pool built on mccc AsyncBus.
 *
 * Architecture:
 *   Submit() -> mccc::AsyncBus::Publish() (lock-free MPSC)
 *                    |
 *              DispatcherThread (ProcessBatch loop)
 *                    | round-robin
 *              Worker[0..N-1] SPSC Queue -> WorkerThread -> Handler
 *
 * Features:
 * - Lock-free ingress via mccc MPSC ring buffer
 * - Lock-free SPSC per-worker queues for dispatch
 * - Function pointer handlers (no std::function in worker hot path)
 * - Priority-based admission control (mccc MessagePriority)
 * - FlushAndPause / Resume for graceful draining
 * - Synchronous execution mode (SubmitSync)
 * - Thread priority and CPU affinity support (Linux)
 * - -fno-exceptions -fno-rtti compatible
 *
 * @tparam PayloadVariant A std::variant<...> of user-defined message types.
 *
 * Usage:
 *   struct TaskA { int id; };
 *   struct TaskB { float value; };
 *   using MyPayload = std::variant<TaskA, TaskB>;
 *
 *   wp::Config cfg;
 *   cfg.name = "demo";
 *   cfg.worker_num = 4;
 *
 *   wp::WorkerPool<MyPayload> pool(cfg);
 *   pool.RegisterHandler<TaskA>([](const TaskA& t, const mccc::MessageHeader&) { ... });
 *   pool.Start();
 *   pool.Submit(TaskA{1});
 *   pool.Shutdown();
 */

#ifndef WP_WORKER_POOL_HPP_
#define WP_WORKER_POOL_HPP_

#include <cstdint>
#include <cstring>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mccc/mccc.hpp>
#include <memory>
#include <mutex>
#include <thread>
#include <variant>
#include <vector>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

namespace wp {

// ============================================================================
// Configuration
// ============================================================================

static constexpr uint32_t kCacheLineSize = 64U;
static constexpr uint32_t kDefaultWorkerQueueDepth = 1024U;

/**
 * @brief WorkerPool configuration.
 */
struct Config {
  mccc::FixedString<32> name{"pool"};
  uint32_t worker_num{1U};
  uint32_t worker_queue_depth{kDefaultWorkerQueueDepth};
  int32_t priority{0};
#ifdef __linux__
  uint32_t cpu_set_size{0U};
  const cpu_set_t* cpu_set{nullptr};
#endif
};

// ============================================================================
// SpscQueue - Lock-free single-producer single-consumer ring buffer
// ============================================================================

/**
 * @brief Lock-free SPSC ring buffer with cache-line-aligned counters.
 *
 * Capacity is rounded up to the next power of 2 for mask-based indexing.
 * Single producer (dispatcher thread) and single consumer (worker thread).
 *
 * @tparam T Element type (must be movable)
 */
template <typename T>
class SpscQueue {
 public:
  explicit SpscQueue(uint32_t min_capacity) noexcept
      : capacity_(NextPowerOf2(min_capacity)), mask_(capacity_ - 1U), buffer_(capacity_) {}

  SpscQueue() noexcept : SpscQueue(kDefaultWorkerQueueDepth) {}

  SpscQueue(const SpscQueue&) = delete;
  SpscQueue& operator=(const SpscQueue&) = delete;
  SpscQueue(SpscQueue&&) = delete;
  SpscQueue& operator=(SpscQueue&&) = delete;

  /**
   * @brief Try to push an element (producer side).
   * @return true if pushed, false if queue is full.
   */
  bool TryPush(const T& item) noexcept {
    const uint32_t wp = write_pos_.load(std::memory_order_relaxed);
    const uint32_t rp = read_pos_.load(std::memory_order_acquire);
    if (wp - rp >= capacity_) {
      return false;
    }
    buffer_[wp & mask_] = item;
    write_pos_.store(wp + 1U, std::memory_order_release);
    return true;
  }

  /**
   * @brief Try to pop an element (consumer side).
   * @return true if popped, false if queue is empty.
   */
  bool TryPop(T& item) noexcept {
    const uint32_t rp = read_pos_.load(std::memory_order_relaxed);
    const uint32_t wp = write_pos_.load(std::memory_order_acquire);
    if (rp == wp) {
      return false;
    }
    item = std::move(buffer_[rp & mask_]);
    read_pos_.store(rp + 1U, std::memory_order_release);
    return true;
  }

  bool Empty() const noexcept {
    return read_pos_.load(std::memory_order_acquire) == write_pos_.load(std::memory_order_acquire);
  }

  uint32_t Size() const noexcept {
    return write_pos_.load(std::memory_order_acquire) - read_pos_.load(std::memory_order_acquire);
  }

  uint32_t Capacity() const noexcept { return capacity_; }

 private:
  static uint32_t NextPowerOf2(uint32_t v) noexcept {
    if (v == 0U) {
      return 1U;
    }
    --v;
    v |= v >> 1U;
    v |= v >> 2U;
    v |= v >> 4U;
    v |= v >> 8U;
    v |= v >> 16U;
    return v + 1U;
  }

  const uint32_t capacity_;
  const uint32_t mask_;
  alignas(kCacheLineSize) std::atomic<uint32_t> write_pos_{0U};
  alignas(kCacheLineSize) std::atomic<uint32_t> read_pos_{0U};
  std::vector<T> buffer_;
};

// ============================================================================
// WorkerPool Statistics
// ============================================================================

struct Stats {
  uint64_t dispatched{0U};
  uint64_t processed{0U};
  uint64_t worker_queue_full{0U};
  mccc::BusStatisticsSnapshot bus_stats{};
};

// ============================================================================
// WorkerPool
// ============================================================================

/**
 * @brief Multi-worker thread pool built on mccc::AsyncBus.
 *
 * @tparam PayloadVariant A std::variant<...> of user-defined message types.
 */
template <typename PayloadVariant>
class WorkerPool {
 public:
  using BusType = mccc::AsyncBus<PayloadVariant>;
  using EnvelopeType = mccc::MessageEnvelope<PayloadVariant>;

  static constexpr uint32_t kMaxTypes = std::variant_size_v<PayloadVariant>;

  explicit WorkerPool(const Config& cfg) noexcept
      : name_(cfg.name),
        worker_num_(cfg.worker_num > 0U ? cfg.worker_num : 1U),
        priority_(cfg.priority),
#ifdef __linux__
        cpu_set_size_(cfg.cpu_set_size),
        cpu_set_(cfg.cpu_set),
#endif
        worker_queue_depth_(cfg.worker_queue_depth > 0U ? cfg.worker_queue_depth : kDefaultWorkerQueueDepth) {
    dispatch_funcs_.fill(nullptr);
    handler_ptrs_.fill(nullptr);
  }

  ~WorkerPool() noexcept {
    if (running_.load(std::memory_order_acquire)) {
      Shutdown();
    }
  }

  WorkerPool(const WorkerPool&) = delete;
  WorkerPool& operator=(const WorkerPool&) = delete;
  WorkerPool(WorkerPool&&) = delete;
  WorkerPool& operator=(WorkerPool&&) = delete;

  // ======================== Handler Registration ========================

  /**
   * @brief Register a handler for message type T.
   *
   * Must be called before Start(). Handler is invoked in worker threads.
   *
   * @tparam T Message type (must be in PayloadVariant)
   * @param handler Function pointer: void(const T&, const MessageHeader&)
   */
  template <typename T>
  void RegisterHandler(void (*handler)(const T&, const mccc::MessageHeader&)) noexcept {
    constexpr size_t idx = mccc::VariantIndex<T, PayloadVariant>::value;
    static_assert(idx < kMaxTypes, "Type not in PayloadVariant");
    static_assert(sizeof(void*) >= sizeof(handler), "Function pointer must fit in void*");
    dispatch_funcs_[idx] = &TypedDispatch<T>;
    void* ptr = nullptr;
    std::memcpy(&ptr, &handler, sizeof(handler));
    handler_ptrs_[idx] = ptr;
  }

  // ======================== Lifecycle ========================

  /**
   * @brief Start dispatcher and worker threads.
   *
   * Subscribes to all registered message types on the mccc bus,
   * then starts the dispatcher thread and N worker threads.
   */
  void Start() noexcept {
    if (running_.load(std::memory_order_acquire)) {
      return;
    }
    running_.store(true, std::memory_order_release);
    shutdown_.store(false, std::memory_order_release);

    workers_.reserve(worker_num_);
    for (uint32_t i = 0U; i < worker_num_; ++i) {
      workers_.push_back(std::unique_ptr<WorkerContext>(new WorkerContext(worker_queue_depth_)));
    }

    SubscribeAll(static_cast<PayloadVariant*>(nullptr));

    dispatcher_thread_ = std::thread(&WorkerPool::DispatcherLoop, this);

    for (uint32_t i = 0U; i < worker_num_; ++i) {
      worker_threads_.emplace_back(&WorkerPool::WorkerLoop, this, i);
    }
  }

  /**
   * @brief Shutdown: stop accepting jobs, drain queues, join all threads.
   */
  void Shutdown() noexcept {
    if (!running_.load(std::memory_order_acquire)) {
      return;
    }
    shutdown_.store(true, std::memory_order_release);

    if (dispatcher_thread_.joinable()) {
      dispatcher_thread_.join();
    }

    for (uint32_t i = 0U; i < worker_num_; ++i) {
      {
        std::lock_guard<std::mutex> lk(workers_[i]->mtx);
      }
      workers_[i]->cv.notify_one();
    }

    for (auto& t : worker_threads_) {
      if (t.joinable()) {
        t.join();
      }
    }

    for (auto& handle : subscription_handles_) {
      BusType::Instance().Unsubscribe(handle);
    }
    subscription_handles_.clear();

    workers_.clear();
    worker_threads_.clear();
    running_.store(false, std::memory_order_release);
  }

  /**
   * @brief Flush all pending work and pause accepting new jobs.
   *
   * Blocks until all dispatched jobs are processed by workers.
   */
  void FlushAndPause() noexcept {
    paused_.store(true, std::memory_order_release);

    // Wait until bus is drained, all worker queues empty, and all dispatched jobs processed
    while (true) {
      bool bus_empty = (BusType::Instance().QueueDepth() == 0U);
      bool workers_empty = true;
      for (uint32_t i = 0U; i < worker_num_; ++i) {
        if (workers_[i] && !workers_[i]->queue.Empty()) {
          workers_empty = false;
          break;
        }
      }
      uint64_t disp = dispatched_.load(std::memory_order_acquire);
      uint64_t proc = processed_.load(std::memory_order_acquire);
      if (bus_empty && workers_empty && disp == proc) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  }

  /**
   * @brief Resume accepting jobs after FlushAndPause.
   */
  void Resume() noexcept { paused_.store(false, std::memory_order_release); }

  // ======================== Submit API ========================

  /**
   * @brief Submit a job asynchronously via the mccc bus (lock-free).
   *
   * @tparam T Message type
   * @param payload Message payload
   * @param priority Message priority (default MEDIUM)
   * @return true if published to bus, false if paused or bus rejected
   */
  template <typename T>
  bool Submit(T&& payload, mccc::MessagePriority priority = mccc::MessagePriority::MEDIUM) noexcept {
    if (paused_.load(std::memory_order_acquire)) {
      return false;
    }
    return BusType::Instance().PublishWithPriority(PayloadVariant(std::forward<T>(payload)), 0U, priority);
  }

  /**
   * @brief Execute a job synchronously in the caller's thread.
   *
   * Bypasses the bus and worker threads entirely.
   *
   * @tparam T Message type
   * @param payload Message payload
   * @return true if handler exists and was invoked, false otherwise
   */
  template <typename T>
  bool SubmitSync(T&& payload) noexcept {
    constexpr size_t idx = mccc::VariantIndex<T, PayloadVariant>::value;
    if (dispatch_funcs_[idx] == nullptr) {
      return false;
    }
    mccc::MessageHeader header(0U, GetTimestampUs(), 0U, mccc::MessagePriority::HIGH);
    PayloadVariant pv(std::forward<T>(payload));
    EnvelopeType env(header, std::move(pv));
    dispatch_funcs_[idx](env, handler_ptrs_[idx]);
    return true;
  }

  // ======================== Query ========================

  Stats GetStats() const noexcept {
    Stats s;
    s.dispatched = dispatched_.load(std::memory_order_relaxed);
    s.processed = processed_.load(std::memory_order_relaxed);
    s.worker_queue_full = worker_queue_full_.load(std::memory_order_relaxed);
    s.bus_stats = BusType::Instance().GetStatistics();
    return s;
  }

  uint32_t WorkerCount() const noexcept { return worker_num_; }

  bool IsRunning() const noexcept { return running_.load(std::memory_order_acquire); }

  bool IsPaused() const noexcept { return paused_.load(std::memory_order_acquire); }

 private:
  // ======================== Type-erased dispatch ========================

  using DispatchFunc = void (*)(const EnvelopeType&, void*);

  template <typename T>
  static void TypedDispatch(const EnvelopeType& env, void* handler_ptr) noexcept {
    using FuncType = void (*)(const T&, const mccc::MessageHeader&);
    FuncType fn = nullptr;
    std::memcpy(&fn, &handler_ptr, sizeof(fn));
    const T* data = std::get_if<T>(&env.payload);
    if (data != nullptr) {
      fn(*data, env.header);
    }
  }

  void DispatchEnvelope(const EnvelopeType& env) noexcept {
    const size_t idx = env.payload.index();
    if (idx < kMaxTypes && dispatch_funcs_[idx] != nullptr) {
      dispatch_funcs_[idx](env, handler_ptrs_[idx]);
    }
  }

  // ======================== Bus subscription ========================

  template <typename T>
  void MaybeSubscribe() noexcept {
    constexpr size_t idx = mccc::VariantIndex<T, PayloadVariant>::value;
    if (dispatch_funcs_[idx] == nullptr) {
      return;
    }
    auto handle =
        BusType::Instance().template Subscribe<T>([this](const EnvelopeType& env) noexcept { DispatchToWorker(env); });
    if (handle.callback_id != static_cast<size_t>(-1)) {
      subscription_handles_.push_back(handle);
    }
  }

  template <typename... Types>
  void SubscribeAllImpl(std::variant<Types...>* /*tag*/) noexcept {
    (MaybeSubscribe<Types>(), ...);
  }

  void SubscribeAll(PayloadVariant* tag) noexcept { SubscribeAllImpl(tag); }

  // ======================== Worker dispatch ========================

  void DispatchToWorker(const EnvelopeType& env) noexcept {
    const uint32_t start = next_worker_.fetch_add(1U, std::memory_order_relaxed) % worker_num_;
    for (uint32_t i = 0U; i < worker_num_; ++i) {
      const uint32_t wid = (start + i) % worker_num_;
      if (workers_[wid]->queue.TryPush(env)) {
        dispatched_.fetch_add(1U, std::memory_order_release);
        { std::lock_guard<std::mutex> lk(workers_[wid]->mtx); }
        workers_[wid]->cv.notify_one();
        return;
      }
    }
    worker_queue_full_.fetch_add(1U, std::memory_order_relaxed);
  }

  // ======================== Dispatcher thread ========================

  void DispatcherLoop() noexcept {
    SetThreadPriority(priority_);

    while (!shutdown_.load(std::memory_order_acquire)) {
      uint32_t count = BusType::Instance().ProcessBatch();
      if (count == 0U) {
        std::this_thread::sleep_for(std::chrono::microseconds(50));
      }
    }

    // Final drain
    for (uint32_t round = 0U; round < 10U; ++round) {
      if (BusType::Instance().ProcessBatch() == 0U) {
        break;
      }
    }
  }

  // ======================== Worker thread ========================

  void WorkerLoop(uint32_t worker_id) noexcept {
    SetThreadPriority(priority_);
#ifdef __linux__
    if (cpu_set_ != nullptr && cpu_set_size_ > 0U) {
      pthread_setaffinity_np(pthread_self(), cpu_set_size_, cpu_set_);
    }
#endif

    WorkerContext& ctx = *workers_[worker_id];
    EnvelopeType env;

    while (!shutdown_.load(std::memory_order_acquire)) {
      if (ctx.queue.TryPop(env)) {
        DispatchEnvelope(env);
        processed_.fetch_add(1U, std::memory_order_release);
        continue;
      }

      std::unique_lock<std::mutex> lk(ctx.mtx);
      ctx.cv.wait_for(lk, std::chrono::milliseconds(1),
                      [&] { return !ctx.queue.Empty() || shutdown_.load(std::memory_order_acquire); });
    }

    // Drain remaining
    while (ctx.queue.TryPop(env)) {
      DispatchEnvelope(env);
      processed_.fetch_add(1U, std::memory_order_release);
    }
  }

  // ======================== Platform helpers ========================

  static void SetThreadPriority(int32_t prio) noexcept {
#ifdef __linux__
    if (prio > 0) {
      struct sched_param param{};
      param.sched_priority = (prio > 99) ? 99 : prio;
      pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
    } else if (prio < 0) {
      struct sched_param param{};
      param.sched_priority = 0;
      pthread_setschedparam(pthread_self(), SCHED_IDLE, &param);
    }
#else
    (void)prio;
#endif
  }

  static uint64_t GetTimestampUs() noexcept {
    auto now = std::chrono::steady_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch());
    return static_cast<uint64_t>(us.count());
  }

  // ======================== Worker context ========================

  struct WorkerContext {
    SpscQueue<EnvelopeType> queue;
    std::mutex mtx;
    std::condition_variable cv;

    explicit WorkerContext(uint32_t depth) noexcept : queue(depth) {}
    WorkerContext(const WorkerContext&) = delete;
    WorkerContext& operator=(const WorkerContext&) = delete;
    WorkerContext(WorkerContext&&) = delete;
    WorkerContext& operator=(WorkerContext&&) = delete;
  };

  // ======================== Data members ========================

  mccc::FixedString<32> name_;
  const uint32_t worker_num_;
  const int32_t priority_;
#ifdef __linux__
  const uint32_t cpu_set_size_{0U};
  const cpu_set_t* cpu_set_{nullptr};
#endif
  const uint32_t worker_queue_depth_;

  std::array<DispatchFunc, kMaxTypes> dispatch_funcs_;
  std::array<void*, kMaxTypes> handler_ptrs_;

  std::atomic<bool> running_{false};
  std::atomic<bool> shutdown_{false};
  std::atomic<bool> paused_{false};

  alignas(kCacheLineSize) std::atomic<uint64_t> dispatched_{0U};
  alignas(kCacheLineSize) std::atomic<uint64_t> processed_{0U};
  alignas(kCacheLineSize) std::atomic<uint64_t> worker_queue_full_{0U};
  alignas(kCacheLineSize) std::atomic<uint32_t> next_worker_{0U};

  std::vector<std::unique_ptr<WorkerContext>> workers_;
  std::vector<std::thread> worker_threads_;
  std::thread dispatcher_thread_;
  std::vector<mccc::SubscriptionHandle> subscription_handles_;
};

}  // namespace wp

#endif  // WP_WORKER_POOL_HPP_
