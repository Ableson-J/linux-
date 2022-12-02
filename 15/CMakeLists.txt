cmake_minimum_required(VERSION 3.23)
project(codes)

set(CMAKE_CXX_STANDARD 14)

add_executable(15-2test 15-2pool_cgi.cpp)
#add_executable(15-5test 15-5http_conn.cpp)
#add_executable(15-51test 15-5http_conn1.cpp)
add_executable(cgi cgi.cpp)
add_executable(15-6test 15-5http_conn.cpp 15-6main.cpp)
include_directories(../14)