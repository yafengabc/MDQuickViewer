package main

import (
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"unicode/utf16"
	"unsafe"

	"golang.org/x/sys/windows"
)

// command ids
const (
	IDM_OPENFILE   = 1001
	IDM_OPENFOLDER = 1002
	IDM_RELOAD     = 1003
	IDM_COPY       = 1004
	IDM_SELECTALL  = 1005
	IDM_EXIT       = 1009
	IDM_TOGGLELIST = 1010
	IDM_ZOOMIN     = 1020
	IDM_ZOOMOUT    = 1021
	IDM_ZOOMRESET  = 1022
	IDM_ABOUT      = 1030
)

const TB_ADDSTRINGW = WM_USER + 38

func makeLong(lo, hi uint16) uintptr {
	return uintptr(uint32(lo) | uint32(hi)<<16)
}

// toolbarStringPool 构造供 TB_ADDSTRINGW 使用的 UTF-16 字符串池：
// 多个以 NUL 分隔的子串 + 末尾双 NUL 结尾。
// 不能用 windows.UTF16PtrFromString，因为它遇到内嵌 NUL 会返回 EINVAL(空指针)。
func toolbarStringPool(parts ...string) *uint16 {
	var buf []uint16
	for _, p := range parts {
		buf = append(buf, utf16.Encode([]rune(p))...)
		buf = append(buf, 0)
	}
	buf = append(buf, 0)
	return &buf[0]
}

// ---------------------------------------------------------------- controls
func createControls() {
	// toolbar
	tb := createWindowEx(0, windows.StringToUTF16Ptr(TOOLBARCLASSNAME), nil,
		WS_CHILD|WS_VISIBLE|TBSTYLE_FLAT|TBSTYLE_LIST|TBSTYLE_TOOLTIPS,
		0, 0, 0, 0, app.mainWnd, 0, hInst, 0)
	app.toolbar = tb
	sendMessage(tb, TB_BUTTONSTRUCTSIZE, uintptr(unsafe.Sizeof(TBBUTTON{})), 0)
	// 注意：不能用 windows.UTF16PtrFromString，它遇到字符串中间的嵌入 NUL 会返回
	// EINVAL(nil 指针)。TB_ADDSTRINGW 需要“以 NUL 分隔、双 NUL 结尾”的 UTF-16 串，
	// 因此手动构造。
	sp := toolbarStringPool("打开文件", "打开文件夹", "文件列表", "放大", "缩小", "重置", "关于")
	first := int32(sendMessage(tb, TB_ADDSTRINGW, 0, uintptr(unsafe.Pointer(sp))))
	runtime.KeepAlive(sp)
	buttons := []TBBUTTON{
		{IBitmap: 0, IdCommand: IDM_OPENFILE, FsState: TBSTATE_ENABLED, FsStyle: BTNS_BUTTON | BTNS_AUTOSIZE, IString: first + 0},
		{IBitmap: 1, IdCommand: IDM_OPENFOLDER, FsState: TBSTATE_ENABLED, FsStyle: BTNS_BUTTON | BTNS_AUTOSIZE, IString: first + 1},
		{IBitmap: 0, IdCommand: 0, FsStyle: BTNS_SEP},
		{IBitmap: 2, IdCommand: IDM_TOGGLELIST, FsState: TBSTATE_ENABLED, FsStyle: BTNS_BUTTON | BTNS_AUTOSIZE, IString: first + 2},
		{IBitmap: 0, IdCommand: 0, FsStyle: BTNS_SEP},
		{IBitmap: 3, IdCommand: IDM_ZOOMIN, FsState: TBSTATE_ENABLED, FsStyle: BTNS_BUTTON | BTNS_AUTOSIZE, IString: first + 3},
		{IBitmap: 4, IdCommand: IDM_ZOOMOUT, FsState: TBSTATE_ENABLED, FsStyle: BTNS_BUTTON | BTNS_AUTOSIZE, IString: first + 4},
		{IBitmap: 5, IdCommand: IDM_ZOOMRESET, FsState: TBSTATE_ENABLED, FsStyle: BTNS_BUTTON | BTNS_AUTOSIZE, IString: first + 5},
		{IBitmap: 0, IdCommand: 0, FsStyle: BTNS_SEP},
		{IBitmap: 6, IdCommand: IDM_ABOUT, FsState: TBSTATE_ENABLED, FsStyle: BTNS_BUTTON | BTNS_AUTOSIZE, IString: first + 6},
	}
	sendMessage(tb, TB_ADDBUTTONSW, uintptr(len(buttons)), uintptr(unsafe.Pointer(&buttons[0])))
	runtime.KeepAlive(buttons)
	// 连接图像列表（手绘 16x16 图标）
	if himl := createToolbarImageList(); himl != 0 {
		sendMessage(tb, TB_SETIMAGELIST, 0, uintptr(himl))
	}
	sendMessage(tb, TB_AUTOSIZE, 0, 0)

	// status bar
	sb := createWindowEx(0, windows.StringToUTF16Ptr(STATUSCLASSNAME), nil, WS_CHILD|WS_VISIBLE, 0, 0, 0, 0, app.mainWnd, 0, hInst, 0)
	app.statusBar = sb
	parts := []int32{-1}
	sendMessage(sb, SB_SETPARTS, 1, uintptr(unsafe.Pointer(&parts[0])))
	runtime.KeepAlive(parts)
	setStatus("就绪")

	// list view
	lv := createWindowEx(WS_EX_CLIENTEDGE, windows.StringToUTF16Ptr(WC_LISTVIEW), nil,
		WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_SHOWSELALWAYS|LVS_SINGLESEL,
		0, 0, 0, 0, app.mainWnd, 0, hInst, 0)
	app.listView = lv
	// 单列：只显示文件名，不显示修改时间/大小
	sendMessage(lv, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER,
		LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER)
	insertColumn(lv, 0, "名称", 280)
	// 左侧文件列表默认折叠（Ctrl+L 或工具栏"文件列表"按钮展开）
	if !app.showList {
		enableWindow(lv, false)
		showWindow(lv, 0)
	}

	// preview
	pv := createWindowEx(0, windows.StringToUTF16Ptr(classNamePreview), nil,
		WS_CHILD|WS_VISIBLE|WS_VSCROLL,
		0, 0, 0, 0, app.mainWnd, 0, hInst, 0)
	app.preview = pv
}

func insertColumn(lv HWND, i int, text string, cx int32) {
	var col LVCOLUMN
	col.Mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT
	col.Fmt = LVCFMT_LEFT
	col.Cx = cx
	col.PszText = windows.StringToUTF16Ptr(text)
	col.ISubItem = int32(i)
	sendMessage(lv, LVM_INSERTCOLUMNW, uintptr(i), uintptr(unsafe.Pointer(&col)))
	runtime.KeepAlive(col.PszText)
}

func insertRow(lv HWND, idx int, name string) {
	var it LVITEM
	it.Mask = LVIF_TEXT | LVIF_PARAM
	it.IItem = int32(idx)
	it.PszText = windows.StringToUTF16Ptr(name)
	it.LParam = uintptr(idx)
	sendMessage(lv, LVM_INSERTITEMW, 0, uintptr(unsafe.Pointer(&it)))
	runtime.KeepAlive(it.PszText)
}

// updateListColumnWidth 让唯一的"名称"列铺满列表宽度。
func updateListColumnWidth(w int32) {
	if w <= 0 {
		return
	}
	sendMessage(app.listView, LVM_SETCOLUMNWIDTH, 0, uintptr(w-6))
}

// ---------------------------------------------------------------- menu
func createMenuBar() {
	m := createMenu()
	fm := createPopupMenu()
	appendMenu(fm, MF_STRING, IDM_OPENFILE, "打开文件...\tCtrl+O")
	appendMenu(fm, MF_STRING, IDM_OPENFOLDER, "打开文件夹...\tCtrl+F")
	appendMenu(fm, MF_STRING, IDM_RELOAD, "重新加载\tF5")
	appendMenu(fm, MF_SEPARATOR, 0, "")
	appendMenu(fm, MF_STRING, IDM_EXIT, "退出\tAlt+F4")
	appendMenu(m, MF_POPUP|MF_STRING, uintptr(fm), "文件")

	em := createPopupMenu()
	appendMenu(em, MF_STRING, IDM_COPY, "复制选中文本\tCtrl+C")
	appendMenu(em, MF_STRING, IDM_SELECTALL, "全选\tCtrl+A")
	appendMenu(m, MF_POPUP|MF_STRING, uintptr(em), "编辑")

	vm := createPopupMenu()
	appendMenu(vm, MF_STRING, IDM_TOGGLELIST, "显示/隐藏文件列表\tCtrl+L")
	appendMenu(vm, MF_SEPARATOR, 0, "")
	appendMenu(vm, MF_STRING, IDM_ZOOMIN, "放大\tCtrl++")
	appendMenu(vm, MF_STRING, IDM_ZOOMOUT, "缩小\tCtrl+-")
	appendMenu(vm, MF_STRING, IDM_ZOOMRESET, "重置缩放\tCtrl+0")
	appendMenu(m, MF_POPUP|MF_STRING, uintptr(vm), "视图")

	hm := createPopupMenu()
	appendMenu(hm, MF_STRING, IDM_ABOUT, "关于 gomd")
	appendMenu(m, MF_POPUP|MF_STRING, uintptr(hm), "帮助")

	app.menu = m
	setMenu(app.mainWnd, m)
	drawMenuBar(app.mainWnd)
}

// ---------------------------------------------------------------- file ops
func populateList(folder string) {
	app.folder = folder
	app.files = nil
	sendMessage(app.listView, LVM_DELETEALLITEMS, 0, 0)
	entries, err := os.ReadDir(folder)
	if err != nil {
		setStatus("无法读取文件夹: " + folder)
		return
	}
	idx := 0
	for _, e := range entries {
		if e.IsDir() {
			continue
		}
		name := e.Name()
		lower := strings.ToLower(name)
		if !strings.HasSuffix(lower, ".md") && !strings.HasSuffix(lower, ".markdown") {
			continue
		}
		path := filepath.Join(folder, name)
		app.files = append(app.files, path)
		insertRow(app.listView, idx, name)
		idx++
	}
	setStatus(fmt.Sprintf("%d 个 Markdown 文件 · %s", len(app.files), folder))
}

func loadFile(path string) {
	data, err := os.ReadFile(path)
	if err != nil {
		messageBox(app.mainWnd, "无法读取文件:\n"+err.Error(), "gomd", MB_ICONERROR)
		return
	}
	d := ParseMarkdown(data, path)
	app.doc = d
	app.scroll = 0
	clearSelection() // 行索引变了，旧选区失效
	relayout()
	setWindowText(app.mainWnd, "gomd — "+filepath.Base(path))
	if d.contentHeight > 0 {
		setStatus(fmt.Sprintf("%s · %d 字节 · %d 行", filepath.Base(path), len(data), len(strings.Split(string(data), "\n"))))
	}
	highlightInList(path)
	invalidateRect(app.preview, nil, true)
}

func highlightInList(path string) {
	for i, p := range app.files {
		if p == path {
			var it LVITEM
			it.Mask = LVIF_STATE
			it.IItem = int32(i)
			it.State = LVIS_SELECTED | LVIS_FOCUSED
			it.StateMask = LVIS_SELECTED | LVIS_FOCUSED
			sendMessage(app.listView, LVM_SETITEMSTATE, uintptr(i), uintptr(unsafe.Pointer(&it)))
			runtime.KeepAlive(&it)
			sendMessage(app.listView, LVM_ENSUREVISIBLE, uintptr(i), 0)
			return
		}
	}
}

func openFolderDialog() {
	folder := browseFolder(app.mainWnd, "选择包含 Markdown 的文件夹")
	if folder != "" {
		rememberFolder(folder)
		populateList(folder)
	}
}

// onDropFiles 处理拖放的文件（WM_DROPFILES）：加载第一个 Markdown 文件。
func onDropFiles(hdrop uintptr) {
	n := dragQueryFileCount(hdrop)
	defer dragFinish(hdrop)
	for i := 0; i < n; i++ {
		path := dragQueryFileName(hdrop, i)
		if path == "" {
			continue
		}
		lower := strings.ToLower(path)
		if !strings.HasSuffix(lower, ".md") && !strings.HasSuffix(lower, ".markdown") {
			continue
		}
		dir := filepath.Dir(path)
		rememberFolder(dir)
		populateList(dir)
		loadFile(path)
		return
	}
	if n > 0 {
		setStatus("拖拽的文件不是 Markdown 文件（支持 .md / .markdown）")
	}
}

// ---------------------------------------------------------------- layout / scroll
func relayout() {
	if app.doc == nil {
		return
	}
	var rc RECT
	getClientRect(app.preview, &rc)
	w := rc.Right - rc.Left
	app.doc.layout(w)
	updateScrollRange()
}

func updateScrollRange() {
	if app.doc == nil {
		return
	}
	var rc RECT
	getClientRect(app.preview, &rc)
	viewH := rc.Bottom - rc.Top
	if viewH < 0 {
		// 窗口被压得极矮时客户区可能算出负高度；直接按 0 处理，
		// 否则 uint32(viewH) 会变成巨大的正数，滚动条范围彻底失真。
		viewH = 0
	}
	total := app.doc.contentHeight
	if viewH >= total {
		app.scroll = 0
	} else {
		clampScroll()
	}
	var si SCROLLINFO
	si.CbSize = uint32(unsafe.Sizeof(si))
	si.FMask = SIF_RANGE | SIF_PAGE | SIF_POS
	si.NMin = 0
	si.NMax = total
	si.NPage = uint32(viewH)
	si.NPos = app.scroll
	setScrollInfo(app.preview, SB_VERT, &si)
}

func clampScroll() {
	if app.doc == nil {
		return
	}
	var rc RECT
	getClientRect(app.preview, &rc)
	viewH := rc.Bottom - rc.Top
	if viewH < 0 {
		viewH = 0
	}
	total := app.doc.contentHeight
	max := total - viewH
	if max < 0 {
		max = 0
	}
	if app.scroll < 0 {
		app.scroll = 0
	}
	if app.scroll > max {
		app.scroll = max
	}
}

func setScrollPos2(pos int32) {
	app.scroll = pos
	clampScroll()
	var si SCROLLINFO
	si.CbSize = uint32(unsafe.Sizeof(si))
	si.FMask = SIF_POS
	si.NPos = app.scroll
	setScrollInfo(app.preview, SB_VERT, &si)
	invalidateRect(app.preview, nil, true)
}

func handleVScroll(wParam uintptr) {
	code := uint16(wParam & 0xffff)
	var rc RECT
	getClientRect(app.preview, &rc)
	viewH := rc.Bottom - rc.Top
	if viewH < 0 {
		viewH = 0
	}
	pos := app.scroll
	switch code {
	case SB_LINEUP:
		pos -= 30
	case SB_LINEDOWN:
		pos += 30
	case SB_PAGEUP:
		pos -= viewH
	case SB_PAGEDOWN:
		pos += viewH
	case SB_TOP:
		pos = 0
	case SB_BOTTOM:
		pos = app.doc.contentHeight
	case SB_THUMBPOSITION, SB_THUMBTRACK:
		var si SCROLLINFO
		si.CbSize = uint32(unsafe.Sizeof(si))
		si.FMask = SIF_TRACKPOS
		getScrollInfo(app.preview, SB_VERT, &si)
		pos = si.NTrackPos
	}
	setScrollPos2(pos)
}

// ---------------------------------------------------------------- commands
func setStatus(text string) {
	p := windows.StringToUTF16Ptr(text)
	sendMessage(app.statusBar, SB_SETTEXTW, 0, uintptr(unsafe.Pointer(p)))
	runtime.KeepAlive(p)
}

func toggleList() {
	app.showList = !app.showList
	enableWindow(app.listView, app.showList)
	if app.showList {
		showWindow(app.listView, SW_SHOW)
	} else {
		showWindow(app.listView, 0)
	}
	layoutChildren()
}

func zoom(factor float64) {
	renderScale *= float32(factor)
	if renderScale < 0.6 {
		renderScale = 0.6
	}
	if renderScale > 3.0 {
		renderScale = 3.0
	}
	clearSelection() // 缩放后行布局改变
	relayout()
	invalidateRect(app.preview, nil, true)
}

func zoomReset() {
	renderScale = 1.0
	relayout()
	invalidateRect(app.preview, nil, true)
}

func about() {
	msg := "gomd — 原生 Win32 Markdown 浏览器\n\n" +
		"纯 Go + Win32 API 实现，无 WebView、无第三方 GUI 库。\n" +
		"渲染基于 goldmark 解析、GDI 自绘。\n\n" +
		"快捷键：Ctrl+O 打开文件，Ctrl+F 打开文件夹，\n" +
		"Ctrl+L 切换列表，鼠标拖选文本后 Ctrl+C 复制，\n" +
		"Ctrl+A 全选，Ctrl+滚轮 缩放，F5 重新加载。"
	messageBox(app.mainWnd, msg, "关于 gomd", MB_ICONINFORMATION)
}

func onCommand(id uintptr) {
	switch int(id) {
	case IDM_OPENFILE:
		openFileDialog()
	case IDM_OPENFOLDER:
		openFolderDialog()
	case IDM_RELOAD:
		if app.doc != nil {
			loadFile(app.doc.path)
		}
	case IDM_COPY:
		copySelection()
	case IDM_SELECTALL:
		selectAllText()
	case IDM_EXIT:
		destroyWindow(app.mainWnd)
	case IDM_TOGGLELIST:
		toggleList()
	case IDM_ZOOMIN:
		zoom(1.1)
	case IDM_ZOOMOUT:
		zoom(1 / 1.1)
	case IDM_ZOOMRESET:
		zoomReset()
	case IDM_ABOUT:
		about()
	}
}

func onListNotify(code int32) {
	if code == LVN_ITEMCHANGED || code == LVN_ITEMACTIVATE || code == NM_DBLCLK {
		idx := int32(sendMessage(app.listView, LVM_GETNEXTITEM, 0xFFFFFFFF, LVNI_SELECTED))
		if idx < 0 || int(idx) >= len(app.files) {
			return
		}
		path := app.files[idx]
		if app.doc == nil || app.doc.path != path {
			loadFile(path)
		}
	}
}

// minPreviewW 列表展开时，预览区至少保留的宽度；也是 cw 的下限参考。
const minPreviewW = 100

// layoutChildren 按主窗口客户区重新摆放工具栏/状态栏/列表/预览。
//
// 关键：所有传给 MoveWindow 的宽高都必须先夹到 >= 0。
// 窗口可以被拖得比 minPreviewW 窄，此时 cw-100 会变成负数；旧代码写成
// `if listW > cw-100 { listW = cw-100 }`，在列表折叠（listW=0）时
// 0 > 负数 成立 → listW 被改成负数 → MoveWindow 以负宽度调整 comctl32
// ListView → 控件内部按负尺寸算布局 → 访问违例 0xC0000005（拖动窗体大小必崩）。
func layoutChildren() {
	var rc RECT
	getClientRect(app.mainWnd, &rc)
	cw := rc.Right - rc.Left
	ch := rc.Bottom - rc.Top
	if cw < 0 {
		cw = 0
	}
	if ch < 0 {
		ch = 0
	}

	// toolbar height
	var tbRect RECT
	getClientRect(app.toolbar, &tbRect)
	tbh := tbRect.Bottom - tbRect.Top
	// status bar height
	var sbRect RECT
	getClientRect(app.statusBar, &sbRect)
	sbh := sbRect.Bottom - sbRect.Top
	if sbh < 18 {
		sbh = 22
	}
	if tbh < 0 {
		tbh = 0
	}

	// toolbar
	moveWindow(app.toolbar, 0, 0, cw, tbh, true)

	// status bar：分区宽度为 cw，再放到客户区底部（窗口太矮时 y 夹到 0）
	parts := []int32{cw}
	sendMessage(app.statusBar, SB_SETPARTS, 1, uintptr(unsafe.Pointer(&parts[0])))
	runtime.KeepAlive(parts)
	sbY := ch - sbh
	if sbY < 0 {
		sbY = 0
	}
	moveWindow(app.statusBar, 0, sbY, cw, sbh, true)

	// list + preview
	top := tbh
	availH := ch - tbh - sbh
	if availH < 0 {
		availH = 0
	}

	listW, prevW := splitListPreview(cw, app.panelW, app.showList)

	moveWindow(app.listView, 0, top, listW, availH, true)
	updateListColumnWidth(listW)
	moveWindow(app.preview, listW, top, prevW, availH, true)

	relayout()
}

// splitListPreview 把客户区宽度拆成"列表宽 + 预览宽"。
//
// 这是"拖动窗体大小必崩"的根因所在：窗口宽度可以小于 minPreviewW，
// 此时旧的 `if listW > cw-100 { listW = cw-100 }` 会在列表折叠
// （listW=0）时算出负宽度并交给 MoveWindow 调整 comctl32 ListView，
// 控件内部按负尺寸排版 → 访问违例 0xC0000005。
//
// 因此这里返回的每个宽度都必须 >= 0，且两者之和不超过 cw。
func splitListPreview(cw, panelW int32, showList bool) (listW, prevW int32) {
	if cw < 0 {
		cw = 0
	}
	listW = panelW
	if !showList {
		listW = 0
	} else if max := cw - minPreviewW; max > 0 && listW > max {
		// 列表展开时按可用宽度收敛，给预览区留出 minPreviewW。
		// 注意 max <= 0 时保持 panelW 不变（窗口太窄，列表交由最小宽度约束处理）。
		listW = max
	}
	if listW < 0 {
		listW = 0
	}
	if listW > cw {
		listW = cw
	}
	prevW = cw - listW
	if prevW < 0 {
		prevW = 0
	}
	return listW, prevW
}

// ---------------------------------------------------------------- toolbar icons
// 手绘 16x16 图标，使用品红(0xFF00FF)作为透明掩色，加入图像列表。
func createToolbarImageList() HIMAGELIST {
	himl := imageListCreate(16, 16, ILC_COLOR32|ILC_MASK, 7)
	if himl == 0 {
		return 0
	}
	for _, k := range []string{"file", "folder", "list", "zoomin", "zoomout", "reset", "about"} {
		bmp := drawIconBitmap(k)
		if bmp == 0 {
			continue
		}
		imageListAddMasked(himl, bmp, COLORREF(0xFF00FF))
		deleteObject(HGDIOBJ(bmp))
	}
	return himl
}

func drawIconBitmap(kind string) HBITMAP {
	mem := createCompatibleDC(measDC)
	if mem == 0 {
		return 0
	}
	var hdr BITMAPINFOHEADER
	hdr.BiSize = uint32(unsafe.Sizeof(hdr))
	hdr.BiWidth = 16
	hdr.BiHeight = -16
	hdr.BiPlanes = 1
	hdr.BiBitCount = 32
	var bits uintptr
	hbm := createDIBSection(mem, &hdr, 0, &bits, 0, 0)
	if hbm == 0 {
		deleteDC(mem)
		return 0
	}
	old := selectObject(mem, HGDIOBJ(hbm))
	rc := RECT{0, 0, 16, 16}
	br := createSolidBrush(COLORREF(0xFF00FF))
	fillRect(mem, &rc, br)
	deleteObject(HGDIOBJ(br))
	drawIconShape(mem, kind)
	selectObject(mem, old)
	deleteDC(mem)
	return hbm
}

func drawIconShape(hdc HDC, kind string) {
	withGP := func(penC, brushC COLORREF, fn func()) {
		pen := createPen(PS_SOLID, 1, penC)
		po := selectObject(hdc, HGDIOBJ(pen))
		brush := createSolidBrush(brushC)
		bo := selectObject(hdc, HGDIOBJ(brush))
		fn()
		selectObject(hdc, bo)
		deleteObject(HGDIOBJ(brush))
		selectObject(hdc, po)
		deleteObject(HGDIOBJ(pen))
	}
	switch kind {
	case "file":
		withGP(0x222222, 0xFFFFFF, func() {
			rectangle(hdc, 3, 2, 12, 14)
		})
		withGP(0x999999, 0xFFFFFF, func() {
			moveToEx(hdc, 5, 6, nil)
			lineTo(hdc, 10, 6)
			moveToEx(hdc, 5, 9, nil)
			lineTo(hdc, 10, 9)
			moveToEx(hdc, 5, 12, nil)
			lineTo(hdc, 8, 12)
		})
	case "folder":
		withGP(0x6B4F00, 0xE0A82A, func() {
			rectangle(hdc, 2, 5, 14, 13)
			moveToEx(hdc, 4, 5, nil)
			lineTo(hdc, 7, 5)
			lineTo(hdc, 9, 3)
			lineTo(hdc, 14, 3)
			lineTo(hdc, 14, 5)
		})
	case "list":
		withGP(0x666666, 0xFFFFFF, func() {
			rectangle(hdc, 2, 2, 14, 14)
		})
		withGP(0x666666, 0x666666, func() {
			rectangle(hdc, 4, 5, 6, 7)
			rectangle(hdc, 4, 9, 6, 11)
		})
		withGP(0x999999, 0x999999, func() {
			moveToEx(hdc, 8, 6, nil)
			lineTo(hdc, 12, 6)
			moveToEx(hdc, 8, 10, nil)
			lineTo(hdc, 12, 10)
		})
	case "zoomin", "zoomout":
		withGP(0x222222, 0xFFFFFF, func() {
			ellipse(hdc, 3, 3, 11, 11)
		})
		withGP(0x222222, 0x222222, func() {
			moveToEx(hdc, 4, 7, nil)
			lineTo(hdc, 10, 7)
			if kind == "zoomin" {
				moveToEx(hdc, 7, 4, nil)
				lineTo(hdc, 7, 10)
			}
		})
		withGP(0x555555, 0x555555, func() {
			moveToEx(hdc, 10, 10, nil)
			lineTo(hdc, 14, 14)
		})
	case "reset":
		withGP(0x0B57D0, 0xFFFFFF, func() {
			ellipse(hdc, 3, 3, 13, 13)
		})
		withGP(0x0B57D0, 0x0B57D0, func() {
			ellipse(hdc, 6, 6, 10, 10)
		})
	case "about":
		withGP(0x0B57D0, 0xFFFFFF, func() {
			ellipse(hdc, 3, 3, 13, 13)
		})
		withGP(0x0B57D0, 0x0B57D0, func() {
			moveToEx(hdc, 8, 5, nil)
			lineTo(hdc, 8, 9)
			ellipse(hdc, 7, 10, 9, 12)
		})
	}
}
