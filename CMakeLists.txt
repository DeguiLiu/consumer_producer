cmake_minimum_required(VERSION 3.14)
project(consumer_producer VERSION 1.0.0 LANGUAGES CXX)

add_library(consumer_producer INTERFACE)
target_include_directories(consumer_producer INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(consumer_producer INTERFACE cxx_std_14)

find_package(Threads REQUIRED)

option(CP_BUILD_EXAMPLES "Build examples" ON)
option(CP_BUILD_TESTS "Build tests" ON)

if(CP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(CP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

install(TARGETS consumer_producer EXPORT consumer_producerTargets)
install(DIRECTORY include/cp DESTINATION include)
