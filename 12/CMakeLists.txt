cmake_minimum_required(VERSION 3.23)
project(codes)

set(CMAKE_CXX_STANDARD 14)

add_executable(12-1test 12-1libevent_test.c)
add_executable(12-2server 12-2server.cpp)
add_executable(12-3client 12-3client.cpp)