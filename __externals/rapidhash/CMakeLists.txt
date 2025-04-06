cmake_minimum_required(VERSION 3.12)
project(rapidhash)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(rapidhash INTERFACE)

target_include_directories(rapidhash INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
