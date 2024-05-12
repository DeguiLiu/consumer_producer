cmake_minimum_required(VERSION 3.10)

project(consumer_producer_test)
project(consumer_producer_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "-O0 -Wall -g -ggdb")
add_executable(consumer_producer_test consumer_producer.hpp consumer_producer_test.cpp)
target_link_libraries(consumer_producer_test pthread)
