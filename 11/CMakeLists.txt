cmake_minimum_required(VERSION 3.23)
project(codes)

set(CMAKE_CXX_STANDARD 14)

add_executable(11-1test 11-1connect_timeout.cpp)
add_executable(11-3test 11-3nonactive_conn.cpp )