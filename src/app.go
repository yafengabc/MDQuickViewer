package main

import (
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"syscall"
	"unsafe"

	"golang.org/x/sys/windows"
)

var (
	hInst      HINSTANCE
	mainWnd    windows.HWND
	previewWnd windows.HWND
)

const (
	classNameMain    = "GomdMainWindow"
	classNamePreview = "GomdPreview"
)

// App holds global UI state.
type App struct {
	mainWnd   windows.HWND
	toolbar   windows.HWND
	statusBar windows.HWND
	listView  windows.HWND
	preview   windows.HWND
	menu      HMENU
	folder    string
	files     []string
	doc       *Doc
	scroll    int32
	panelW    int32
	showList  bool
	mouseX    int32
	mouseY    int32
}

// showList=false：左侧文件列表默认折叠（Ctrl+L / 工具栏"文件列表"按钮展开）
var app = &App{panelW: 280, showList: false}

func main() {
	// 关键：把消息循环锁定在创建窗口的 OS 线程上。
	// Go 调度器会迁移 goroutine 到不同 OS 线程；Win32 把窗口消息投递到
	// “创建窗口的线程”的消息队列，若 main 被迁移走，GetMessage 将收不到
	// 任何窗口消息，导致界面彻底卡死（“无响应”）。
	runtime.LockOSThread()

	hInst = getModuleHandle()

	// init common controls (toolbar / status bar / list view)
	var icc INITCOMMONCONTROLSEX
	icc.DwSize = uint32(unsafe.Sizeof(icc))
	icc.DwICC = ICC_BAR_CLASSES
	initCommonControlsEx(&icc)

	// measuring DC for text layout
	measDC = getDC(0)

	// register main window class
	var wc WNDCLASSEX
	wc.CbSize = uint32(unsafe.Sizeof(wc))
	wc.Style = CS_HREDRAW | CS_VREDRAW
	wc.LpfnWndProc = syscall.NewCallback(wndProcMain)
	wc.HInstance = hInst
	wc.HCursor = loadCursor(0, IDC_ARROW)
	wc.HbrBackground = HBRUSH(COLOR_WINDOW + 1)
	// 应用图标（来自资源 ID 1，gomd.rc 中的 gomd.ico）
	wc.HIcon = loadIcon(hInst, 1)
	wc.HIconSm = loadImageIcon(hInst, 1, 16, 16)
	wc.LpszClassName = windows.StringToUTF16Ptr(classNameMain)
	if _, err := registerClassEx(&wc); err != nil {
		messageBox(0, "注册主窗口类失败: "+err.Error(), "gomd", MB_ICONERROR)
		return
	}

	// register preview window class
	var pwc WNDCLASSEX
	pwc.CbSize = uint32(unsafe.Sizeof(pwc))
	pwc.Style = CS_HREDRAW | CS_VREDRAW
	pwc.LpfnWndProc = syscall.NewCallback(wndProcPreview)
	pwc.HInstance = hInst
	pwc.HCursor = loadCursor(0, IDC_IBEAM)
	pwc.HbrBackground = HBRUSH(COLOR_WINDOW + 1)
	pwc.LpszClassName = windows.StringToUTF16Ptr(classNamePreview)
	if _, err := registerClassEx(&pwc); err != nil {
		messageBox(0, "注册预览窗口类失败: "+err.Error(), "gomd", MB_ICONERROR)
		return
	}

	// 注册"打开文件"自定义对话框类
	registerOpenDlgClass()

	app.mainWnd = createWindowEx(0,
		windows.StringToUTF16Ptr(classNameMain),
		windows.StringToUTF16Ptr("gomd — Markdown 浏览器"),
		WS_OVERLAPPEDWINDOW|WS_VISIBLE|WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT, 1000, 700,
		0, 0, hInst, 0)
	if app.mainWnd == 0 {
		messageBox(0, "创建主窗口失败。", "gomd", MB_ICONERROR)
		return
	}
	mainWnd = app.mainWnd
	previewWnd = app.preview

	showWindow(app.mainWnd, SW_SHOW)
	updateWindow(app.mainWnd)

	// 命令行参数：gomd.exe <file.md> 或 <文件夹>；没有参数时回退到 exe 旁的 sample.md
	if !openFromArgs(os.Args[1:]) {
		if exe, err := os.Executable(); err == nil {
			dir := filepath.Dir(exe)
			sample := filepath.Join(dir, "sample.md")
			if _, err := os.Stat(sample); err == nil {
				populateList(dir)
				loadFile(sample)
			}
		}
	}

	var msg MSG
	for getMessage(&msg, 0, 0, 0) != 0 {
		translateMessage(&msg)
		dispatchMessage(&msg)
	}
}

// isMarkdown 判断文件名是否是 Markdown。
func isMarkdown(name string) bool {
	lower := strings.ToLower(name)
	return strings.HasSuffix(lower, ".md") || strings.HasSuffix(lower, ".markdown")
}

// openFromArgs 处理命令行参数：
//   - 给出一个 .md/.markdown 文件路径 → 直接打开该文件
//   - 给出一个文件夹 → 只列出其中的 Markdown 文件
//   - 参数不存在/格式不对 → 返回 false（由调用方回退到默认示例）
//
// 这样在资源管理器里把 .md 拖到 gomd.exe 上、或用 "打开方式" 指定 gomd 也能直接打开。
func openFromArgs(args []string) bool {
	for _, a := range args {
		if a == "" {
			continue
		}
		path, err := filepath.Abs(a)
		if err != nil {
			continue
		}
		st, err := os.Stat(path)
		if err != nil {
			// 明确给了参数却找不到：提示一下，避免"点了没反应"
			messageBox(app.mainWnd, "找不到文件或文件夹:\n"+path, "gomd", MB_ICONERROR)
			return true
		}
		if st.IsDir() {
			rememberFolder(path)
			populateList(path)
			return true
		}
		if isMarkdown(path) {
			rememberFolder(filepath.Dir(path))
			populateList(filepath.Dir(path))
			loadFile(path)
			return true
		}
	}
	return false
}

func wndProcMain(hwnd, msg, wParam, lParam uintptr) uintptr {
	switch msg {
	case WM_CREATE:
		app.mainWnd = windows.HWND(hwnd)
		createControls()
		createMenuBar()
		// 主窗口与预览区接受文件拖放
		dragAcceptFiles(HWND(hwnd), true)
		dragAcceptFiles(app.preview, true)
		return 0
	case WM_DROPFILES:
		onDropFiles(wParam)
		return 0
	case WM_SIZE:
		layoutChildren()
		return 0
	case WM_COMMAND:
		id := wParam & 0xffff
		onCommand(id)
		return 0
	case WM_NOTIFY:
		nmh := (*NMHDR)(unsafe.Pointer(lParam))
		onListNotify(int32(nmh.Code))
		return 0
	case WM_KEYDOWN:
		onKeyDown(wParam)
		return 0
	case WM_DESTROY:
		postQuitMessage(0)
		return 0
	}
	return defWindowProc(windows.HWND(hwnd), uint32(msg), wParam, lParam)
}

func wndProcPreview(hwnd, msg, wParam, lParam uintptr) uintptr {
	switch msg {
	case WM_ERASEBKGND:
		return 1
	case WM_PAINT:
		var ps PAINTSTRUCT
		hdc := beginPaint(windows.HWND(hwnd), &ps)
		var rc RECT
		getClientRect(windows.HWND(hwnd), &rc)
		if app.doc != nil {
			app.doc.paint(hdc, app.scroll, rc.Right-rc.Left, rc.Bottom-rc.Top)
		} else {
			var br RECT
			br.Right = rc.Right
			br.Bottom = rc.Bottom
			b := createSolidBrush(COLORREF(0xFFFFFF))
			fillRect(hdc, &br, b)
			deleteObject(HGDIOBJ(b))
			setBkMode(hdc, TRANSPARENT)
			setTextColor(hdc, COLORREF(0x999999))
			textOut(hdc, LM, TM, "从菜单或工具栏打开一个 Markdown 文件开始阅读")
		}
		endPaint(windows.HWND(hwnd), &ps)
		return 0
	case WM_VSCROLL:
		handleVScroll(wParam)
		return 0
	case WM_MOUSEWHEEL:
		if app.doc == nil {
			return 0
		}
		delta := int32(int16(uint16(uint32(wParam) >> 16)))
		app.scroll -= delta / 120 * 40
		clampScroll()
		setScrollPos2(app.scroll)
		return 0
	case WM_LBUTTONDOWN:
		if app.doc != nil {
			x := int32(uint16(uintptr(lParam)))
			y := int32(int16(uint16(uintptr(lParam) >> 16)))
			// 开始可能的拖选：先记录锚点并捕获鼠标
			selAnchor = app.doc.hitPos(x, y+app.scroll)
			selHead = selAnchor
			selOn = false
			selDrag = true
			setCapture(windows.HWND(hwnd))
			invalidateRect(windows.HWND(hwnd), nil, false)
		}
		return 0
	case WM_MOUSEMOVE:
		if app.doc != nil {
			x := int32(uint16(uintptr(lParam)))
			y := int32(int16(uint16(uintptr(lParam) >> 16)))
			app.mouseX, app.mouseY = x, y
			if selDrag {
				// 拖出可视区时自动滚动
				var rc RECT
				getClientRect(windows.HWND(hwnd), &rc)
				if y < 0 {
					app.scroll -= 24
				} else if y > rc.Bottom-rc.Top {
					app.scroll += 24
				}
				selHead = app.doc.hitPos(x, y+app.scroll)
				selOn = !selAnchor.equal(selHead)
				clampScroll()
				invalidateRect(windows.HWND(hwnd), nil, false)
			}
			updatePreviewCursor()
		}
		return 0
	case WM_LBUTTONUP:
		if app.doc != nil {
			x := int32(uint16(uintptr(lParam)))
			y := int32(int16(uint16(uintptr(lParam) >> 16)))
			if selDrag {
				selDrag = false
				releaseCapture()
				selHead = app.doc.hitPos(x, y+app.scroll)
				selOn = !selAnchor.equal(selHead)
				if !selOn {
					// 单击（没有拖出选区）：保持原有的打开链接行为
					if dest := app.doc.hitLink(x, y+app.scroll); dest != "" {
						openLink(dest)
					}
				}
				invalidateRect(windows.HWND(hwnd), nil, false)
			}
		}
		return 0
	case WM_KEYDOWN:
		onKeyDown(wParam)
		return 0
	case WM_SETCURSOR:
		updatePreviewCursor()
		return 1
	case WM_DROPFILES:
		// 拖到预览区上时消息发给子窗口，同样处理
		onDropFiles(wParam)
		return 0
	}
	return defWindowProc(windows.HWND(hwnd), uint32(msg), wParam, lParam)
}

func updatePreviewCursor() {
	var cur HCURSOR
	if app.doc != nil {
		cy := app.mouseY + app.scroll
		if app.doc.hitLink(app.mouseX, cy) != "" {
			cur = loadCursor(0, IDC_HAND)
		} else {
			cur = loadCursor(0, IDC_IBEAM)
		}
	} else {
		cur = loadCursor(0, IDC_IBEAM)
	}
	setCursor(cur)
}

func openLink(dest string) {
	if strings.HasPrefix(dest, "http://") || strings.HasPrefix(dest, "https://") || strings.HasPrefix(dest, "mailto:") {
		shellExecute(0, "open", dest, "", "", SW_SHOWNORMAL_F)
	}
}

func onKeyDown(wParam uintptr) {
	ctrl := getKeyState(VK_CONTROL) < 0
	switch int(wParam) {
	case 'O':
		if ctrl {
			openFileDialog()
		}
	case 'F':
		if ctrl {
			openFolderDialog()
		}
	case 'L':
		if ctrl {
			toggleList()
		}
	case 'C':
		if ctrl {
			copySelection()
		}
	case 'A':
		if ctrl {
			selectAllText()
		}
	case '=':
		if ctrl {
			zoom(1.1)
		}
	case '+':
		if ctrl {
			zoom(1.1)
		}
	case '-':
		if ctrl {
			zoom(1 / 1.1)
		}
	case '0':
		if ctrl {
			zoomReset()
		}
	case VK_F5:
		if app.doc != nil {
			loadFile(app.doc.path)
		}
	}
}
