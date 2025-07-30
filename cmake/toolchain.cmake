# compiler
# if used toolchain, set aarch64 by default
message(STATUS "Detected ${CMAKE_SYSTEM_PROCESSOR} architecture")
set(CMAKE_C_COMPILER "/usr/bin/aarch64-linux-gnu-gcc")
set(CMAKE_CXX_COMPILER "/usr/bin/aarch64-linux-gnu-g++")
# set Asan内存检测工具. sanitize flags
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer -g")