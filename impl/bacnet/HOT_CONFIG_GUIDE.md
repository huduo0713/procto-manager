# 🔥 热配置自动监控 - 快速测试指南

## 📋 功能说明

从 **v3.1** 版本开始，BACnet 驱动支持 **自动配置文件监控**：
- ✅ 编辑 `config.yaml` 并保存
- ✅ 1 秒内自动检测到变化
- ✅ 自动重载配置，无需重启程序
- ✅ CPU 开销 < 0.1%

---

## 🚀 快速测试（3 步）

### 步骤 1：编译测试程序

```bash
cd /path/to/procto-manager-hd/impl/bacnet

# Debug 模式编译
bash build.sh debug
```

### 步骤 2：启动测试程序

```bash
cd build/package/bin

# 方式 1：使用专用测试程序
./test_hot_config

# 方式 2：使用常规示例程序
./bacnet_demo
```

**期待输出**：
```
[BACnet][Driver] Initialization completed successfully
[BACnet][Driver] Hot config monitoring started for: ../config.yaml
[BACnet][HotConfig] Monitor thread started, watching: ../config.yaml
```

### 步骤 3：修改配置文件并观察

**在另一个终端窗口**：
```bash
cd /path/to/procto-manager-hd/impl/bacnet

# 编辑配置文件
vim config.yaml

# 修改任意配置项，例如：
# log_level: debug  →  log_level: info

# 保存退出 (:wq)
```

**回到程序输出窗口，1 秒内会看到**：
```
[BACnet][HotConfig] Config file changed (1699234567 -> 1699234890), invoking callback
[BACnet][HotConfig] Config file changed, reloading...
[BACnet] Config reload triggered by signal
[BACnet] Releasing current driver for config reload...
[BACnet][Driver] Stopping hot config monitoring...
[BACnet][HotConfig] Monitor thread stopped
[BACnet] Driver will be reinitialized on next request
...
[BACnet][Driver] Initializing driver...
[BACnet][Config] Configuration loaded from '../config.yaml'
[BACnet][Driver] Hot config monitoring started for: ../config.yaml  ← 自动重启监控
```

---

## 📊 性能验证

### 方法 1：使用 `top` 命令

```bash
# 找到进程 PID
ps aux | grep bacnet

# 监控 CPU 占用
top -p <PID>

# 预期结果：CPU < 0.5%（包括业务逻辑）
```

### 方法 2：使用 `strace` 追踪系统调用

```bash
# 追踪监控线程的系统调用
strace -p <PID> -e trace=stat,open,read 2>&1 | grep config.yaml

# 每秒会看到一次 stat() 调用
stat("../config.yaml", {...})  = 0
```

---

## 🔧 配置选项

### 修改轮询间隔

编辑 `src/proto_bacnet_hot_config.cpp`：

```cpp
void monitor_loop() {
    while (g_hot_config.running.load()) {
        // ... 检查文件变化 ...
        
        // 修改这里的休眠时间
        std::this_thread::sleep_for(std::chrono::seconds(1));  // 改为 5 秒
    }
}
```

**建议值**：
- 开发环境：1 秒（快速响应）
- 生产环境：5-10 秒（降低开销）

### 禁用自动监控

编辑 `src/proto_bacnet_driver.cpp`：

```cpp
int BacnetDriver::initialize(...) {
    // ... 现有代码 ...
    
    // 注释掉这段代码即可禁用
    /*
    if (!config_path_.empty()) {
        bacnet_hot_config_init(...);
    }
    */
}
```

---

## 🐛 故障排查

### 问题 1：监控线程未启动

**症状**：没有看到 "Hot config monitoring started" 日志

**检查清单**：
1. 配置文件路径是否正确
2. 配置文件是否存在且可读
3. 是否正确编译了 `proto_bacnet_hot_config.cpp`

**验证**：
```bash
# 检查配置文件是否存在
ls -l ../config.yaml

# 查看日志
grep "Hot config" bacnet.log
```

### 问题 2：配置修改后未生效

**症状**：编辑配置文件后没有触发重载

**可能原因**：
1. 编辑器使用了临时文件（如 Vim 的 `.swp`）
2. 文件修改时间（mtime）未更新
3. 监控线程已崩溃

**解决方法**：
```bash
# 强制更新 mtime
touch ../config.yaml

# 检查监控状态
grep "Monitor thread" bacnet.log | tail -5

# 手动触发重载
# 在代码中调用：bacnet_reload_config();
```

### 问题 3：CPU 占用过高

**症状**：CPU 占用超过 1%

**可能原因**：
1. 轮询间隔设置过短（< 100ms）
2. 磁盘 I/O 缓慢（如 NFS 网络挂载）

**解决方法**：
1. 增大轮询间隔到 5-10 秒
2. 考虑使用 `inotify` 机制（Linux）或 `kqueue`（BSD/macOS）

---

## 📚 API 参考

### C 接口

```c
// 启动监控（通常自动调用，无需手动）
int bacnet_hot_config_init(const char *config_path, 
                           void (*on_changed)(void), 
                           void *userdata);

// 停止监控
void bacnet_hot_config_stop(void);

// 检查监控状态
int bacnet_hot_config_is_running(void);

// 手动触发重载
int bacnet_reload_config(void);
```

### 使用示例

```c
#include "proto_bacnet.h"

// 自定义回调函数
void my_callback(void) {
    printf("配置文件已变化！\n");
    bacnet_reload_config();
}

int main() {
    // 方式 1：使用 BACnet 驱动（自动启动监控）
    plc_proto_read(...);  // 会自动初始化并启动监控
    
    // 方式 2：手动启动监控
    bacnet_hot_config_init("../config.yaml", my_callback, NULL);
    
    // ... 业务逻辑 ...
    
    // 清理
    bacnet_hot_config_stop();
    return 0;
}
```

---

## 🎯 最佳实践

### ✅ 推荐做法

1. **使用默认配置**：让 BACnet 驱动自动启动监控，无需额外代码
2. **保持轮询间隔合理**：1-5 秒即可，无需更频繁
3. **监控日志文件**：观察配置重载是否成功
4. **测试配置变更**：先在开发环境验证配置有效性

### ❌ 避免做法

1. **频繁修改配置**：避免在 1 秒内多次保存配置文件
2. **过短轮询间隔**：< 100ms 可能导致性能问题
3. **忽略重载失败**：检查日志确认配置成功加载
4. **在回调中执行耗时操作**：`on_changed` 回调应快速返回

---

## 🔮 未来改进方向

- [ ] 使用 `inotify`（Linux）替代轮询，实时响应
- [ ] 支持配置验证（重载前检查语法）
- [ ] 支持部分配置热更新（不重启驱动）
- [ ] 配置变更历史记录

---

## 📞 支持与反馈

如有问题或建议，请联系开发团队或提交 Issue。

**文档版本**：v3.1  
**最后更新**：2025-11-07
