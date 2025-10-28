## 拉取代码
```angular2html
git clone --recurse-submodules https://git.cisdigital.cn/qtouch/cisdi-framework/i800/procto-manager.git
```
## 拉取代码
```
git pull --recurse-submodules
```


## 配置环境变量
这一步会初始化相关环境变量，如果是交叉编译，需要更改里面的设备信息参数(IP、用户名、密码等)，以便deploy.sh到目标设备

```angular2html
cd {PROJECT_DIR}/
source script/env_setup.sh

# 参数说明

# 环境变量配置
# 项目路径
export PROJECT_ROOT=$(dirname "$DIR")
# 部署设备IP
export DEVICE_IP=192.168.11.75
# 部署设备用户名
export DEVICE_USERNAME=HwHiAiUser
# 部署设备密码
export DEVICE_PASSWD=Mind@123
# 部署设备远程调试端口
export DEVICE_GDB_SERVER_PORT=123
# 部署二进制的路径
export DEVICE_DEPLOY_DIR=/home/HwHiAiUser/xcd
```
## 协议编译
```
# 进入所在协议栈目录，执行脚本
cd ${PROJECT_DIR}/impl/modbus
# debug版本
build.sh
# release版本
build.sh release
```
# 运行demo
```
bash run.bash
# 调试
bash run.bash gdb
```

