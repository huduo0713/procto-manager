rm -rf build
# 删除旧的 build 目录及其所有内容（即清理上一次的构建缓存、错误文件）

mkdir build
# 创建一个新的 build 目录，用于存放构建生成的中间文件和最终可执行文件

cd build
# 进入 build 目录，准备在这里运行 cmake 和 make 命令

cmake ..
# 运行 CMake，读取上层目录（..）中的 CMakeLists.txt 文件，生成 Makefile 或其他构建配置

make
# 根据刚刚生成的 Makefile，实际编译项目的源代码并生成最终的可执行文件
