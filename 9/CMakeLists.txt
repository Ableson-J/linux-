cmake_minimum_required(VERSION 3.23)
project(codes)

set(CMAKE_CXX_STANDARD 14)

add_executable(9-3test 9-3mtlt.cpp)
add_executable(9-5test 9-5unblockconnect.cpp)
add_executable(9-6test 9-6mytalk_client.cpp)
add_executable(9-7test 9-7mytalk_server.cpp)
add_executable(9-8test 9-8multi_port.cpp)
add_executable(1test test.cpp)
