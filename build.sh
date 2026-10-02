#!/usr/bin/env bash
# gomd 构建脚本
#
# 关于 -mwindows：那是 MinGW/gcc 的链接参数，作用是把 PE 子系统设为 GUI
# （启动时不带控制台黑窗口）。Go 的等价参数是 -ldflags="-H windowsgui"，
# 本脚本使用后者，效果完全一致。
set -e
cd "$(dirname "$0")"

# windres：优先用 PATH 里的，否则退回 MSYS2/UCRT64 常见安装位置
WINDRES=windres
if ! command -v windres >/dev/null 2>&1; then
  WINDRES=/d/msys64/ucrt64/bin/windres.exe
fi

echo "[1/3] 编译资源（图标 + manifest）-> src/gomd.syso"
(cd resources && "$WINDRES" gomd.rc -O coff -o ../src/gomd.syso)

echo "[2/3] 编译 gomd.exe（GUI 子系统，无控制台）"
go build -trimpath -ldflags="-s -w -H windowsgui" -o dist/gomd.exe ./src

echo "[3/3] 复制示例文档"
cp docs/sample.md dist/

echo "构建完成: dist/gomd.exe"
