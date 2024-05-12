/**

*/

#ifndef MY_LIDAR_LOG_H_
#define MY_LIDAR_LOG_H_
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>

#ifndef MY_LOG_MACROS_H
#define MY_LOG_MACROS_H
#define MY_LOG(level, ...) \
  MemTest::inno_log(MemTest::MyLogLevel::level, __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)

#define MY_LOG_TRACE(...) MY_LOG(INNOLOG_LEVEL_TRACE, __VA_ARGS__)
#define MY_LOG_DEBUG(...) MY_LOG(INNOLOG_LEVEL_DEBUG, __VA_ARGS__)
#define MY_LOG_INFO(...) MY_LOG(INNOLOG_LEVEL_INFO, __VA_ARGS__)
#define MY_LOG_WARN(...) MY_LOG(INNOLOG_LEVEL_WARN, __VA_ARGS__)
#define MY_LOG_ERROR(...) MY_LOG(INNOLOG_LEVEL_ERROR, __VA_ARGS__)
#define MY_LOG_CRITICAL(...) MY_LOG(INNOLOG_LEVEL_CRITICAL, __VA_ARGS__)
#define MY_LOG_PANIC(...)                      \
  MY_LOG(INNOLOG_LEVEL_CRITICAL, __VA_ARGS__); \
  exit(1)
#endif  // MY_LOG_MACROS_H

namespace MemTest {

enum class MyLogLevel : std::int32_t {
  INNOLOG_LEVEL_TRACE = 0,
  INNOLOG_LEVEL_DEBUG = 1,
  INNOLOG_LEVEL_INFO = 2,
  INNOLOG_LEVEL_WARN = 3,
  INNOLOG_LEVEL_ERROR = 4,
  INNOLOG_LEVEL_CRITICAL = 5,
  INNOLOG_LEVEL_OFF = 6
};

static constexpr uint32_t kLogBufferSize{1024};

template <typename... ARGS_TYPE>
void inno_log(MyLogLevel src_lev, const std::string &src_file, const std::string &src_func, int32_t src_line,
              const std::string &fmt_str, ARGS_TYPE &&... fmt_args) {
  std::string tmp_buf(kLogBufferSize, '\0');
  int32_t sp_res{0};
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-security"
  sp_res = std::snprintf(&tmp_buf[0], tmp_buf.size(), fmt_str.c_str(), std::forward<ARGS_TYPE>(fmt_args)...);
#pragma GCC diagnostic pop
  if (sp_res >= 0) {
    std::cout << "["<< src_file << " - " << static_cast<int32_t>(src_lev)
        << " - " << src_func << ":" << src_line << "] - ";
    std::cout << tmp_buf << std::endl;
  }
}

}  // namespace MemTest

#endif  // MY_LIDAR_LOG_H_
