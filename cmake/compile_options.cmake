# 设置 C++ 17标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# 打印Make详情，用于调试
set(CMAKE_VERBOSE_MAKEFILE ON)

# 设置编译工具链
# set in cmake cmd.

# 设置版本信息，仅在 Release 模式下启用
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    include(${ROOT_DIR}/cmake/version_generate.cmake)
    generate_version_func()
    message(STATUS "Version generation enabled for Release build.")
else()
    message(STATUS "Version generation skipped for non-Release build.")
endif()



# 定义项目统一的头文件目录
set(SOURCE_INCLUDE_DIRS
        ${ROOT_DIR}
)