package main

import (
	"os"
	"testing"
	"time"
	"unsafe"

	"golang.org/x/sys/windows"
)

func TestUTF16Filter(t *testing.T) {
	filter := "Markdown 文件\000*.md;*.markdown\000所有文件\000*.*\000"
	p, err := windows.UTF16PtrFromString(filter)
	t.Logf("UTF16PtrFromString(filter) -> p=%v err=%v", p, err)
	if p == nil {
		t.Logf("=> LpstrFilter 会变成 nil（每个子串之间用 \\\\0 分隔的写法无效）")
	}
}

func TestOpenFileDialogSandbox(t *testing.T) {
	// 该测试会真的弹出一个模态"打开文件"对话框，且 GetOpenFileName 即使带
	// OFN_NOCHANGEDIR 也可能改变进程工作目录（Windows 已知行为），
	// 因此默认跳过，仅在设置 GOMD_DIALOG_TEST=1 时手动运行。
	if os.Getenv("GOMD_DIALOG_TEST") == "" {
		t.Skip("弹出真实对话框且可能改变工作目录，默认跳过（GOMD_DIALOG_TEST=1 启用）")
	}
	measDC = getDC(0)
	var ofn OPENFILENAME
	ofn.LStructSize = uint32(unsafe.Sizeof(ofn))
	ofn.HwndOwner = 0
	buf := make([]uint16, 1024)
	ofn.LpstrFile = &buf[0]
	ofn.NMaxFile = 1024
	ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR
	done := make(chan bool, 1)
	go func() {
		defer func() {
			if r := recover(); r != nil {
				t.Logf("PANIC in getOpenFileName: %v", r)
				done <- false
			}
		}()
		done <- getOpenFileName(&ofn)
	}()
	select {
	case r := <-done:
		t.Logf("getOpenFileName returned: %v (false=对话框未弹出/被取消; 沙箱通常如此)", r)
	case <-time.After(3 * time.Second):
		t.Logf("getOpenFileName 阻塞 3s 未返回（沙箱无交互桌面，无法继续验证）")
	}
}
