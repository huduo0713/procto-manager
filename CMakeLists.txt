cmake_minimum_required(VERSION 3.10)
project(XGBoostPredictor)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Debug)  # 添加这句或确保为 Debug 模式

# 添加头文件搜索路径
include_directories(
    ${CMAKE_SOURCE_DIR}/impl
    ${CMAKE_SOURCE_DIR}/common/utils
    ${CMAKE_SOURCE_DIR}/3rdparty/spdlog/include
    
    )



# lib
add_library(algo_temp SHARED impl/infer_impl.cc)

target_link_libraries(algo_temp
    pthread
    rt             
    )


add_executable(demo 
    main.cpp
    )


target_link_libraries(demo PRIVATE algo_temp)

