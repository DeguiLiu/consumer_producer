// Copyright (c) 2024 liudegui. MIT License.
// Basic usage example for WorkerPool.

#include <cstdint>
#include <cstdio>

#include <chrono>
#include <thread>
#include <variant>
#include <wp/worker_pool.hpp>

struct SensorData {
  int32_t sensor_id;
  float temperature;
};

struct MotorCommand {
  int32_t motor_id;
  int32_t speed;
};

using MyPayload = std::variant<SensorData, MotorCommand>;

static void HandleSensor(const SensorData& data, const mccc::MessageHeader& hdr) {
  std::printf("[%lu] Sensor %d: temp=%.1f\n", static_cast<unsigned long>(hdr.msg_id), data.sensor_id,
              static_cast<double>(data.temperature));
}

static void HandleMotor(const MotorCommand& cmd, const mccc::MessageHeader& hdr) {
  std::printf("[%lu] Motor %d: speed=%d\n", static_cast<unsigned long>(hdr.msg_id), cmd.motor_id, cmd.speed);
}

int main() {
  wp::Config cfg;
  cfg.name = "demo";
  cfg.worker_num = 2;
  cfg.worker_queue_depth = 64;

  wp::WorkerPool<MyPayload> pool(cfg);
  pool.RegisterHandler<SensorData>(HandleSensor);
  pool.RegisterHandler<MotorCommand>(HandleMotor);

  pool.Start();

  // Submit jobs
  for (int32_t i = 0; i < 5; ++i) {
    pool.Submit(SensorData{i, 20.0f + static_cast<float>(i)});
    pool.Submit(MotorCommand{i, 1000 + i * 100});
  }

  // Flush and show stats
  pool.FlushAndPause();

  auto stats = pool.GetStats();
  std::printf("\n--- Stats ---\n");
  std::printf("dispatched: %lu\n", static_cast<unsigned long>(stats.dispatched));
  std::printf("processed:  %lu\n", static_cast<unsigned long>(stats.processed));
  std::printf("bus published: %lu\n", static_cast<unsigned long>(stats.bus_stats.messages_published));

  pool.Resume();

  // SubmitSync example
  std::printf("\n--- SubmitSync ---\n");
  pool.SubmitSync(SensorData{99, 42.0f});

  pool.Shutdown();
  std::printf("Done.\n");
  return 0;
}
