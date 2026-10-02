package main

import (
	"fmt"
	"syscall"
	"testing"
	"unsafe"

	"golang.org/x/sys/windows"
)

// TestToolbarCreate 复刻 createControls() 里工具栏的创建流程，
// 用控制台打印逐步定位崩溃点（go test 是控制台程序，崩溃会打印栈）。
func TestToolbarCreate(t *testing.T) {
	var icc INITCOMMONCONTROLSEX
	icc.DwSize = uint32(unsafe.Sizeof(icc))
	icc.DwICC = ICC_BAR_CLASSES
	if !initCommonControlsEx(&icc) {
		t.Fatal("initCommonControlsEx failed")
	}
	hInst := getModuleHandle()

	// 父窗口
	var wc WNDCLASSEX
	wc.CbSize = uint32(unsafe.Sizeof(wc))
	wc.Style = CS_HREDRAW | CS_VREDRAW
	wc.LpfnWndProc = syscall.NewCallback(func(hwnd uintptr, msg uint32, wp, lp uintptr) uintptr {
		return defWindowProc(windows.HWND(hwnd), msg, wp, lp)
	})
	wc.HInstance = hInst
	wc.LpszClassName = windows.StringToUTF16Ptr("TestParent")
	if _, err := registerClassEx(&wc); err != nil {
		t.Fatal("register parent class:", err)
	}
	parent := createWindowEx(0, windows.StringToUTF16Ptr("TestParent"), nil,
		WS_OVERLAPPEDWINDOW, 100, 100, 400, 300, 0, 0, hInst, 0)
	fmt.Println("parent =", parent)
	if parent == 0 {
		t.Fatal("parent create failed")
	}

	fmt.Printf("TBBUTTON size = %d (期望 32)\n", unsafe.Sizeof(TBBUTTON{}))
	fmt.Printf("TBBUTTON offsets: IBitmap=%d IdCommand=%d FsState=%d FsStyle=%d BReserved=%d DwData=%d IString=%d\n",
		unsafe.Offsetof(TBBUTTON{}.IBitmap),
		unsafe.Offsetof(TBBUTTON{}.IdCommand),
		unsafe.Offsetof(TBBUTTON{}.FsState),
		unsafe.Offsetof(TBBUTTON{}.FsStyle),
		unsafe.Offsetof(TBBUTTON{}.BReserved),
		unsafe.Offsetof(TBBUTTON{}.DwData),
		unsafe.Offsetof(TBBUTTON{}.IString))

	tb := createWindowEx(0, windows.StringToUTF16Ptr(TOOLBARCLASSNAME), nil,
		WS_CHILD|WS_VISIBLE|TBSTYLE_FLAT|TBSTYLE_LIST|TBSTYLE_TOOLTIPS,
		0, 0, 0, 0, parent, 0, hInst, 0)
	fmt.Println("toolbar =", tb)
	if tb == 0 {
		t.Fatal("toolbar create failed")
	}

	sendMessage(tb, TB_BUTTONSTRUCTSIZE, uintptr(unsafe.Sizeof(TBBUTTON{})), 0)
	fmt.Println("TB_BUTTONSTRUCTSIZE ok")

	strs := "打开文件\000打开文件夹\000放大\000缩小\000重置\000关于\000"
	_ = strs
	sp := toolbarStringPool("打开文件", "打开文件夹", "放大", "缩小", "重置", "关于")
	first := int32(sendMessage(tb, TB_ADDSTRINGW, 0, uintptr(unsafe.Pointer(sp))))
	fmt.Println("TB_ADDSTRINGW first =", first)

	buttons := []TBBUTTON{
		{IBitmap: I_IMAGENONE, IdCommand: IDM_OPENFILE, FsState: TBSTATE_ENABLED, FsStyle: BTNS_BUTTON | BTNS_AUTOSIZE, IString: first + 0},
		{IBitmap: I_IMAGENONE, IdCommand: IDM_OPENFOLDER, FsState: TBSTATE_ENABLED, FsStyle: BTNS_BUTTON | BTNS_AUTOSIZE, IString: first + 1},
		{IBitmap: 0, IdCommand: 0, FsStyle: BTNS_SEP},
		{IBitmap: I_IMAGENONE, IdCommand: IDM_ZOOMIN, FsState: TBSTATE_ENABLED, FsStyle: BTNS_BUTTON | BTNS_AUTOSIZE, IString: first + 2},
	}
	fmt.Println("准备发送 TB_ADDBUTTONSW ...")
	r := sendMessage(tb, TB_ADDBUTTONSW, uintptr(len(buttons)), uintptr(unsafe.Pointer(&buttons[0])))
	fmt.Println("TB_ADDBUTTONSW result =", r)
	sendMessage(tb, TB_AUTOSIZE, 0, 0)
	fmt.Println("TB_AUTOSIZE ok")
	fmt.Println("OK: 工具栏创建流程未崩溃")
}
