#!/bin/bash

# BACnet 重构版本切换脚本
# 用于在新旧实现之间切换

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="${SCRIPT_DIR}/src"

echo "========================================="
echo "BACnet 实现版本切换脚本"
echo "========================================="

show_status() {
    echo ""
    echo "当前文件状态:"
    echo "----------------------------------------"
    if [ -f "${SRC_DIR}/proto_bacnet_core_old.cpp" ]; then
        echo "✓ 旧版本已备份: proto_bacnet_core_old.cpp"
    else
        echo "✗ 旧版本未备份"
    fi
    
    if [ -f "${SRC_DIR}/proto_bacnet_core_new.cpp" ]; then
        echo "✓ 新版本存在: proto_bacnet_core_new.cpp"
    else
        echo "✗ 新版本不存在"
    fi
    
    if [ -f "${SRC_DIR}/proto_bacnet_core.cpp" ]; then
        local line_count=$(wc -l < "${SRC_DIR}/proto_bacnet_core.cpp")
        echo "✓ 当前使用: proto_bacnet_core.cpp (${line_count} 行)"
        
        if grep -q "namespace bacnet" "${SRC_DIR}/proto_bacnet_core.cpp" 2>/dev/null; then
            echo "  -> 检测到: 新版本 (使用 namespace bacnet)"
        else
            echo "  -> 检测到: 旧版本 (未使用 namespace)"
        fi
    else
        echo "✗ proto_bacnet_core.cpp 不存在"
    fi
    
    echo ""
    echo "新增模块文件:"
    for file in callbacks discovery io; do
        if [ -f "${SRC_DIR}/proto_bacnet_${file}.cpp" ]; then
            local lines=$(wc -l < "${SRC_DIR}/proto_bacnet_${file}.cpp")
            echo "  ✓ proto_bacnet_${file}.cpp (${lines} 行)"
        else
            echo "  ✗ proto_bacnet_${file}.cpp (缺失)"
        fi
    done
    echo "----------------------------------------"
}

backup_old() {
    echo ""
    echo "备份旧版本..."
    if [ -f "${SRC_DIR}/proto_bacnet_core.cpp" ]; then
        if [ -f "${SRC_DIR}/proto_bacnet_core_old.cpp" ]; then
            echo "警告: proto_bacnet_core_old.cpp 已存在，将被覆盖"
        fi
        cp "${SRC_DIR}/proto_bacnet_core.cpp" "${SRC_DIR}/proto_bacnet_core_old.cpp"
        echo "✓ 已备份到 proto_bacnet_core_old.cpp"
    else
        echo "✗ proto_bacnet_core.cpp 不存在，无需备份"
        return 1
    fi
}

switch_to_new() {
    echo ""
    echo "切换到新版本..."
    
    if [ ! -f "${SRC_DIR}/proto_bacnet_core_new.cpp" ]; then
        echo "✗ 错误: proto_bacnet_core_new.cpp 不存在"
        return 1
    fi
    
    # 备份当前版本
    if [ -f "${SRC_DIR}/proto_bacnet_core.cpp" ]; then
        backup_old
    fi
    
    # 使用新版本
    cp "${SRC_DIR}/proto_bacnet_core_new.cpp" "${SRC_DIR}/proto_bacnet_core.cpp"
    echo "✓ 已切换到新版本"
    
    # 检查依赖文件
    local missing=0
    for file in callbacks discovery io; do
        if [ ! -f "${SRC_DIR}/proto_bacnet_${file}.cpp" ]; then
            echo "✗ 警告: 缺少 proto_bacnet_${file}.cpp"
            missing=1
        fi
    done
    
    if [ $missing -eq 0 ]; then
        echo "✓ 所有模块文件完整"
    fi
}

switch_to_old() {
    echo ""
    echo "恢复到旧版本..."
    
    if [ ! -f "${SRC_DIR}/proto_bacnet_core_old.cpp" ]; then
        echo "✗ 错误: proto_bacnet_core_old.cpp 不存在，无法恢复"
        return 1
    fi
    
    cp "${SRC_DIR}/proto_bacnet_core_old.cpp" "${SRC_DIR}/proto_bacnet_core.cpp"
    echo "✓ 已恢复到旧版本"
}

clean_backup() {
    echo ""
    echo "清理备份文件..."
    if [ -f "${SRC_DIR}/proto_bacnet_core_old.cpp" ]; then
        rm -f "${SRC_DIR}/proto_bacnet_core_old.cpp"
        echo "✓ 已删除 proto_bacnet_core_old.cpp"
    fi
    if [ -f "${SRC_DIR}/proto_bacnet_core_new.cpp" ]; then
        rm -f "${SRC_DIR}/proto_bacnet_core_new.cpp"
        echo "✓ 已删除 proto_bacnet_core_new.cpp"
    fi
}

build_project() {
    echo ""
    echo "编译项目..."
    cd "${SCRIPT_DIR}"
    
    if [ -d "build" ]; then
        echo "清理旧的构建目录..."
        rm -rf build
    fi
    
    bash build.sh debug
    
    if [ $? -eq 0 ]; then
        echo "✓ 编译成功"
        return 0
    else
        echo "✗ 编译失败"
        return 1
    fi
}

# 主菜单
show_menu() {
    show_status
    echo ""
    echo "请选择操作:"
    echo "  1) 切换到新版本 (重构版)"
    echo "  2) 恢复到旧版本"
    echo "  3) 仅备份当前版本"
    echo "  4) 切换到新版本并编译"
    echo "  5) 清理备份文件"
    echo "  6) 查看状态"
    echo "  q) 退出"
    echo ""
    read -p "请输入选择 [1-6/q]: " choice
    
    case $choice in
        1)
            switch_to_new
            ;;
        2)
            switch_to_old
            ;;
        3)
            backup_old
            ;;
        4)
            switch_to_new && build_project
            ;;
        5)
            clean_backup
            ;;
        6)
            show_status
            ;;
        q|Q)
            echo "退出"
            exit 0
            ;;
        *)
            echo "无效选择"
            ;;
    esac
    
    echo ""
    read -p "按 Enter 继续..."
}

# 如果有命令行参数，直接执行
if [ $# -gt 0 ]; then
    case "$1" in
        new)
            switch_to_new
            ;;
        old)
            switch_to_old
            ;;
        backup)
            backup_old
            ;;
        status)
            show_status
            ;;
        build)
            build_project
            ;;
        clean)
            clean_backup
            ;;
        *)
            echo "用法: $0 [new|old|backup|status|build|clean]"
            echo ""
            echo "命令说明:"
            echo "  new     - 切换到新版本"
            echo "  old     - 恢复到旧版本"
            echo "  backup  - 备份当前版本"
            echo "  status  - 查看状态"
            echo "  build   - 编译项目"
            echo "  clean   - 清理备份文件"
            exit 1
            ;;
    esac
else
    # 交互式菜单
    while true; do
        clear
        show_menu
    done
fi
