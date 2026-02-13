// Copyright (c) 2024 liudegui. MIT License.

#ifndef WP_TEST_TYPES_HPP_
#define WP_TEST_TYPES_HPP_

#include <cstdint>

#include <variant>

struct TaskA {
  int32_t id{0};
  int32_t value{0};
};

struct TaskB {
  float x{0.0f};
  float y{0.0f};
};

struct TaskC {
  uint64_t seq{0U};
};

using TestPayload = std::variant<TaskA, TaskB, TaskC>;

#endif  // WP_TEST_TYPES_HPP_
