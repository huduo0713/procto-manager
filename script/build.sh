#!/bin/bash
# 名称：build.sh
# 功能：基于不同参数执行构建、清理或帮助等任务

source $PROJECT_ROOT/tools/color.sh

# 获取系统 CPU 核心数
NPROC=$(nproc)

# 设置 JOBS 参数
if [ "$NPROC" -lt 9 ]; then
    JOBS=4
else
    JOBS=$NPROC
fi

# 创建 build 目录（如果不存在）
if [ ! -d "build" ]; then
    echo_green "Creating build directory..."
    mkdir build
fi

clean_3rdparty() {
  rm -rf $PROJECT_ROOT/3rdparty/FastDeploy/build
  echo_yellow "clean Done!"
}

build_3rdparty() {
  cd $PROJECT_ROOT/3rdparty/FastDeploy
  mkdir -p build
  cd build
  cmake -DCMAKE_BUILD_TYPE=$1\
           -DENABLE_SOPHGO_BACKEND=ON \
           -DCMAKE_INSTALL_PREFIX=${PWD}/fastdeploy-sophgo \
           -DENABLE_VISION=ON \
           -DLIBSOPHON_INCLUDE_DIRS=/opt/sophon/libsophon-current/include/ \
           -DLIBSOPHON_LIB_DIRS=/opt/sophon/libsophon-current/lib/ \
           -DTARGET_ABI=arm64 \
           -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain.cmake \
           -DOPENCV_DIRECTORY=/usr/aarch64-linux-gnu/lib/cmake/opencv4/ \
           -DOpenCV_INCLUDE_DIRS=/usr/local/opencv4.3/include/opencv4 \
           -DOpenCV_LIBS=/usr/local/opencv4.3/aarch64/ \
           ..

  echo_green "Building project with $JOBS cores..."
  make -j$JOBS
  make install

}

# 定义构建函数
build_project() {
    local BUILD_TYPE=$1
    echo_green "Running CMake with build type: $BUILD_TYPE"

    # 进入 build 目录
    cd build

    # 运行 cmake
#    cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DCMAKE_INSTALL_PREFIX=./package ..
    cmake -DCMAKE_TOOLCHAIN_FILE=$PROJECT_ROOT/cmake/toolchain.cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DCMAKE_INSTALL_PREFIX=./package ..
    if [ $? -ne 0 ]; then
        echo_red "Error: CMake failed."
        exit 1
    fi

    # 并行构建，使用 nproc 来确定 CPU 核心数量
    echo_green "Building project with $JOBS cores..."
    make -j$JOBS
    if [ $? -ne 0 ]; then
        echo "Error: Build failed."
        exit 1
    fi

    echo_green "Build successful!"
    cd -
}

# 部署函数
deploy_project() {
    deploy.sh
    if [ $? -eq 0 ]; then
        echo_green "Deployment success!"
    else
        echo_red "Deployment failed!"
        exit 1
    fi
}

# 部署函数
install_project() {
    cmake --install ./build
    if [ $? -eq 0 ]; then
        echo_green "Install successful!"
    else
        echo_red "Install failed!"
        exit 1
    fi
}

# 参数校验
if [ $# -eq 0 ]; then
    echo_yellow "No argument provided, defaulting to Debug mode, and deploy..."
    build_project Debug
    deploy_project
    exit 0
fi

# 处理不同的命令行参数
case "$1" in
    clean)
       # 检查是否有第二个参数
       if [ $# -gt 1 ]; then
           case "$2" in
               app)
                   echo_yellow "Cleaning previous build app..."
                   rm -rf build/CMakeFiles/*.dir/
                   ;;
               3rdparty)
                   echo_yellow "Cleaning previous build 3rdparty..."
                   rm -rf build/3rdparty
                   ;;
               all)
                   echo_yellow "Cleaning previous build all..."
                   rm -rf build
                   ;;
               *)
                   echo_yellow "Cleaning previous build app..."
                   rm -rf build/CMakeFiles/*.dir/
                   ;;
           esac
       else
         echo_yellow "Cleaning previous build app..."
         rm -rf build/CMakeFiles/*.dir/
       fi
    ;;

    debug)
        echo_green "Building in Debug mode..."
        build_project Debug
        ;;

    release)
        echo_green "Building in Release mode..."
        build_project Release
        ;;
     deploy)
            echo_green "Deploying the project..."
            deploy_project
            ;;
     install)
            echo_green "install the project..."
            install_project
            ;;

   help|--help|-h)
       echo_yellow "Usage: $(basename $0) [options]"
       echo "  clean [app|3rdparty|all]      - Clean the built directory [app|3rdparty|all]."
       echo "  debug                         - Build the project in Debug mode. Default is debug."
       echo "  release                       - Build the project in Release mode."
       echo "  deploy                        - Deploy the built executable."
       echo "  install                       - Install the built package to CMAKE_INSTALL_PREFIX."
       echo "  help                          - Show this help message."
       ;;

   *)
       echo_yellow "Invalid argument: $1"
       echo_yellow "Usage: $(basename $0) [clean|debug|release|install|deploy|3rdparty|help]"
       exit 1
       ;;
esac
