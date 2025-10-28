cmake_minimum_required(VERSION 3.10)
# 最好和协议名字保持一致
project(proto_mqtt)
set(CMAKE_BUILD_TYPE Debug)
# 项目根目录
set(ROOT_DIR ../..)

# 统一编译选项配置
include(${ROOT_DIR}/cmake/compile_options.cmake)

# 协议编译
# 指定依赖
set(PROTOCOL_LIBS ${CMAKE_CURRENT_SOURCE_DIR}/depend/lib/libpaho-mqtt3a.a yaml)
include(${ROOT_DIR}/cmake/protocol.cmake)

# 强制仅对 paho-mqtt3a 使用静态链接，其余保持动态
if (UNIX)
    target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -Wl,-Bstatic -lpaho-mqtt3a -Wl,-Bdynamic)
endif()

########################### demo测试文件 ############################
# 创建 demo 可执行文件
if (PLT_SOPHON_USE_SOPHON_LIB)
    set(DEPEND_SOPHON_FILE ${ROOT_DIR}/platform/sophon/media/ff_video_decode.cpp)
endif ()
add_executable(
        demo demo/main.cc
)
# 定义版本
target_compile_definitions(demo PRIVATE
        PLATFORM_NAME=${PLATFORM_NAME}
)

# 为 demo 设置包含目录
set_common_include_dirs(demo)

# 链接 algo 库到 demo
# 使用1.3.3版本
target_link_libraries(demo PRIVATE ${CMAKE_PROJECT_NAME} yaml)


# overview compile information
include(${ROOT_DIR}/cmake/overview.cmake)
project_overview()


# 所有依赖安装到build/package下
include(${ROOT_DIR}/cmake/install_rule.cmake)