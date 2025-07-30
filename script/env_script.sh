#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"


# 环境变量配置
# 项目路径
export PROJECT_ROOT=$(dirname "$DIR")
# 部署设备IP
export DEVICE_IP=192.168.11.89
#export DEVICE_IP=192.168.11.231
# 部署设备用户名
export DEVICE_USERNAME=linaro
# 部署设备密码
export DEVICE_PASSWD=Mind@123
# 部署设备远程调试端口
export DEVICE_GDB_SERVER_PORT=123
# 部署二进制的路径
#export DEVICE_DEPLOY_DIR=/home/mic-711/xcd
export DEVICE_DEPLOY_DIR=/home/linaro/xcd
export DEVICE_DEPLOY_DIR=$PROJECT_ROOT/../

env |grep -E 'PROJECT_ROOT|DEVICE_*'
env |grep -E 'PROJECT_ROOT|DEVICE_*' > $PROJECT_ROOT/script/.env


# 配置脚本路径
chmod +x $PROJECT_ROOT/script/build.sh
chmod +x $PROJECT_ROOT/tools/deploy/deploy.sh
export PATH=$PROJECT_ROOT/script:$PATH
export PATH=$PROJECT_ROOT/tools/deploy:$PATH
