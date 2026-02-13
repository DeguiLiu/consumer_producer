add_executable(wp_basic_example basic_example.cpp)
target_link_libraries(wp_basic_example worker_pool Threads::Threads)

add_executable(wp_benchmark benchmark.cpp)
target_link_libraries(wp_benchmark worker_pool Threads::Threads)
