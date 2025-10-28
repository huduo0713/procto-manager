cmake_minimum_required(VERSION 3.10)
# 最好和协议名字保持一致
project(proto_bacnet)

# \------------------------------------------------------------------

# 新增: 强制构建静态库 (.a)

# 1\. 这将使 libproto\_bacnet 编译为 libproto\_bacnet.a

# 2\. 这也会“鼓励”CMake在链接时优先选择 libbacnet.a 和 libyaml.a

# (前提是它们在您的开发环境中存在)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)

# \------------------------------------------------------------------

# 项目根目录

set(ROOT_DIR ../..)

# 统一编译选项配置

include(${ROOT_DIR}/cmake/compile_options.cmake)

# 协议编译

# 指定依赖

set(PROTOCOL_LIBS bacnet yaml)
include(${ROOT_DIR}/cmake/protocol.cmake)

# 应用编译

include(${ROOT_DIR}/cmake/app.cmake)

# 所有依赖安装到build/package下

include(${ROOT_DIR}/cmake/install_rule.cmake)

add_executable(bacnet_demo demo/main.cc)

# 将 ${CMAKE_PROJECT_NAME} (它代表我们创建的 proto_bacnet 库) 添加到链接列表

set_common_include_dirs(bacnet_demo)

# 链接库 (现在会寻找 .a 文件)

target_link_libraries(bacnet_demo PRIVATE ${CMAKE_PROJECT_NAME} ${PROTOCOL_LIBS})

# \------------------------------------------------------------------

# 新增: 将 C++ 运行时静态编译进可执行文件

# 这解决了从开发容器 (如Ubuntu) 迁移到

# 测试盒子 (可能是另一个精简Linux) 时的 C++ 库版本不兼容问题

set_target_properties(bacnet_demo PROPERTIES
LINK_FLAGS "-static-libgcc -static-libstdc++")

# \------------------------------------------------------------------