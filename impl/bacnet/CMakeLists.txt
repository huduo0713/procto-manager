cmake_minimum_required(VERSION 3.10)

# ==============================================================================
# 项目基础配置
# ==============================================================================

project(proto_bacnet VERSION 1.0.0 LANGUAGES C CXX)

# 项目根目录
set(ROOT_DIR ../..)

# ==============================================================================
# 构建选项配置
# ==============================================================================

# 强制构建静态库 (.a 文件)
# 优势：
#   1. libproto_bacnet 编译为 libproto_bacnet.a
#   2. 链接时优先选择 libbacnet.a 和 libyaml.a (如果存在)
#   3. 减少运行时依赖，方便部署
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)

# ==============================================================================
# BACnet 数据链路层配置
# ==============================================================================

# 启用多数据链路层支持（允许同时使用 BACnet/IP + MS/TP 等）
# 说明：
#   - BACDL_MULTIPLE=1: 启用多数据链路层模式，运行时可同时初始化多个数据链路层
#   - 适用场景：PLC 需要同时访问以太网设备（BIP）和串口设备（MS/TP）
#   - 协议栈会自动维护设备路由表，根据设备地址选择正确的数据链路层
#   - 用户可选在 bacnet_read_t/bacnet_write_t 中指定 datalink_hint 强制路由
#   
#   ⚠️ 重要：BACnet 库编译要求
#   - BACnet/IP 支持：必定可用（BACDL_BIP）
#   - MS/TP 支持：需要 BACnet 库编译时启用 BACDL_MSTP
#   - 如果库未编译 MS/TP，运行时会自动跳过 MS/TP 初始化（优雅降级）
add_definitions(-DBACDL_MULTIPLE=1 -DBACDL_ALL=1 )

# ==============================================================================
# 包含公共配置文件
# ==============================================================================

# 统一的编译选项配置（编译器标志、警告级别等）
include(${ROOT_DIR}/cmake/compile_options.cmake)

# 协议栈编译配置（源文件扫描、库依赖等）
# 注意：使用 bacnet-stack 而不是 bacnet，因为新编译的库文件名为 libbacnet-stack.a
set(PROTOCOL_LIBS bacnet-stack yaml)
include(${ROOT_DIR}/cmake/protocol.cmake)

# 应用程序编译配置
include(${ROOT_DIR}/cmake/app.cmake)

# 安装规则配置（将依赖安装到 build/package 目录）
include(${ROOT_DIR}/cmake/install_rule.cmake)

# ==============================================================================
# 可执行文件构建 - 通用配置宏
# ==============================================================================

# 定义一个宏来简化可执行文件的配置
# 参数：
#   target_name  - 可执行文件名称
#   source_file  - 源文件路径
macro(add_bacnet_executable target_name source_file)
    add_executable(${target_name} ${source_file})
    set_common_include_dirs(${target_name})
    target_link_libraries(${target_name} PRIVATE ${CMAKE_PROJECT_NAME} ${PROTOCOL_LIBS})
    
    # 静态链接 C++ 运行时，避免不同 Linux 发行版的 libstdc++ 版本兼容问题
    # 这解决了从开发容器 (如 Ubuntu) 迁移到测试盒子 (可能是另一个精简 Linux) 时的兼容性问题
    set_target_properties(${target_name} PROPERTIES
        LINK_FLAGS "-static-libgcc -static-libstdc++"
    )
endmacro()

# ==============================================================================
# 可执行文件构建 - Demo 和测试程序
# ==============================================================================

# ------------------------------------------------------------------------------
# 1. 基础功能演示程序
#    文件：demo/main.cc
#    功能：演示基本的 BACnet 读写操作
# ------------------------------------------------------------------------------
add_bacnet_executable(bacnet_demo demo/main.cc)

# ------------------------------------------------------------------------------
# 2. 并发读写测试程序
#    文件：demo/concurrent_test.c
#    功能：测试多线程并发读写 BACnet 对象的正确性和线程安全性
# ------------------------------------------------------------------------------
# 目标：concurrent_test
#    描述：并发读写测试程序（新缓存机制）
#    文件：demo/concurrent_test.cc
#    链接：动态库 libproto_bacnet.so
add_bacnet_executable(concurrent_test demo/concurrent_test.cc)

# 允许程序在运行时查找同目录下的 .so 库
set_target_properties(concurrent_test PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN"
)

# ------------------------------------------------------------------------------
# 3. 热配置测试程序
#    文件：demo/hot_config_test.c
#    功能：演示配置文件自动监控和热重载功能
#    用法：运行后修改 config.yaml，观察程序自动检测并重载配置
#    注意：虽然是 .c 文件，但因为包含了 C++ 头文件，需要用 C++ 编译器编译
# ------------------------------------------------------------------------------
add_bacnet_executable(hot_config_test demo/hot_config_test.cc)

# 允许程序在运行时查找同目录下的 .so 库
set_target_properties(hot_config_test PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN"
)

# 强制用 C++ 编译器编译（因为包含了 one_logger.hpp 等 C++ 头文件）
set_source_files_properties(demo/hot_config_test.cc PROPERTIES LANGUAGE CXX)

# ------------------------------------------------------------------------------
# 4. 配置更新 API 测试程序
#    文件：demo/config_update_test.cc
#    功能：演示使用 plc_proto_config_update() API 动态修改配置文件
#    用法：运行程序，观察配置文件的修改
# ------------------------------------------------------------------------------
add_bacnet_executable(config_update_test demo/config_update_test.cc)

# 允许程序在运行时查找同目录下的 .so 库
set_target_properties(config_update_test PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN"
)

# ------------------------------------------------------------------------------
# 5. 多数据链路层使用示例
#    文件：demo/multi_datalink_example.cc
#    功能：演示如何在一个 PLC 中同时使用 BACnet/IP（以太网）和 MS/TP（串口）
#    用法：配置好 config.yaml 的 network 和 mstp section 后运行
#    说明：
#      - 演示自动路由和显式指定数据链路层两种方式
#      - 适用场景：同时访问以太网设备和串口设备
#      - 需要在 config.yaml 中配置 mstp.port（如 "/dev/ttyUSB0"）
# ------------------------------------------------------------------------------
add_bacnet_executable(multi_datalink_example demo/multi_datalink_example.cc)

# 允许程序在运行时查找同目录下的 .so 库
set_target_properties(multi_datalink_example PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN"
)

# ==============================================================================
# 构建总结信息
# ==============================================================================

message(STATUS "==============================================================")
message(STATUS "BACnet Protocol Stack - Build Configuration Summary")
message(STATUS "==============================================================")
message(STATUS "Project Name          : ${CMAKE_PROJECT_NAME}")
message(STATUS "Project Version       : ${PROJECT_VERSION}")
message(STATUS "Build Type            : ${CMAKE_BUILD_TYPE}")
message(STATUS "C Compiler            : ${CMAKE_C_COMPILER}")
message(STATUS "C++ Compiler          : ${CMAKE_CXX_COMPILER}")
message(STATUS "Build Shared Libs     : ${BUILD_SHARED_LIBS}")
message(STATUS "Protocol Libraries    : ${PROTOCOL_LIBS}")
message(STATUS "--------------------------------------------------------------")
message(STATUS "Executables to build:")
message(STATUS "  - bacnet_demo           : Basic BACnet demo")
message(STATUS "  - concurrent_test       : Concurrent read/write test")
message(STATUS "  - hot_config_test       : Hot config reload test")
message(STATUS "  - config_update_test    : Config update API test")
message(STATUS "  - multi_datalink_example: Multiple datalink demo (BIP + MS/TP)")
message(STATUS "==============================================================")

# ==============================================================================
# 结束
# ==============================================================================
