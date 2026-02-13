cmake_minimum_required(VERSION 3.14)
project(worker_pool VERSION 1.0.0 LANGUAGES CXX)

include(FetchContent)

# Fetch mccc dependency
set(MCCC_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MCCC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  mccc
  GIT_REPOSITORY https://ghfast.top/https://github.com/DeguiLiu/mccc.git
  GIT_TAG master)
FetchContent_MakeAvailable(mccc)

add_library(worker_pool INTERFACE)
target_include_directories(worker_pool INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(worker_pool INTERFACE cxx_std_17)
target_link_libraries(worker_pool INTERFACE mccc)

find_package(Threads REQUIRED)

option(WP_BUILD_EXAMPLES "Build examples" ON)
option(WP_BUILD_TESTS "Build tests" ON)

if(WP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(WP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

install(TARGETS worker_pool EXPORT worker_poolTargets)
install(DIRECTORY include/wp DESTINATION include)
