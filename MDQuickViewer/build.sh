#!/usr/bin/env bash
# MDQuickViewer 构建脚本（MSYS2 UCRT64）
#
# 与 Go 版 build.sh 的对应关系：
#   -mwindows  == Go 的 -ldflags="-H windowsgui"（GUI 子系统，无控制台黑框）
#   windres     == Go 里的 go:embed syso 效果（图标 + manifest）
#
# 用法：
#   ./build.sh          构建
#   ./build.sh test     构建并跑解析器自检
set -e
cd "$(dirname "$0")"

export PATH="/d/msys64/ucrt64/bin:$PATH"

CC=${CC:-gcc}
CFLAGS="-O2 -Wall -Wextra -Wno-unused-parameter -std=c11 -D_WIN32_WINNT=0x0601 -D_WIN32_IE=0x0600"
# -municode: 入口用 wWinMain（宽字符命令行，省去手工转 argv）
# -mwindows:  PE 子系统设为 GUI
LDFLAGS="-municode -mwindows"
# advapi32: settings.c 用注册表 API（RegOpenKeyExW / RegQueryValueExW / RegSetValueExW ...）
# 之前漏了它，gcc 靠符号解析侥幸链接成功；MSVC 侧会直接报 LNK2019 未解析符号。
LIBS="-lcomctl32 -lgdi32 -luser32 -lshell32 -lshlwapi -lcomdlg32 -lole32 -luuid -ladvapi32"

mkdir -p build

echo "[1/5] 编译资源（图标 + manifest）"
windres resources/mdqv.rc -O coff -I resources -o build/mdqv.res.o

echo "[2/5] 检查注释里的裸 C 注释终止符"
# 注释里写 "/*" 会提前闭合注释，编译器报的却是 "missing terminating \""
# 这种与真实原因无关的错误。这里只匹配"注释续行（ * 后跟空格）里又出现 /*"的情况，
# 避免把 "*cp = c; /* ... */" 这种正常代码误判。
BAD=$(grep -rn --include=*.c --include=*.h -E '^\s*\*[ \t].*/\*' src/ || true)
if [ -n "$BAD" ]; then
    echo "警告：以下注释续行含 /* 字面量，会提前闭合注释："
    echo "$BAD"
    echo "提示：改写成 slash-star 之类的文字描述。"
fi

echo "[3/5] 编译 MDQuickViewer.exe"
# shellcheck disable=SC2086
$CC $CFLAGS $LDFLAGS -o build/MDQuickViewer.exe \
    src/main.c src/win32.c src/markdown.c src/markdown_test.c \
    src/highlight.c src/highlight_test.c src/render.c src/render_test.c \
    src/settings.c src/dlg.c src/ui.c \
    build/mdqv.res.o $LIBS

echo "[4/5] 复制 sample.md 到产物目录（无参数启动回退）"
cp sample.md build/sample.md 2>/dev/null || true

echo "[5/5] 完成"
ls -la build/MDQuickViewer.exe

if [ "$1" = "test" ]; then
    echo ""
    echo "=== 运行自检 ==="
    LOG="$TEMP/MDQuickViewer-test.log"
    [ -f "$LOG" ] || LOG="/tmp/MDQuickViewer-test.log"
    rm -f "$LOG" 2>/dev/null || true
    # GUI 子系统的 stdout 未必接到调用方管道，测试结果一律走日志文件。
    # stdout 上已经打过的内容会重复一遍，这里只取日志，且过滤掉重复行。
    ./build/MDQuickViewer.exe --test >/dev/null 2>&1
    RC=$?
    if [ -f "$LOG" ]; then
        cat "$LOG"
    else
        echo "(未找到测试日志，退出码 $RC)"
    fi
    exit $RC
fi
