cmake_minimum_required(VERSION 3.10)
project(XGBoostPredictor)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Debug)  # 添加这句或确保为 Debug 模式

# 添加头文件搜索路径
include_directories(
    ${CMAKE_SOURCE_DIR}/impl
    ${CMAKE_SOURCE_DIR}/common/utils
    ${CMAKE_SOURCE_DIR}/3rdparty/spdlog/include
    ${CMAKE_SOURCE_DIR}/include
    )


# 链接 xgboost 的动态库路径
link_directories(${CMAKE_SOURCE_DIR}/lib)

# lib
file(GLOB IMPL_SOURCES "impl/*.cc")

add_library(algo_temp SHARED ${IMPL_SOURCES})

target_link_libraries(algo_temp
    pthread
    rt
    xgboost             
    )


add_executable(demo 
    main.cpp
    )


target_link_libraries(demo PRIVATE algo_temp)

