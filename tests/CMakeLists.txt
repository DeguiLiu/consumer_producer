include(FetchContent)
set(CATCH2_VERSION v3.5.2)
FetchContent_Declare(
  Catch2
  URL https://ghfast.top/https://github.com/catchorg/Catch2/archive/refs/tags/${CATCH2_VERSION}.zip
  DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(Catch2)

include(Catch)

find_package(Threads REQUIRED)

add_executable(cp_tests
    test_basic.cpp
    test_priority.cpp
    test_discard.cpp
    test_concurrent.cpp
    test_stats.cpp
    test_edge_cases.cpp)
target_link_libraries(cp_tests Catch2::Catch2WithMain consumer_producer Threads::Threads)
target_compile_options(cp_tests PRIVATE -Wall -Wextra -Wpedantic)
catch_discover_tests(cp_tests)
