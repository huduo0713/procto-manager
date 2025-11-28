@echo off
REM BACnet项目同步脚本 (Windows)
REM 使用WSL的rsync命令同步bacnet文件夹到服务器

REM 配置参数
set SERVER_HOST=10.77.26.5
set SERVER_PORT=16002
set SERVER_USER=root
set LOCAL_PATH=/mnt/e/procto-manager-hd/impl/bacnet/
set REMOTE_PATH=/workspace/protocol/procto-manager-hd/impl/bacnet/

echo ========================================
echo BACnet项目同步脚本
echo ========================================
echo 服务器: %SERVER_HOST%:%SERVER_PORT%
echo 本地路径: %LOCAL_PATH%
echo 远程路径: %REMOTE_PATH%
echo ========================================

REM 检查WSL是否可用
wsl --list >nul 2>&1
if %errorlevel% neq 0 (
    echo 错误: WSL未安装或不可用
    pause
    exit /b 1
)

echo 正在同步文件...
echo.

REM 使用rsync命令同步到服务器（使用WSL）
wsl rsync -avz --delete -e 'ssh -p %SERVER_PORT%' ^
    --exclude='build/' ^
    --exclude='depend/' ^
    --exclude='.vscode/' ^
    --exclude='*.o' ^
    --exclude='*.a' ^
    --exclude='*.so' ^
    --exclude='*.log' ^
    --exclude='.git/' ^
    --exclude='.git/**' ^
    --exclude='*.tmp' ^
    --exclude='CMakeCache.txt' ^
    --exclude='CMakeFiles/' ^
    --exclude='cmake_install.cmake' ^
    --exclude='Makefile' ^
    --exclude='compile_commands.json' ^
    %LOCAL_PATH% ^
    %SERVER_USER%@%SERVER_HOST%:%REMOTE_PATH%

REM 检查rsync命令结果
if %errorlevel% equ 0 (
    echo.
    echo ========================================
    echo ✅ 同步完成！
    echo ========================================
) else (
    echo.
    echo ========================================
    echo ❌ 同步失败！错误码: %errorlevel%
    echo ========================================
)

echo.
@REM echo 按任意键退出...
@REM pause >nul