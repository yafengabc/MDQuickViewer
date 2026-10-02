//go:build windows

// scap 是一个临时的开发辅助工具：启动 gomd、把窗口/屏幕抓成 PNG，
// 便于在无交互桌面的环境里目视检查界面（工具栏图标、表格渲染、自绘打开对话框等）。
package main

import (
	"errors"
	"fmt"
	"image"
	"image/color"
	"image/png"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"syscall"
	"time"
	"unicode/utf16"
	"unsafe"

	"golang.org/x/sys/windows"
)

var (
	user32 = windows.NewLazySystemDLL("user32.dll")
	gdi32  = windows.NewLazySystemDLL("gdi32.dll")

	pFindWindowW         = user32.NewProc("FindWindowW")
	pFindWindowExW       = user32.NewProc("FindWindowExW")
	pPostMessage         = user32.NewProc("PostMessageW")
	pEnumWindows         = user32.NewProc("EnumWindows")
	pGetWindowThreadProc = user32.NewProc("GetWindowThreadProcessId")
	pIsWindowVisible     = user32.NewProc("IsWindowVisible")
	pGetClassNameW       = user32.NewProc("GetClassNameW")
	pGetWindowTextW      = user32.NewProc("GetWindowTextW")
	pSetForegroundWindow = user32.NewProc("SetForegroundWindow")
	pShowWindow          = user32.NewProc("ShowWindow")
	pSetWindowPos        = user32.NewProc("SetWindowPos")
	pGetWindowRect       = user32.NewProc("GetWindowRect")
	pGetDC               = user32.NewProc("GetDC")
	pReleaseDC           = user32.NewProc("ReleaseDC")
	pGetDeviceCaps       = gdi32.NewProc("GetDeviceCaps")
	pCreateCompatibleDC  = gdi32.NewProc("CreateCompatibleDC")
	pCreateDIBSection    = gdi32.NewProc("CreateDIBSection")
	pSelectObject        = gdi32.NewProc("SelectObject")
	pBitBlt              = gdi32.NewProc("BitBlt")
	pDeleteObject        = gdi32.NewProc("DeleteObject")
	pDeleteDC            = gdi32.NewProc("DeleteDC")
)

type RECT struct{ Left, Top, Right, Bottom int32 }

type BITMAPINFOHEADER struct {
	BiSize          uint32
	BiWidth         int32
	BiHeight        int32
	BiPlanes        uint16
	BiBitCount      uint16
	BiCompression   uint32
	BiSizeImage     uint32
	BiXPelsPerMeter int32
	BiYPelsPerMeter int32
	BiClrUsed       uint32
	BiClrImportant  uint32
}

type BITMAPINFO struct {
	Hdr    BITMAPINFOHEADER
	Colors [4]byte
}

const SRCCOPY = 0x00CC0020

func findWindow(cls string) windows.HWND {
	p := windows.StringToUTF16Ptr(cls)
	h, _, _ := pFindWindowW.Call(uintptr(unsafe.Pointer(p)), 0)
	return windows.HWND(h)
}

// findChildEx 在 parent 下按类名找子窗口（预览区 GomdPreview 是主窗口的子窗口）
func findChildEx(parent windows.HWND, cls string) windows.HWND {
	p := windows.StringToUTF16Ptr(cls)
	h, _, _ := pFindWindowExW.Call(uintptr(parent), 0, uintptr(unsafe.Pointer(p)), 0)
	return windows.HWND(h)
}

func getWindowRect(h windows.HWND) (RECT, error) {
	var r RECT
	ok, _, _ := pGetWindowRect.Call(uintptr(h), uintptr(unsafe.Pointer(&r)))
	if ok == 0 {
		return r, errors.New("GetWindowRect failed")
	}
	return r, nil
}

func screenSize() (int32, int32) {
	hdc, _, _ := pGetDC.Call(0)
	if hdc == 0 {
		return 1920, 1080
	}
	w, _, _ := pGetDeviceCaps.Call(hdc, 8) // HORZRES
	h, _, _ := pGetDeviceCaps.Call(hdc, 10)
	pReleaseDC.Call(0, hdc)
	return int32(w), int32(h)
}

// capture 把屏幕上的 (x,y,w,h) 区域抓成 PNG
func capture(x, y, w, h int32, path string) error {
	if w <= 0 || h <= 0 {
		return fmt.Errorf("bad rect %dx%d", w, h)
	}
	hdcScreen, _, _ := pGetDC.Call(0)
	if hdcScreen == 0 {
		return errors.New("GetDC(0) failed")
	}
	defer pReleaseDC.Call(0, hdcScreen)

	hdcMem, _, _ := pCreateCompatibleDC.Call(hdcScreen)
	if hdcMem == 0 {
		return errors.New("CreateCompatibleDC failed")
	}
	defer pDeleteDC.Call(hdcMem)

	var bmi BITMAPINFO
	bmi.Hdr.BiSize = uint32(unsafe.Sizeof(bmi.Hdr))
	bmi.Hdr.BiWidth = w
	bmi.Hdr.BiHeight = -h // top-down
	bmi.Hdr.BiPlanes = 1
	bmi.Hdr.BiBitCount = 32
	bmi.Hdr.BiCompression = 0

	var bits unsafe.Pointer
	hbm, _, _ := pCreateDIBSection.Call(hdcScreen, uintptr(unsafe.Pointer(&bmi)),
		0, uintptr(unsafe.Pointer(&bits)), 0, 0)
	if hbm == 0 || bits == nil {
		return errors.New("CreateDIBSection failed")
	}
	defer pDeleteObject.Call(hbm)
	pSelectObject.Call(hdcMem, hbm)

	r, _, _ := pBitBlt.Call(hdcMem, 0, 0, uintptr(w), uintptr(h),
		hdcScreen, uintptr(x), uintptr(y), uintptr(SRCCOPY))
	if r == 0 {
		return errors.New("BitBlt failed")
	}

	data := unsafe.Slice((*byte)(bits), int(w)*int(h)*4)
	img := image.NewRGBA(image.Rect(0, 0, int(w), int(h)))
	for yy := 0; yy < int(h); yy++ {
		row := yy * int(w)
		for xx := 0; xx < int(w); xx++ {
			i := (row + xx) * 4
			img.SetRGBA(xx, yy, color.RGBA{R: data[i+2], G: data[i+1], B: data[i], A: 255})
		}
	}
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer f.Close()
	return png.Encode(f, img)
}

func raise(h windows.HWND) {
	pShowWindow.Call(uintptr(h), 9) // SW_RESTORE
	pSetWindowPos.Call(uintptr(h), 0, 0, 0, 0, 0, 0x0043)
	pSetForegroundWindow.Call(uintptr(h))
}

var (
	enumPid     uint32
	enumResults []string
)

// listProcessWindows 枚举指定进程的所有顶层窗口（类名 | 标题 | 可见 | 位置）
func listProcessWindows(pid uint32) []string {
	enumPid = pid
	enumResults = nil
	cb := syscall.NewCallback(func(hwnd windows.HWND, lparam uintptr) uintptr {
		var outPid uint32
		pGetWindowThreadProc.Call(uintptr(hwnd), uintptr(unsafe.Pointer(&outPid)))
		if outPid != enumPid {
			return 1
		}
		var buf [256]uint16
		pGetClassNameW.Call(uintptr(hwnd), uintptr(unsafe.Pointer(&buf[0])), 256)
		cls := windows.UTF16ToString(buf[:])
		var tbuf [256]uint16
		pGetWindowTextW.Call(uintptr(hwnd), uintptr(unsafe.Pointer(&tbuf[0])), 256)
		title := windows.UTF16ToString(tbuf[:])
		vis, _, _ := pIsWindowVisible.Call(uintptr(hwnd))
		r, err := getWindowRect(hwnd)
		rect := "err"
		if err == nil {
			rect = fmt.Sprintf("(%d,%d)-(%d,%d)", r.Left, r.Top, r.Right, r.Bottom)
		}
		enumResults = append(enumResults, fmt.Sprintf("  hwnd=%d cls=%q title=%q visible=%d rect=%s",
			uintptr(hwnd), cls, title, vis&1, rect))
		return 1
	})
	pEnumWindows.Call(cb, 0)
	return enumResults
}

var (
	kernel32      = windows.NewLazySystemDLL("kernel32.dll")
	pGlobalAlloc  = kernel32.NewProc("GlobalAlloc")
	pGlobalLock   = kernel32.NewProc("GlobalLock")
	pGlobalUnlock = kernel32.NewProc("GlobalUnlock")

	pOpenClipboard    = user32.NewProc("OpenClipboard")
	pCloseClipboard   = user32.NewProc("CloseClipboard")
	pGetClipboardData = user32.NewProc("GetClipboardData")
)

// clipboardText 读取剪贴板里的 CF_UNICODETEXT 文本。
func clipboardText() string {
	const CF_UNICODETEXT = 13
	if r, _, _ := pOpenClipboard.Call(0); r == 0 {
		return "<打开剪贴板失败>"
	}
	defer pCloseClipboard.Call()
	h, _, _ := pGetClipboardData.Call(CF_UNICODETEXT)
	if h == 0 {
		return "<剪贴板无文本>"
	}
	p, _, _ := pGlobalLock.Call(h)
	if p == 0 {
		return "<GlobalLock 失败>"
	}
	defer pGlobalUnlock.Call(h)
	// 以 NUL 结尾的 UTF-16
	n := 0
	for {
		c := *(*uint16)(unsafe.Pointer(uintptr(p) + uintptr(n*2)))
		if c == 0 {
			break
		}
		n++
	}
	slice := unsafe.Slice((*uint16)(unsafe.Pointer(p)), n)
	return windows.UTF16ToString(slice)
}

// dragSelect 在 preview 上模拟一次鼠标拖选（按下→移动→松开）。
func dragSelect(preview windows.HWND, x0, y0, x1, y1 int32) {
	mk := func(x, y int32) uintptr { return uintptr(uint32(uint16(x)) | uint32(uint16(y))<<16) }
	pPostMessage.Call(uintptr(preview), 0x0201, 1, mk(x0, y0)) // WM_LBUTTONDOWN
	time.Sleep(200 * time.Millisecond)
	steps := 6
	for i := 1; i <= steps; i++ {
		x := x0 + (x1-x0)*int32(i)/int32(steps)
		y := y0 + (y1-y0)*int32(i)/int32(steps)
		pPostMessage.Call(uintptr(preview), 0x0200, 1, mk(x, y)) // WM_MOUSEMOVE
		time.Sleep(60 * time.Millisecond)
	}
	pPostMessage.Call(uintptr(preview), 0x0202, 0, mk(x1, y1)) // WM_LBUTTONUP
	time.Sleep(400 * time.Millisecond)
}

type POINT struct{ X, Y int32 }

// DROPFILES 结构 + UTF-16 文件名列表（双 NUL 结尾），放进 GMEM_SHARE 全局内存，
// 模拟一次拖放。返回 HDROP 句柄（由接收方 DragFinish 释放）。
func makeHDrop(files ...string) uintptr {
	const GMEM_MOVEABLE = 0x0002
	const GMEM_SHARE = 0x2000

	var df struct {
		PFiles uint32
		Pt     POINT
		FNC    uint32
		FWide  uint32
	}
	df.PFiles = 20 // DROPFILES 结构体大小，文件名列表紧随其后
	df.FWide = 1   // 文件名是 UTF-16

	payload := make([]byte, 0, 64)
	payload = append(payload, (*[20]byte)(unsafe.Pointer(&df))[:]...)
	for _, f := range files {
		for _, c := range utf16.Encode([]rune(f)) {
			payload = append(payload, byte(c), byte(c>>8))
		}
		payload = append(payload, 0, 0) // 每个名字 NUL 结尾
	}
	payload = append(payload, 0, 0) // 列表双 NUL 结尾

	h, _, _ := pGlobalAlloc.Call(GMEM_MOVEABLE|GMEM_SHARE, uintptr(len(payload)))
	if h == 0 {
		return 0
	}
	p, _, _ := pGlobalLock.Call(h)
	if p == 0 {
		return 0
	}
	copy(unsafe.Slice((*byte)(unsafe.Pointer(p)), len(payload)), payload)
	pGlobalUnlock.Call(h)
	return h
}

func main() {
	exe := `D:\Projects\gomd\dist\gomd.exe`
	outDir := `D:\Projects\gomd\dist`
	cmd := exec.Command(exe)
	cmd.Dir = outDir
	if err := cmd.Start(); err != nil {
		fmt.Println("启动失败:", err)
		return
	}
	fmt.Println("pid:", cmd.Process.Pid)
	time.Sleep(4 * time.Second)

	main := findWindow("GomdMainWindow")
	fmt.Println("主窗口 hwnd:", main)
	if main == 0 {
		fmt.Println("找不到主窗口")
		return
	}
	raise(main)
	time.Sleep(2 * time.Second)

	if r, err := getWindowRect(main); err == nil {
		p := filepath.Join(outDir, "shot_main.png")
		if err := capture(r.Left, r.Top, r.Right-r.Left, r.Bottom-r.Top, p); err != nil {
			fmt.Println("抓主窗口失败:", err)
		} else {
			fmt.Println("已保存", p)
		}
	}

	// 滚动预览区到底部，检查表格渲染（SB_BOTTOM=8）
	preview := findChildEx(main, "GomdPreview")
	fmt.Println("预览窗口 hwnd:", preview)
	if preview != 0 {
		pPostMessage.Call(uintptr(preview), 0x0115, 7, 0) // WM_VSCROLL, SB_BOTTOM(=7)
		time.Sleep(1200 * time.Millisecond)
		if r, err := getWindowRect(main); err == nil {
			p := filepath.Join(outDir, "shot_bottom.png")
			if err := capture(r.Left, r.Top, r.Right-r.Left, r.Bottom-r.Top, p); err != nil {
				fmt.Println("抓底部失败:", err)
			} else {
				fmt.Println("已保存", p)
			}
		}
	}

	// 模拟拖放一个 Markdown 文件到主窗口（WM_DROPFILES + 共享内存 HDROP）
	if hdrop := makeHDrop(`D:\Projects\goc\docs\fix-roadmap.md`); hdrop != 0 {
		pPostMessage.Call(uintptr(main), 0x0233, hdrop, 0) // WM_DROPFILES
		time.Sleep(1500 * time.Millisecond)
		if r, err := getWindowRect(main); err == nil {
			p := filepath.Join(outDir, "shot_drop.png")
			if err := capture(r.Left, r.Top, r.Right-r.Left, r.Bottom-r.Top, p); err != nil {
				fmt.Println("抓拖放结果失败:", err)
			} else {
				fmt.Println("已保存", p)
			}
		}
	}

	// 模拟在预览区拖选文本 → 截图 → Ctrl+C（发 WM_COMMAND 1004）→ 读剪贴板
	if preview != 0 {
		dragSelect(preview, 60, 45, 430, 150)
		if r, err := getWindowRect(main); err == nil {
			p := filepath.Join(outDir, "shot_selection.png")
			if err := capture(r.Left, r.Top, r.Right-r.Left, r.Bottom-r.Top, p); err != nil {
				fmt.Println("抓选区失败:", err)
			} else {
				fmt.Println("已保存", p)
			}
		}
		pPostMessage.Call(uintptr(main), 0x0111, 1004, 0) // IDM_COPY
		time.Sleep(600 * time.Millisecond)
		fmt.Println("剪贴板内容 =", strconv.Quote(clipboardText()))
	}

	// 切换文件列表显示 (IDM_TOGGLELIST = 1010)，验证单列文件列表
	pPostMessage.Call(uintptr(main), 0x0111, 1010, 0)
	time.Sleep(1500 * time.Millisecond)
	if r, err := getWindowRect(main); err == nil {
		p := filepath.Join(outDir, "shot_list.png")
		if err := capture(r.Left, r.Top, r.Right-r.Left, r.Bottom-r.Top, p); err != nil {
			fmt.Println("抓列表失败:", err)
		} else {
			fmt.Println("已保存", p)
		}
	}

	// 触发 "打开文件..." (IDM_OPENFILE = 1001)
	pPostMessage.Call(uintptr(main), 0x0111, 1001, 0)
	time.Sleep(2500 * time.Millisecond)

	// 枚举本进程所有顶层窗口，确认对话框到底开没开
	for i, s := range listProcessWindows(uint32(cmd.Process.Pid)) {
		fmt.Println(s)
		_ = i
	}

	sw, sh := screenSize()
	pf := filepath.Join(outDir, "shot_dialog.png")
	if err := capture(0, 0, sw, sh, pf); err != nil {
		fmt.Println("抓全屏失败:", err)
	} else {
		fmt.Println("已保存", pf)
	}

	dlg := findWindow("#32770") // 标准通用对话框类名
	if dlg == 0 {
		dlg = findWindow("GomdOpenDlg") // 自绘兜底对话框
	}
	fmt.Println("打开对话框 hwnd:", dlg)
	if dlg != 0 {
		if r, err := getWindowRect(dlg); err == nil {
			p := filepath.Join(outDir, "shot_dialog_crop.png")
			if err := capture(r.Left, r.Top, r.Right-r.Left, r.Bottom-r.Top, p); err != nil {
				fmt.Println("抓对话框裁剪失败:", err)
			} else {
				fmt.Println("已保存", p)
			}
		} else {
			fmt.Println("GetWindowRect(对话框) 失败:", err)
		}
	}

	time.Sleep(500 * time.Millisecond)
	_ = cmd.Process.Kill()
	fmt.Println("done")
}
