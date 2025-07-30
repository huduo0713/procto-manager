cmake_minimum_required(VERSION 3.10)
# 最好和协议名字保持一致
project(proto_modbus)

# 项目根目录
set(ROOT_DIR ../..)

# 统一编译选项配置
include(${ROOT_DIR}/cmake/compile_options.cmake)

# 协议编译
# 指定依赖
set(PROTOCOL_LIBS modbus)
include(${ROOT_DIR}/cmake/protocol.cmake)

# 应用编译
include(${ROOT_DIR}/cmake/app.cmake)

# 所有依赖安装到build/package下
include(${ROOT_DIR}/cmake/install_rule.cmake)