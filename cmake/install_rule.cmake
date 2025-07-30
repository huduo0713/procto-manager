# # 替换配置文件模板的占位符 @PROJECT_NAME@ 为项目名称
# configure_file(
#         ${ROOT_DIR}/config/algo_config.json  # 输入模板
#         ./algo_config.json # 输出文件
#         @ONLY              # 仅替换 @ 符号里面的变量为CMAKE的项目名称变量PROJECT_NAME
# )


# 定义 package 文件夹的路径（创建在 CMake 构建目录中）
set(PACKAGE_DIR "${CMAKE_BINARY_DIR}/package")
# 创建 package 文件夹
file(MAKE_DIRECTORY ${PACKAGE_DIR})

# install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/build/algo_config.json DESTINATION ./)
# install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/demo/default_config.yaml DESTINATION ./)
# install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/build/liblogic_process.so DESTINATION ./)
# install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/build/lib${PROJECT_NAME}.so DESTINATION ./)
# install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/demo/data/model/ DESTINATION ./
#         PATTERN ".*" EXCLUDE     # 排除隐藏文件和隐藏目录
# )
# install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/build/3rdparty/fastdeploy/third_party/yaml-cpp/libyaml-cpp.so.0.7 DESTINATION ./)
# install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/build/3rdparty/fastdeploy/libfastdeploy.so DESTINATION ./)
# message(STATUS "package file is generated at: ${PACKAGE_DIR}")
