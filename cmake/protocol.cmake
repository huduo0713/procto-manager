# 第三方依赖库构建(FastDeploy,opencv, ...)
# include(${ROOT_DIR}/cmake/3rdparty.cmake)

set(PROTOCOL_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/depend/include)
set(PROTOCOL_LIBRARY_DIR ${CMAKE_SOURCE_DIR}/depend/lib)

# 定义第三方库头文件
set(THIRDPARTY_INCLUDE_DIRS
)

# 为目标设置公共的包含目录
function(set_common_include_dirs target)
    target_include_directories(${target} PRIVATE ${PROTOCOL_INCLUDE_DIR})
    target_include_directories(${target} PRIVATE ${ROOT_DIR})
    target_include_directories(${target} PRIVATE ${ROOT_DIR}/3rdparty/spdlog/include)
    # 额外路径
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
endfunction()


# 指定源文件
file(GLOB SRC_FILES
        src/*.cc
        src/*.c
)

# 对依赖 C++ 头文件的 C 源做特殊处理
set(_maybe_cpp_sources
        ${CMAKE_CURRENT_SOURCE_DIR}/src/proto_mqtt_core.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/proto_mqtt_callbacks.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/plc_mqtt_entry.c
)
foreach(_src ${_maybe_cpp_sources})
    if (EXISTS ${_src})
        set_source_files_properties(${_src} PROPERTIES LANGUAGE CXX)
    endif ()
endforeach()

# 库文件链接目录
link_directories(
    ${PROTOCOL_LIBRARY_DIR}
)

# 创建 协议栈库
add_library(${CMAKE_PROJECT_NAME} SHARED ${SRC_FILES})


# 定义版本
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        PROJECT_VERSION=${VERSION_CONTENT}
        PROJECT_BUILD_TIME=${PROJECT_BUILD_TIME}
)
# 设置包含目录
set_common_include_dirs(${CMAKE_PROJECT_NAME})

# 定义依赖的库变量.
set(COMMON_LIBS pthread)

# 链接库文件, 顺序不能变, 被依赖的库放后面
target_link_libraries(${CMAKE_PROJECT_NAME} PUBLIC ${PROTOCOL_LIBS}  ${COMMON_LIBS})