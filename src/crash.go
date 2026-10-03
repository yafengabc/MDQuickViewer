package main

import (
	"fmt"
	"os"
	"path/filepath"
	"runtime/debug"
	"strings"
	"time"
)

// crashLogName 崩溃日志文件名（放在临时目录，程序退出后仍可查看）。
const crashLogName = "gomd-crash.log"

func crashLogPath() string {
	return filepath.Join(os.TempDir(), crashLogName)
}

// guardPanic 挂在窗口过程上：Go 的 panic 若逃出 wndProc，进程会直接退出，
// 而 GUI 子系统没有控制台，用户只看到窗口凭空消失。这里把堆栈落盘后再返回，
// 既保留现场也避免"窗口一碰就没"。
func guardPanic(where string) {
	if r := recover(); r != nil {
		writeCrashLog(where, r, debug.Stack())
	}
}

func writeCrashLog(where string, r interface{}, stack []byte) {
	var sb strings.Builder
	sb.WriteString("==== gomd 崩溃 ")
	sb.WriteString(time.Now().Format("2006-01-02 15:04:05"))
	sb.WriteString(" ====\n")
	sb.WriteString("位置: ")
	sb.WriteString(where)
	sb.WriteString("\n错误: ")
	fmt.Fprintf(&sb, "%v\n\n", r)
	sb.WriteString("---- goroutine 堆栈 ----\n")
	sb.Write(stack)
	if !strings.HasSuffix(string(stack), "\n") {
		sb.WriteString("\n")
	}

	path := crashLogPath()
	// 追加写，保留历史现场
	f, err := os.OpenFile(path, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644)
	if err == nil {
		_, _ = f.WriteString(sb.String())
		_ = f.Close()
	}
	// 同时弹窗告知用户日志位置
	messageBox(0, "gomd 遇到内部错误，已记录到：\n"+path, "gomd - 内部错误", 0x10)
}
