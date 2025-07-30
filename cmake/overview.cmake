function(project_overview)
  message(STATUS "")
  message(STATUS "*************${CMAKE_PROJECT_NAME} Building Summary**********")
  message(STATUS "  Project Build time        : ${PROJECT_BUILD_TIME}")
  message(STATUS "  Project version           : ${VERSION_CONTENT}")
  message(STATUS "  CMake version             : ${CMAKE_VERSION}")
  message(STATUS "  CMake command             : ${CMAKE_COMMAND}")
  message(STATUS "  System                    : ${CMAKE_SYSTEM_NAME}")
  message(STATUS "  C++ compiler              : ${CMAKE_CXX_COMPILER}")
  message(STATUS "  C++ standard              : ${CMAKE_CXX_STANDARD}")
  message(STATUS "  C++ cuda standard         : ${CMAKE_CUDA_STANDARD}")
  message(STATUS "  C++ compiler version      : ${CMAKE_CXX_COMPILER_VERSION}")
  message(STATUS "  CXX flags                 : ${CMAKE_CXX_FLAGS}")
  message(STATUS "  EXE linker flags          : ${CMAKE_EXE_LINKER_FLAGS}")
  message(STATUS "  Shared linker flags       : ${CMAKE_SHARED_LINKER_FLAGS}")
  message(STATUS "  Build type                : ${CMAKE_BUILD_TYPE}")
  get_directory_property(tmp DIRECTORY ${PROJECT_SOURCE_DIR} COMPILE_DEFINITIONS)
  message(STATUS "  Compile definitions       : ${tmp}")
  message(STATUS "  CMAKE_PREFIX_PATH         : ${CMAKE_PREFIX_PATH}")
  message(STATUS "  CMAKE_INSTALL_PREFIX      : ${CMAKE_INSTALL_PREFIX}")

endfunction()


function(detectPlatformByEnv)
  if(EXISTS "/usr/local/Ascend")
    set(PLATFORM_NAME "ascend" PARENT_SCOPE)
  elseif(EXISTS "/opt/sophon")
    set(PLATFORM_NAME "sophon" PARENT_SCOPE)
  elseif(EXISTS "/usr/local/cuda")
    set(PLATFORM_NAME "jetson" PARENT_SCOPE)
  endif()
endfunction()


function(getPlatformName)
  set(SUPPORTED_PLATFORMS ascend sophon nvidia jetson cpu rknn)
  # 获取当前目录
  get_filename_component(CURRENT_DIR ${CMAKE_CURRENT_SOURCE_DIR} ABSOLUTE)

  # 获取上一级目录
  get_filename_component(PARENT_DIR ${CURRENT_DIR} DIRECTORY)

  # 获取上一级目录的名称
  get_filename_component(PARENT_NAME ${PARENT_DIR} NAME)
  # 兼容性判断
  if (NOT ${PARENT_NAME} IN_LIST SUPPORTED_PLATFORMS)
    message(STATUS "detect platform failed by dir(found [${PARENT_NAME}]), use detectPlatformByEnv() instead.")
    detectPlatformByEnv()
    # 传递给上一级作用域
    set(PLATFORM_NAME ${PLATFORM_NAME} PARENT_SCOPE)
  else ()
    # 输出上一级目录的名称
    set(PLATFORM_NAME ${PARENT_NAME} PARENT_SCOPE)
  endif()
  message(STATUS "detect platform success, found [${PLATFORM_NAME}].")


endfunction()