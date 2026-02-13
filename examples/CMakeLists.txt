add_executable(cp_basic_example basic_example.cpp)
target_link_libraries(cp_basic_example consumer_producer Threads::Threads)

add_executable(cp_priority_example priority_example.cpp)
target_link_libraries(cp_priority_example consumer_producer Threads::Threads)

add_executable(cp_benchmark benchmark.cpp)
target_link_libraries(cp_benchmark consumer_producer Threads::Threads)
