########################### demo测试文件 ############################
# 创建 demo 可执行文件
if (PLT_SOPHON_USE_SOPHON_LIB)
    set(DEPEND_SOPHON_FILE ${ROOT_DIR}/platform/sophon/media/ff_video_decode.cpp)
endif ()
add_executable(demo demo/main.c
        # ${ROOT_DIR}/common/utils/config_manager.cpp
        # ${ROOT_DIR}/common/manage/service/websocket/web_socket_service_task.cpp
        # ${ROOT_DIR}/common/manage/task/base_task.cpp
        # ${DEPEND_SOPHON_FILE}
)
# 定义版本
target_compile_definitions(demo PRIVATE
        PLATFORM_NAME=${PLATFORM_NAME}
)

# 为 demo 设置包含目录
set_common_include_dirs(demo)

# 链接 algo 库到 demo
# 使用1.3.3版本
target_link_libraries(demo PRIVATE ${CMAKE_PROJECT_NAME}
)

# overview compile information
include(${ROOT_DIR}/cmake/overview.cmake)
project_overview()
