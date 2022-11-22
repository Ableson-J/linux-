cmake_minimum_required(VERSION 3.23)
project(codes)

set(CMAKE_CXX_STANDARD 14)

add_executable(14-1test 14-1mutual_lock.c)
add_executable(14-3test 14-3thread_atfork.c)
add_executable(14-5test 14-5sigmask.c)
add_executable(cond_var_test cond_var_test.cpp)
add_executable(signal_test mutil_thread_signal_test.cpp)