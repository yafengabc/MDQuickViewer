package main

import (
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"syscall"
	"unicode/utf16"
	"unsafe"

	"golang.org/x/sys/windows"
)

// 自绘"打开文件"对话框，替代不稳定的 GetOpenFileNameW 通用对话框。
// 仅列出当前文件夹下的 .md/.markdown 文件，用户双击或点"打开"加载。

const classNameOpenDlg = "GomdOpenDlg"

const (
	idcDlgStatic = 2100
	idcDlgList   = 2101
	idcDlgUp     = 2102
	idcDlgBrowse = 2103
	idcDlgOpen   = 2104
	idcDlgCancel = 2105
)

var (
	dlgHwnd   HWND
	dlgList   HWND
	dlgStatic HWND
	dlgFolder string
)

// openFileDialog 打开标准的"打开文件"对话框（GetOpenFileNameW，即 OpenFileDialog）。
// 若调用真实失败（非用户取消），回退到自绘对话框兜底。
func openFileDialog() {
	dlgFolder = initialFolder()
	filter := utf16DoubleNul([]string{
		"Markdown 文件 (*.md;*.markdown)", "*.md;*.markdown",
		"所有文件 (*.*)", "*.*",
	})
	var buf [32768]uint16
	var ofn OPENFILENAME
	ofn.LStructSize = uint32(unsafe.Sizeof(ofn))
	ofn.HwndOwner = app.mainWnd
	ofn.HInstance = hInst
	ofn.LpstrFilter = &filter[0]
	ofn.LpstrFile = &buf[0]
	ofn.NMaxFile = uint32(len(buf))
	ofn.LpstrInitialDir = windows.StringToUTF16Ptr(dlgFolder)
	ofn.LpstrTitle = windows.StringToUTF16Ptr("打开 Markdown 文件")
	ofn.LpstrDefExt = windows.StringToUTF16Ptr("md")
	ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
		OFN_HIDEREADONLY | OFN_ENABLESIZING | OFN_NOCHANGEDIR

	if getOpenFileName(&ofn) {
		path := windows.UTF16ToString(buf[:])
		if path != "" {
			rememberFolder(filepath.Dir(path))
			populateList(dlgFolder)
			loadFile(path)
		}
		return
	}
	// 返回 FALSE：0 = 用户取消，静默即可；非 0 = 真实错误，回退自绘对话框
	if code := commDlgExtendedError(); code != 0 {
		fallbackOpenFileDialog()
	}
}

// utf16DoubleNul 构造通用对话框所需的"多字符串"（每段 NUL 结尾，整体双 NUL 结尾）。
func utf16DoubleNul(parts []string) []uint16 {
	u := make([]uint16, 0, 64)
	for _, p := range parts {
		u = append(u, utf16.Encode([]rune(p))...)
		u = append(u, 0)
	}
	return append(u, 0)
}

// fallbackOpenFileDialog 自绘"打开文件"对话框（仅在本机 GetOpenFileNameW 不可用时兜底）。
func fallbackOpenFileDialog() {
	if dlgHwnd != 0 {
		setForegroundWindow(dlgHwnd)
		return
	}
	// 居中于主窗口上方
	mr, _ := getWindowRect(app.mainWnd)
	w, h := int32(560), int32(440)
	x := (mr.Left + (mr.Right-mr.Left)/2) - w/2
	y := (mr.Top + (mr.Bottom-mr.Top)/2) - h/2
	dlgHwnd = createWindowEx(WS_EX_DLGMODALFRAME|WS_EX_TOPMOST,
		windows.StringToUTF16Ptr(classNameOpenDlg),
		windows.StringToUTF16Ptr("打开 Markdown 文件"),
		WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_VISIBLE,
		x, y, w, h, app.mainWnd, 0, hInst, 0)
	if dlgHwnd == 0 {
		messageBox(app.mainWnd, "无法创建打开对话框。", "gomd", MB_ICONERROR)
	}
}

func dlgPopulate() {
	if dlgList == 0 {
		return
	}
	sendMessage(dlgList, LB_RESETCONTENT, 0, 0)
	if dlgStatic != 0 {
		setWindowText(dlgStatic, "文件夹："+dlgFolder)
	}
	entries, err := os.ReadDir(dlgFolder)
	if err != nil {
		return
	}
	for _, e := range entries {
		if e.IsDir() {
			continue
		}
		name := e.Name()
		lower := strings.ToLower(name)
		if !strings.HasSuffix(lower, ".md") && !strings.HasSuffix(lower, ".markdown") {
			continue
		}
		p := windows.StringToUTF16Ptr(name)
		sendMessage(dlgList, LB_ADDSTRING, 0, uintptr(unsafe.Pointer(p)))
		runtime.KeepAlive(p)
	}
}

func dlgOpenSelected() {
	if dlgList == 0 {
		return
	}
	cur := int32(sendMessage(dlgList, LB_GETCURSEL, 0, 0))
	if cur < 0 {
		messageBox(dlgHwnd, "请先在列表中选择一个文件。", "gomd", MB_ICONINFORMATION)
		return
	}
	n := int32(sendMessage(dlgList, LB_GETTEXTLEN, uintptr(cur), 0))
	if n <= 0 {
		return
	}
	buf := make([]uint16, n+1)
	sendMessage(dlgList, LB_GETTEXT, uintptr(cur), uintptr(unsafe.Pointer(&buf[0])))
	runtime.KeepAlive(buf)
	name := windows.UTF16ToString(buf)
	path := filepath.Join(dlgFolder, name)
	rememberFolder(dlgFolder)
	populateList(dlgFolder)
	loadFile(path)
	dlgClose()
}

func dlgClose() {
	if dlgHwnd != 0 {
		destroyWindow(dlgHwnd)
	}
}

func dlgWndProc(hwnd, msg, wParam, lParam uintptr) uintptr {
	switch msg {
	case WM_CREATE:
		dlgStatic = createWindowEx(0, windows.StringToUTF16Ptr("STATIC"),
			windows.StringToUTF16Ptr("文件夹："),
			WS_CHILD|WS_VISIBLE|SS_LEFT, 0, 0, 0, 0, windows.HWND(hwnd), idcDlgStatic, hInst, 0)
		dlgList = createWindowEx(WS_EX_CLIENTEDGE,
			windows.StringToUTF16Ptr("LISTBOX"),
			nil,
			WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_TABSTOP|LBS_NOTIFY|LBS_HASSTRINGS|LBS_SORT,
			0, 0, 0, 0, windows.HWND(hwnd), idcDlgList, hInst, 0)
		createWindowEx(0, windows.StringToUTF16Ptr("BUTTON"),
			windows.StringToUTF16Ptr("上级目录"),
			WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 0, 0, 0, 0, windows.HWND(hwnd), idcDlgUp, hInst, 0)
		createWindowEx(0, windows.StringToUTF16Ptr("BUTTON"),
			windows.StringToUTF16Ptr("浏览文件夹..."),
			WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 0, 0, 0, 0, windows.HWND(hwnd), idcDlgBrowse, hInst, 0)
		createWindowEx(0, windows.StringToUTF16Ptr("BUTTON"),
			windows.StringToUTF16Ptr("打开"),
			WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON, 0, 0, 0, 0, windows.HWND(hwnd), idcDlgOpen, hInst, 0)
		createWindowEx(0, windows.StringToUTF16Ptr("BUTTON"),
			windows.StringToUTF16Ptr("取消"),
			WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 0, 0, 0, 0, windows.HWND(hwnd), idcDlgCancel, hInst, 0)
		dlgPopulate()
		return 0
	case WM_SIZE:
		// 注意：WM_SIZE 的尺寸在 lParam（低16位=宽，高16位=高），wParam 只是标志位。
		cx := int32(int16(uint16(lParam & 0xffff)))
		cy := int32(int16(uint16(lParam >> 16)))
		const pad int32 = 12
		const bh int32 = 28
		const sideW int32 = 120
		// 顶部 static
		moveWindow(dlgStatic, pad, pad, cx-2*pad, 22, true)
		// 列表框占满中间
		listY := pad + 28
		listH := cy - listY - bh - 3*pad
		if listH < 60 {
			listH = 60
		}
		moveWindow(dlgList, pad, listY, cx-2*pad, listH, true)
		// 底部按钮行
		by := cy - pad - bh
		// 取消 / 打开 放在右下
		openX := cx - pad - sideW
		cancelX := openX - pad - sideW
		moveWindow(getDlgItem(windows.HWND(hwnd), idcDlgCancel), cancelX, by, sideW, bh, true)
		moveWindow(getDlgItem(windows.HWND(hwnd), idcDlgOpen), openX, by, sideW, bh, true)
		// 左上：上级目录 / 浏览
		moveWindow(getDlgItem(windows.HWND(hwnd), idcDlgUp), pad, by, sideW, bh, true)
		moveWindow(getDlgItem(windows.HWND(hwnd), idcDlgBrowse), pad+sideW+pad, by, sideW+40, bh, true)
		return 0
	case WM_COMMAND:
		code := uint16(wParam >> 16)
		id := wParam & 0xffff
		switch id {
		case idcDlgOpen:
			dlgOpenSelected()
			return 0
		case idcDlgCancel:
			dlgClose()
			return 0
		case idcDlgUp:
			parent := filepath.Dir(dlgFolder)
			if parent != dlgFolder {
				rememberFolder(parent)
				dlgPopulate()
			}
			return 0
		case idcDlgBrowse:
			if f := browseFolder(dlgHwnd, "选择包含 Markdown 的文件夹"); f != "" {
				rememberFolder(f)
				dlgPopulate()
			}
			return 0
		case idcDlgList:
			if code == LBN_DBLCLK {
				dlgOpenSelected()
				return 0
			}
		}
		return 0
	case WM_KEYDOWN:
		if int(wParam) == VK_ESCAPE {
			dlgClose()
			return 0
		}
		return 0
	case WM_CLOSE:
		dlgClose()
		return 0
	case WM_DESTROY:
		dlgHwnd = 0
		dlgList = 0
		dlgStatic = 0
		return 0
	}
	return defWindowProc(windows.HWND(hwnd), uint32(msg), wParam, lParam)
}

// registerOpenDlgClass 在 main 中调用，注册对话框窗口类。
func registerOpenDlgClass() {
	var wc WNDCLASSEX
	wc.CbSize = uint32(unsafe.Sizeof(wc))
	wc.Style = CS_HREDRAW | CS_VREDRAW
	wc.LpfnWndProc = syscall.NewCallback(dlgWndProc)
	wc.HInstance = hInst
	wc.HCursor = loadCursor(0, IDC_ARROW)
	wc.HbrBackground = HBRUSH(COLOR_BTNFACE + 1)
	wc.LpszClassName = windows.StringToUTF16Ptr(classNameOpenDlg)
	registerClassEx(&wc)
}
