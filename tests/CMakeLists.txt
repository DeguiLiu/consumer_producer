include(FetchContent)
set(CATCH2_VERSION v3.5.2)
FetchContent_Declare(
  Catch2
  URL https://ghfast.top/https://github.com/catchorg/Catch2/archive/refs/tags/${CATCH2_VERSION}.zip
  DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(Catch2)

include(Catch)

find_package(Threads REQUIRED)

add_executable(wp_tests
    test_basic.cpp
    test_submit.cpp
    test_concurrent.cpp
    test_flush.cpp
    test_stats.cpp)
target_link_libraries(wp_tests Catch2::Catch2WithMain worker_pool Threads::Threads)
target_compile_options(wp_tests PRIVATE -Wall -Wextra -Wpedantic)
catch_discover_tests(wp_tests)
