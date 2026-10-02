package main

import (
	"fmt"
	"unsafe"

	"golang.org/x/sys/windows"
)

// ---------------------------------------------------------------- DLLs
var (
	kernel32 = windows.NewLazySystemDLL("kernel32.dll")
	user32   = windows.NewLazySystemDLL("user32.dll")
	gdi32    = windows.NewLazySystemDLL("gdi32.dll")
	comctl32 = windows.NewLazySystemDLL("comctl32.dll")
	comdlg32 = windows.NewLazySystemDLL("comdlg32.dll")
	shell32  = windows.NewLazySystemDLL("shell32.dll")
	advapi32 = windows.NewLazySystemDLL("advapi32.dll")
)

// ---------------------------------------------------------------- procs
var (
	procGetModuleHandleW    = kernel32.NewProc("GetModuleHandleW")
	procGetLastError        = kernel32.NewProc("GetLastError")
	procGlobalAlloc         = kernel32.NewProc("GlobalAlloc")
	procGlobalFree          = kernel32.NewProc("GlobalFree")
	procGlobalLock          = kernel32.NewProc("GlobalLock")
	procGlobalUnlock        = kernel32.NewProc("GlobalUnlock")
	procWideCharToMultiByte = kernel32.NewProc("WideCharToMultiByte")
	procLocalFree           = kernel32.NewProc("LocalFree")

	procRegisterClassExW     = user32.NewProc("RegisterClassExW")
	procCreateWindowExW      = user32.NewProc("CreateWindowExW")
	procDefWindowProcW       = user32.NewProc("DefWindowProcW")
	procShowWindow           = user32.NewProc("ShowWindow")
	procUpdateWindow         = user32.NewProc("UpdateWindow")
	procGetMessageW          = user32.NewProc("GetMessageW")
	procTranslateMessage     = user32.NewProc("TranslateMessage")
	procDispatchMessageW     = user32.NewProc("DispatchMessageW")
	procPostQuitMessage      = user32.NewProc("PostQuitMessage")
	procSendMessageW         = user32.NewProc("SendMessageW")
	procDefDlgProcW          = user32.NewProc("DefDlgProcW")
	procDestroyWindow        = user32.NewProc("DestroyWindow")
	procSetWindowTextW       = user32.NewProc("SetWindowTextW")
	procGetWindowTextLengthW = user32.NewProc("GetWindowTextLengthW")
	procGetWindowTextW       = user32.NewProc("GetWindowTextW")
	procGetClientRect        = user32.NewProc("GetClientRect")
	procGetWindowRect        = user32.NewProc("GetWindowRect")
	procMoveWindow           = user32.NewProc("MoveWindow")
	procLoadCursorW          = user32.NewProc("LoadCursorW")
	procLoadImageW           = user32.NewProc("LoadImageW")
	procLoadIconW            = user32.NewProc("LoadIconW")
	procGetDC                = user32.NewProc("GetDC")
	procReleaseDC            = user32.NewProc("ReleaseDC")
	procBeginPaint           = user32.NewProc("BeginPaint")
	procEndPaint             = user32.NewProc("EndPaint")
	procGetSysColor          = user32.NewProc("GetSysColor")
	procInvalidateRect       = user32.NewProc("InvalidateRect")
	procValidateRect         = user32.NewProc("ValidateRect")
	procGetWindowLongPtrW    = user32.NewProc("GetWindowLongPtrW")
	procSetWindowLongPtrW    = user32.NewProc("SetWindowLongPtrW")
	procSetWindowPos         = user32.NewProc("SetWindowPos")
	procMessageBoxW          = user32.NewProc("MessageBoxW")
	procGetSystemMetrics     = user32.NewProc("GetSystemMetrics")
	procGetKeyState          = user32.NewProc("GetKeyState")
	procSetScrollInfo        = user32.NewProc("SetScrollInfo")
	procGetScrollInfo        = user32.NewProc("GetScrollInfo")
	procSetScrollPos         = user32.NewProc("SetScrollPos")
	procGetScrollPos         = user32.NewProc("GetScrollPos")
	procScreenToClient       = user32.NewProc("ScreenToClient")
	procClientToScreen       = user32.NewProc("ClientToScreen")
	procSetCapture           = user32.NewProc("SetCapture")
	procReleaseCapture       = user32.NewProc("ReleaseCapture")
	procEnableWindow         = user32.NewProc("EnableWindow")
	procSetFocus             = user32.NewProc("SetFocus")
	procSetForegroundWindow  = user32.NewProc("SetForegroundWindow")
	procGetWindow            = user32.NewProc("GetWindow")
	procTrackMouseEvent      = user32.NewProc("TrackMouseEvent")
	procSetCursor            = user32.NewProc("SetCursor")
	procGetParent            = user32.NewProc("GetParent")
	procIsWindow             = user32.NewProc("IsWindow")
	procGetDlgItem           = user32.NewProc("GetDlgItem")
	procCreateMenu           = user32.NewProc("CreateMenu")
	procCreatePopupMenu      = user32.NewProc("CreatePopupMenu")
	procAppendMenuW          = user32.NewProc("AppendMenuW")
	procSetMenu              = user32.NewProc("SetMenu")
	procDrawMenuBar          = user32.NewProc("DrawMenuBar")
	procGetMenuStringW       = user32.NewProc("GetMenuStringW")
	procCheckMenuItem        = user32.NewProc("CheckMenuItem")
	procSetMenuDefaultItem   = user32.NewProc("SetMenuDefaultItem")
	procGetSubMenu           = user32.NewProc("GetSubMenu")

	procCreateFontIndirectW   = gdi32.NewProc("CreateFontIndirectW")
	procCreateCompatibleDC    = gdi32.NewProc("CreateCompatibleDC")
	procCreateDIBSection      = gdi32.NewProc("CreateDIBSection")
	procDeleteDC              = gdi32.NewProc("DeleteDC")
	procSelectObject          = gdi32.NewProc("SelectObject")
	procDeleteObject          = gdi32.NewProc("DeleteObject")
	procGetObjectW            = gdi32.NewProc("GetObjectW")
	procGetTextExtentPoint32W = gdi32.NewProc("GetTextExtentPoint32W")
	procSetTextColor          = gdi32.NewProc("SetTextColor")
	procSetBkMode             = gdi32.NewProc("SetBkMode")
	procSetBkColor            = gdi32.NewProc("SetBkColor")
	procTextOutW              = gdi32.NewProc("TextOutW")
	procDrawTextW             = gdi32.NewProc("DrawTextW")
	procExtTextOutW           = gdi32.NewProc("ExtTextOutW")
	procCreateSolidBrush      = gdi32.NewProc("CreateSolidBrush")
	procFillRect              = user32.NewProc("FillRect")
	procFillPath              = gdi32.NewProc("FillPath")
	procRectangle             = gdi32.NewProc("Rectangle")
	procRoundRect             = gdi32.NewProc("RoundRect")
	procEllipse               = gdi32.NewProc("Ellipse")
	procCreatePen             = gdi32.NewProc("CreatePen")
	procMoveToEx              = gdi32.NewProc("MoveToEx")
	procLineTo                = gdi32.NewProc("LineTo")
	procGetDeviceCaps         = gdi32.NewProc("GetDeviceCaps")
	procGetTextMetricsW       = gdi32.NewProc("GetTextMetricsW")
	procGetStockObject        = gdi32.NewProc("GetStockObject")
	procSetTextAlign          = gdi32.NewProc("SetTextAlign")
	procSetMapMode            = gdi32.NewProc("SetMapMode")
	procSaveDC                = gdi32.NewProc("SaveDC")
	procRestoreDC             = gdi32.NewProc("RestoreDC")
	procGetClipBox            = gdi32.NewProc("GetClipBox")

	procInitCommonControlsEx = comctl32.NewProc("InitCommonControlsEx")
	procImageList_Create     = comctl32.NewProc("ImageList_Create")
	procImageList_Add        = comctl32.NewProc("ImageList_Add")
	procImageList_AddMasked  = comctl32.NewProc("ImageList_AddMasked")
	procImageList_Destroy    = comctl32.NewProc("ImageList_Destroy")

	procGetOpenFileNameW     = comdlg32.NewProc("GetOpenFileNameW")
	procCommDlgExtendedError = comdlg32.NewProc("CommDlgExtendedError")

	procSHGetFileInfoW       = shell32.NewProc("SHGetFileInfoW")
	procShell_NotifyIconW    = shell32.NewProc("Shell_NotifyIconW")
	procShellExecuteW        = shell32.NewProc("ShellExecuteW")
	procSHBrowseForFolderW   = shell32.NewProc("SHBrowseForFolderW")
	procSHGetPathFromIDListW = shell32.NewProc("SHGetPathFromIDListW")

	procOpenClipboard    = user32.NewProc("OpenClipboard")
	procCloseClipboard   = user32.NewProc("CloseClipboard")
	procEmptyClipboard   = user32.NewProc("EmptyClipboard")
	procSetClipboardData = user32.NewProc("SetClipboardData")

	procDragAcceptFiles = shell32.NewProc("DragAcceptFiles")
	procDragQueryFileW  = shell32.NewProc("DragQueryFileW")
	procDragFinish      = shell32.NewProc("DragFinish")

	procRegCreateKeyExW  = advapi32.NewProc("RegCreateKeyExW")
	procRegOpenKeyExW    = advapi32.NewProc("RegOpenKeyExW")
	procRegSetValueExW   = advapi32.NewProc("RegSetValueExW")
	procRegQueryValueExW = advapi32.NewProc("RegQueryValueExW")
	procRegDeleteValueW  = advapi32.NewProc("RegDeleteValueW")
	procRegCloseKey      = advapi32.NewProc("RegCloseKey")
)

// ---------------------------------------------------------------- constants
const (
	NULL = 0

	// window styles
	WS_OVERLAPPED       = 0x00000000
	WS_POPUP            = 0x80000000
	WS_CHILD            = 0x40000000
	WS_MINIMIZE         = 0x20000000
	WS_VISIBLE          = 0x10000000
	WS_DISABLED         = 0x08000000
	WS_CLIPSIBLINGS     = 0x04000000
	WS_CLIPCHILDREN     = 0x02000000
	WS_MAXIMIZE         = 0x01000000
	WS_CAPTION          = 0x00C00000
	WS_BORDER           = 0x00800000
	WS_DLGFRAME         = 0x00400000
	WS_VSCROLL          = 0x00200000
	WS_HSCROLL          = 0x00100000
	WS_SYSMENU          = 0x00080000
	WS_THICKFRAME       = 0x00040000
	WS_GROUP            = 0x00020000
	WS_TABSTOP          = 0x00010000
	WS_MINIMIZEBOX      = 0x00020000
	WS_MAXIMIZEBOX      = 0x00010000
	WS_OVERLAPPEDWINDOW = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX
	WS_EX_CLIENTEDGE    = 0x00000200
	WS_EX_TOPMOST       = 0x00000008
	WS_EX_DLGMODALFRAME = 0x00000001
	WS_EX_ACCEPTFILES   = 0x00000010
	WS_EX_APPWINDOW     = 0x00040000
	WS_EX_WINDOWEDGE    = 0x00000100
	WS_EX_COMPOSITED    = 0x02000000

	// class styles
	CS_HREDRAW = 0x0002
	CS_VREDRAW = 0x0001
	CS_DBLCLKS = 0x0008

	// show
	SW_HIDE        = 0
	SW_SHOWNORMAL  = 1
	SW_SHOW        = 5
	SW_SHOWDEFAULT = 10
	SW_MAXIMIZE    = 3
	SW_MINIMIZE    = 6
	SW_RESTORE     = 9

	CW_USEDEFAULT = -2147483648

	// messages
	WM_NULL           = 0x0000
	WM_CREATE         = 0x0001
	WM_DESTROY        = 0x0002
	WM_MOVE           = 0x0003
	WM_SIZE           = 0x0005
	WM_ACTIVATE       = 0x0006
	WM_PAINT          = 0x000F
	WM_CLOSE          = 0x0010
	WM_QUIT           = 0x0012
	WM_ERASEBKGND     = 0x0014
	WM_SETCURSOR      = 0x0020
	WM_SETFOCUS       = 0x0007
	WM_KILLFOCUS      = 0x0008
	WM_COMMAND        = 0x0111
	WM_NOTIFY         = 0x004E
	WM_VSCROLL        = 0x0115
	WM_HSCROLL        = 0x0114
	WM_MOUSEWHEEL     = 0x020A
	WM_MOUSEMOVE      = 0x0200
	WM_LBUTTONDOWN    = 0x0201
	WM_LBUTTONUP      = 0x0202
	WM_LBUTTONDBLCLK  = 0x0203
	WM_RBUTTONDOWN    = 0x0204
	WM_RBUTTONUP      = 0x0205
	WM_MOUSELEAVE     = 0x02A3
	WM_KEYDOWN        = 0x0100
	WM_CHAR           = 0x0102
	WM_GETMINMAXINFO  = 0x0024
	WM_SETFONT        = 0x0030
	WM_GETFONT        = 0x0031
	WM_DROPFILES      = 0x0233
	WM_INITMENUPOPUP  = 0x0117
	WM_MENUSELECT     = 0x011F
	WM_MEASUREITEM    = 0x002C
	WM_DRAWITEM       = 0x002B
	WM_CTLCOLORSTATIC = 0x0138
	WM_CTLCOLOREDIT   = 0x0137
	WM_TIMER          = 0x0113
	WM_USER           = 0x0400

	// scroll
	SB_HORZ             = 0
	SB_VERT             = 1
	SB_CTL              = 2
	SB_LINEUP           = 0
	SB_LINEDOWN         = 1
	SB_PAGEUP           = 2
	SB_PAGEDOWN         = 3
	SB_THUMBPOSITION    = 4
	SB_THUMBTRACK       = 5
	SB_TOP              = 6
	SB_BOTTOM           = 7
	SB_ENDSCROLL        = 8
	SIF_RANGE           = 0x0001
	SIF_PAGE            = 0x0002
	SIF_POS             = 0x0004
	SIF_DISABLENOSCROLL = 0x0008
	SIF_TRACKPOS        = 0x00010
	SIF_ALL             = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_TRACKPOS

	// devices
	LOGPIXELSX = 88
	LOGPIXELSY = 90

	// gdi
	BLACK_BRUSH      = 4
	WHITE_BRUSH      = 0
	NULL_BRUSH       = 5
	HOLLOW_BRUSH     = NULL_BRUSH
	DEFAULT_GUI_FONT = 17

	TRANSPARENT = 1
	OPAQUE      = 2

	TA_LEFT   = 0
	TA_RIGHT  = 2
	TA_CENTER = 6
	TA_TOP    = 0
	TA_BOTTOM = 8

	// bk modes / colors
	COLOR_WINDOW        = 5
	COLOR_WINDOWTEXT    = 8
	COLOR_BTNFACE       = 15
	COLOR_BTNTEXT       = 18
	COLOR_3DFACE        = 15
	COLOR_3DSHADOW      = 16
	COLOR_3DHIGHLIGHT   = 20
	COLOR_HIGHLIGHT     = 13
	COLOR_HIGHLIGHTTEXT = 14
	COLOR_SCROLLBAR     = 0
	COLOR_MENU          = 4
	COLOR_GRAYTEXT      = 17

	// system metrics
	SM_CXSCREEN    = 0
	SM_CYSCREEN    = 1
	SM_CXVSCROLL   = 2
	SM_CYHSCROLL   = 3
	SM_CYCAPTION   = 4
	SM_CXBORDER    = 5
	SM_CYBORDER    = 6
	SM_CXDLGFRAME  = 7
	SM_CYDLGFRAME  = 8
	SM_CYMENU      = 15
	SM_CXSMICON    = 49
	SM_CYSMICON    = 50
	SM_CXICON      = 11
	SM_CYICON      = 12
	SM_CXMENUCHECK = 71
	SM_CYMENUCHECK = 72

	// cursors
	IDC_ARROW  = 32512
	IDC_IBEAM  = 32513
	IDC_WAIT   = 32514
	IDC_CROSS  = 32515
	IDC_HAND   = 32649
	IDC_SIZENS = 32645

	// icons
	IDI_APPLICATION = 32512
	IDI_INFORMATION = 32516
	IDI_QUESTION    = 32514
	IDI_WARNING     = 32515
	IDI_ERROR       = 32513

	// virtual keys
	VK_CONTROL = 0x11
	VK_MENU    = 0x12
	VK_F5      = 0x74
	VK_ESCAPE  = 0x1B

	// standard ids
	IDOK     = 1
	IDCANCEL = 2

	// common controls
	ICC_WIN95_CLASSES  = 0x000000FF
	ICC_BAR_CLASSES    = 0x00000004
	ICC_COOL_CLASSES   = 0x00000400
	ICC_USEREX_CLASSES = 0x00000200

	// toolbar
	TBSTYLE_TOOLTIPS    = 0x0100
	TBSTYLE_FLAT        = 0x0800
	TBSTYLE_LIST        = 0x1000
	TBSTYLE_TRANSPARENT = 0x8000
	TBSTYLE_WRAPABLE    = 0x0200
	TB_BUTTON           = 0
	TB_PRESS_CHECK      = 0x0004
	TBSTATE_ENABLED     = 0x0004
	TBSTATE_PRESSED     = 0x0001
	BTNS_BUTTON         = 0x0000
	BTNS_AUTOSIZE       = 0x0010
	BTNS_SEP            = 0x0001
	BTNS_CHECK          = 0x0002
	TBIF_IMAGE          = 0x0001
	I_IMAGENONE         = -2
	TB_ADDBUTTONSA      = WM_USER + 20
	TB_ADDBUTTONSW      = WM_USER + 68 // W 版是 +68；+20 是 ANSI 版，误用会导致按钮文字全丢
	TB_BUTTONSTRUCTSIZE = WM_USER + 30
	TB_SETBUTTONSIZE    = WM_USER + 32
	TB_AUTOSIZE         = WM_USER + 33
	TB_GETBUTTONSIZE    = WM_USER + 59
	TB_SETIMAGELIST     = WM_USER + 48
	TB_SETBITMAPSIZE    = WM_USER + 32 + 0

	// image list
	ILC_MASK    = 0x0001
	ILC_COLOR32 = 0x00000020

	// status bar
	SB_SETTEXTW    = WM_USER + 11
	SB_GETTEXTW    = WM_USER + 13
	SB_SETTIPTEXTW = WM_USER + 17
	SB_SETPARTS    = WM_USER + 4
	SB_GETPARTS    = WM_USER + 6
	SB_SIMPLE      = WM_USER + 9
	SBT_NOBORDERS  = 0x0100
	SBT_POPOUT     = 0x0200
	SBT_OWNERDRAW  = 0x1000

	// listview
	LVM_FIRST                    = 0x1000
	LVM_GETITEMCOUNT             = LVM_FIRST + 4
	LVM_GETNEXTITEM              = LVM_FIRST + 12
	LVM_INSERTCOLUMNW            = LVM_FIRST + 97 // W 版是 +97；+27 是 ANSI 版，误用会导致列头 UTF-16 被 ANSI 解码成乱码
	LVM_SETEXTENDEDLISTVIEWSTYLE = LVM_FIRST + 54
	LVM_GETEXTENDEDLISTVIEWSTYLE = LVM_FIRST + 55
	LVM_SETCOLUMNW               = LVM_FIRST + 26
	LVM_SETCOLUMNWIDTH           = LVM_FIRST + 30
	LVSCW_AUTOSIZE               = -1 // 按内容自适应列宽
	LVSCW_AUTOSIZE_USEHEADER     = -2
	LVM_INSERTITEMW              = LVM_FIRST + 77
	LVM_SETITEMW                 = LVM_FIRST + 76
	LVM_GETITEMW                 = LVM_FIRST + 75
	LVM_DELETEALLITEMS           = LVM_FIRST + 9
	LVM_DELETEITEM               = LVM_FIRST + 8
	LVM_GETITEMTEXTW             = LVM_FIRST + 115
	LVM_ENSUREVISIBLE            = LVM_FIRST + 19
	LVM_GETSELECTEDCOUNT         = LVM_FIRST + 74
	LVM_SETITEMSTATE             = LVM_FIRST + 43
	LVM_GETITEMSTATE             = LVM_FIRST + 44
	LVM_SETITEMPOSITION          = LVM_FIRST + 15
	LVM_GETTOPINDEX              = LVM_FIRST + 39
	LVM_GETCOUNTPERPAGE          = LVM_FIRST + 40
	LVM_SETBKCOLOR               = LVM_FIRST + 1
	LVM_SETTEXTCOLOR             = LVM_FIRST + 36
	LVM_SETTEXTBKCOLOR           = LVM_FIRST + 38
	LVM_GETITEMSPACING           = LVM_FIRST + 64
	LVM_SETICONSPACING           = LVM_FIRST + 53
	LVM_APPROXIMATEVIEWRECT      = LVM_FIRST + 64
	LVM_SETVIEW                  = LVM_FIRST + 142
	LVM_GETVIEW                  = LVM_FIRST + 143
	LVM_SETTILEWIDTH             = LVM_FIRST + 141

	LVIF_TEXT           = 0x0001
	LVIF_IMAGE          = 0x0002
	LVIF_PARAM          = 0x0004
	LVIF_STATE          = 0x0008
	LVIF_INDENT         = 0x0010
	LVIS_SELECTED       = 0x0002
	LVIS_FOCUSED        = 0x0001
	LVIS_STATEIMAGEMASK = 0xF000

	LVCF_FMT      = 0x0001
	LVCF_WIDTH    = 0x0002
	LVCF_TEXT     = 0x0004
	LVCF_SUBITEM  = 0x0008
	LVCF_ORDER    = 0x0020
	LVCFMT_LEFT   = 0x0000
	LVCFMT_RIGHT  = 0x0001
	LVCFMT_CENTER = 0x0002
	LVCFMT_IMAGE  = 0x0800

	LVS_ICON           = 0x0000
	LVS_REPORT         = 0x0001
	LVS_SMALLICON      = 0x0002
	LVS_LIST           = 0x0003
	LVS_SINGLESEL      = 0x0004
	LVS_SHOWSELALWAYS  = 0x0008
	LVS_SORTASCENDING  = 0x0010
	LVS_SORTDESCENDING = 0x0020
	LVS_NOSORTHEADER   = 0x8000
	LVS_OWNERDATA      = 0x1000

	LVS_EX_FULLROWSELECT  = 0x00000020
	LVS_EX_GRIDLINES      = 0x00000001
	LVS_EX_DOUBLEBUFFER   = 0x00010000
	LVS_EX_HEADERDRAGDROP = 0x00000010
	LVS_EX_LABELTIP       = 0x00004000
	LVS_EX_BORDERSELECT   = 0x00000800

	LVN_FIRST        = -100
	LVN_ITEMCHANGED  = LVN_FIRST - 1
	LVN_ITEMACTIVATE = LVN_FIRST - 14
	LVN_GETDISPINFOA = LVN_FIRST - 50
	LVN_GETDISPINFOW = LVN_FIRST - 77

	NM_FIRST           = 0
	NM_CLICK           = NM_FIRST - 2
	NM_DBLCLK          = NM_FIRST - 3
	NM_RCLICK          = NM_FIRST - 5
	NM_RETURN          = NM_FIRST - 4
	NM_KILLFOCUS       = NM_FIRST - 8
	NM_CUSTOMDRAW      = NM_FIRST - 12
	NM_RELEASEDCAPTURE = NM_FIRST - 16

	LVNI_SELECTED = 0x0002
	LVNI_FOCUSED  = 0x0001
	LVNI_ALL      = 0x0000

	// window long
	GWL_STYLE      = -16
	GWL_EXSTYLE    = -20
	GWL_WNDPROC    = -4
	GWL_HINSTANCE  = -6
	GWL_ID         = -12
	GWL_USERDATA   = -21
	GWLP_WNDPROC   = -4
	GWLP_HINSTANCE = -6
	GWLP_ID        = -12
	GWLP_USERDATA  = -21
	GW_OWNER       = 4
	GW_CHILD       = 5
	GW_HWNDNEXT    = 2

	// common control class names
	WC_LISTVIEW      = "SysListView32"
	WC_TREEVIEW      = "SysTreeView32"
	WC_TOOLBAR       = "ToolbarWindow32"
	WC_STATUSBAR     = "msctls_statusbar32"
	WC_HEADER        = "SysHeader32"
	WC_PROGRESS      = "msctls_progress32"
	WC_TABCONTROL    = "SysTabControl32"
	TOOLBARCLASSNAME = "ToolbarWindow32"
	STATUSCLASSNAME  = "msctls_statusbar32"
	WC_BUTTON        = "Button"
	WC_EDIT          = "Edit"
	WC_STATIC        = "Static"

	// edit / static styles
	ES_MULTILINE   = 0x0004
	ES_AUTOVSCROLL = 0x0040
	ES_AUTOHSCROLL = 0x0080
	ES_READONLY    = 0x0800
	ES_WANTRETURN  = 0x1000
	ES_NOHIDESEL   = 0x0100

	SS_LEFT         = 0x0000
	SS_CENTER       = 0x0001
	SS_RIGHT        = 0x0002
	SS_NOTIFY       = 0x0100
	SS_SUNKEN       = 0x1000
	SS_ETCHEDHORZ   = 0x000C
	SS_OWNERDRAW    = 0x000D
	SS_BITMAP       = 0x000E
	SS_ICON         = 0x0003
	SS_WORDELLIPSIS = 0x000C
	SS_ENDELLIPSIS  = 0x4000
	SS_PATHELLIPSIS = 0x8000

	BS_PUSHBUTTON      = 0x0000
	BS_DEFPUSHBUTTON   = 0x0001
	BS_CHECKBOX        = 0x0002
	BS_AUTOCHECKBOX    = 0x0003
	BS_GROUPBOX        = 0x0007
	BS_AUTORADIOBUTTON = 0x0009
	BS_LEFT            = 0x0100
	BS_FLAT            = 0x8000

	// listbox
	LBS_NOTIFY           = 0x0001
	LBS_SORT             = 0x0002
	LBS_HASSTRINGS       = 0x0008
	LBS_NOINTEGRALHEIGHT = 0x0100
	LB_ADDSTRING         = 0x0180
	LB_INSERTSTRING      = 0x0181
	LB_DELETESTRING      = 0x0182
	LB_RESETCONTENT      = 0x0184
	LB_GETCOUNT          = 0x018B
	LB_GETCURSEL         = 0x0188
	LB_SETCURSEL         = 0x0186
	LB_GETTEXT           = 0x0189
	LB_GETTEXTLEN        = 0x018A
	LB_DIR               = 0x018D
	LBN_SELCHANGE        = 1
	LBN_DBLCLK           = 2

	// menu
	MF_STRING     = 0x00000000
	MF_POPUP      = 0x00000010
	MF_SEPARATOR  = 0x00000800
	MF_ENABLED    = 0x00000000
	MF_DISABLED   = 0x00000002
	MF_GRAYED     = 0x00000001
	MF_UNCHECKED  = 0x00000000
	MF_CHECKED    = 0x00000008
	MF_BYCOMMAND  = 0x00000000
	MF_BYPOSITION = 0x00000400

	// scrollbar messages
	EM_GETSEL     = 0x00B0
	EM_SETSEL     = 0x00B1
	EM_REPLACESEL = 0x00C2

	// getopenfilename
	OFN_EXPLORER         = 0x00080000
	OFN_FILEMUSTEXIST    = 0x00001000
	OFN_PATHMUSTEXIST    = 0x00000800
	OFN_ALLOWMULTISELECT = 0x00000200
	OFN_HIDEREADONLY     = 0x00000004
	OFN_NOCHANGEDIR      = 0x00000008
	OFN_OVERWRITEPROMPT  = 0x00000002
	OFN_CREATEPROMPT     = 0x00002000
	OFN_SHAREAWARE       = 0x00004000
	OFN_NOREADONLYRETURN = 0x00008000
	OFN_ENABLESIZING     = 0x00800000

	// misc
	LR_DEFAULTCOLOR     = 0x0000
	LR_LOADFROMFILE     = 0x0010
	LR_CREATEDIBSECTION = 0x2000
	LR_DEFAULTSIZE      = 0x0040

	IMAGE_ICON   = 1
	IMAGE_CURSOR = 2
	IMAGE_BITMAP = 0

	GMEM_FIXED    = 0x0000
	GMEM_MOVEABLE = 0x0002
	GMEM_ZEROINIT = 0x0040

	DT_LEFT          = 0x00000000
	DT_CENTER        = 0x00000001
	DT_RIGHT         = 0x00000002
	DT_TOP           = 0x00000000
	DT_VCENTER       = 0x00000004
	DT_BOTTOM        = 0x00000008
	DT_WORDBREAK     = 0x00000010
	DT_SINGLELINE    = 0x00000020
	DT_EXPANDTABS    = 0x00000040
	DT_TABSTOP       = 0x00000080
	DT_NOCLIP        = 0x00000100
	DT_CALCRECT      = 0x00000400
	DT_PATH_ELLIPSIS = 0x00004000
	DT_END_ELLIPSIS  = 0x00008000
	DT_EDITCONTROL   = 0x00002000

	MB_OK              = 0x00000000
	MB_OKCANCEL        = 0x00000001
	MB_YESNOCANCEL     = 0x00000003
	MB_YESNO           = 0x00000004
	MB_ICONERROR       = 0x00000010
	MB_ICONQUESTION    = 0x00000020
	MB_ICONWARNING     = 0x00000030
	MB_ICONINFORMATION = 0x00000040
	MB_APPLMODAL       = 0x00000000
	MB_TASKMODAL       = 0x00000200
	MB_TOPMOST         = 0x00040000
	IDYES              = 6
	IDNO               = 7

	CP_UTF8 = 65001

	// font weights
	FW_NORMAL = 400
	FW_BOLD   = 700

	// charset
	ANSI_CHARSET        = 0
	DEFAULT_CHARSET     = 1
	OUT_DEFAULT_PRECIS  = 0
	CLIP_DEFAULT_PRECIS = 0
	DEFAULT_QUALITY     = 0
	CLEARTYPE_QUALITY   = 5
	DEFAULT_PITCH       = 0
	FF_DONTCARE         = 0
	FF_SWISS            = 2
	FF_MODERN           = 48

	// SHGetFileInfo
	SHGFI_ICON         = 0x000000100
	SHGFI_DISPLAYNAME  = 0x000000200
	SHGFI_TYPENAME     = 0x000000400
	SHGFI_ATTRIBUTES   = 0x000000800
	SHGFI_ICONLOCATION = 0x000001000
	SHGFI_LARGEICON    = 0x000000000
	SHGFI_SMALLICON    = 0x000000001
	SHGFI_SYSICONINDEX = 0x000004000

	// track mouse
	TME_LEAVE = 0x00000002

	// pen
	PS_SOLID = 0
	PS_DASH  = 1
)

// ---------------------------------------------------------------- helper wrappers
func getModuleHandle() HINSTANCE {
	r, _, _ := callX(procGetModuleHandleW, 1, 0, 0, 0)
	return HINSTANCE(r)
}

func registerClassEx(wc *WNDCLASSEX) (ATOM, error) {
	r, _, err := callX(procRegisterClassExW, 1, uintptr(unsafe.Pointer(wc)), 0, 0)
	if r == 0 {
		return 0, err
	}
	return ATOM(r), nil
}

func createWindowEx(exStyle uint32, className *uint16, windowName *uint16, style uint32,
	x, y, width, height int32, parent windows.HWND, menu HMENU,
	inst HINSTANCE, param uintptr) windows.HWND {
	r, _, _ := callX(procCreateWindowExW, 12,
		uintptr(exStyle),
		uintptr(unsafe.Pointer(className)),
		uintptr(unsafe.Pointer(windowName)),
		uintptr(style),
		uintptr(x), uintptr(y), uintptr(width), uintptr(height),
		uintptr(parent), uintptr(menu), uintptr(inst), uintptr(param))
	return windows.HWND(r)
}

func defWindowProc(hwnd windows.HWND, msg uint32, wParam, lParam uintptr) uintptr {
	r, _, _ := callX(procDefWindowProcW, 4,
		uintptr(hwnd), uintptr(msg), wParam, lParam, 0, 0)
	return r
}

func showWindow(hwnd windows.HWND, cmdShow int32) bool {
	r, _, _ := callX(procShowWindow, 2, uintptr(hwnd), uintptr(cmdShow), 0)
	return r != 0
}

func updateWindow(hwnd windows.HWND) bool {
	r, _, _ := callX(procUpdateWindow, 1, uintptr(hwnd), 0, 0)
	return r != 0
}

func getMessage(msg *MSG, hwnd windows.HWND, msgFilterMin, msgFilterMax uint32) int {
	r, _, _ := callX(procGetMessageW, 4,
		uintptr(unsafe.Pointer(msg)), uintptr(hwnd),
		uintptr(msgFilterMin), uintptr(msgFilterMax), 0, 0)
	return int(int32(r))
}

func translateMessage(msg *MSG) {
	callX(procTranslateMessage, 1, uintptr(unsafe.Pointer(msg)), 0, 0)
}

func dispatchMessage(msg *MSG) {
	callX(procDispatchMessageW, 1, uintptr(unsafe.Pointer(msg)), 0, 0)
}

func postQuitMessage(exitCode int32) {
	callX(procPostQuitMessage, 1, uintptr(exitCode), 0, 0)
}

func sendMessage(hwnd windows.HWND, msg uint32, wParam, lParam uintptr) uintptr {
	r, _, _ := callX(procSendMessageW, 4,
		uintptr(hwnd), uintptr(msg), wParam, lParam, 0, 0)
	return r
}

func postMessage(hwnd windows.HWND, msg uint32, wParam, lParam uintptr) bool {
	r, _, _ := callX(procSendMessageW, 4,
		uintptr(hwnd), uintptr(msg), wParam, lParam, 0, 0)
	return r != 0
}

func destroyWindow(hwnd windows.HWND) bool {
	r, _, _ := callX(procDestroyWindow, 1, uintptr(hwnd), 0, 0)
	return r != 0
}

func setWindowText(hwnd windows.HWND, text string) bool {
	p, _ := windows.UTF16PtrFromString(text)
	r, _, _ := callX(procSetWindowTextW, 2, uintptr(hwnd), uintptr(unsafe.Pointer(p)), 0)
	return r != 0
}

func getWindowTextLength(hwnd windows.HWND) int {
	r, _, _ := callX(procGetWindowTextLengthW, 1, uintptr(hwnd), 0, 0)
	return int(int32(r))
}

func getWindowText(hwnd windows.HWND) string {
	n := getWindowTextLength(hwnd)
	if n == 0 {
		return ""
	}
	buf := make([]uint16, n+1)
	r, _, _ := callX(procGetWindowTextW, 3, uintptr(hwnd),
		uintptr(unsafe.Pointer(&buf[0])), uintptr(n+1))
	return windows.UTF16ToString(buf[:r])
}

func getClientRect(hwnd windows.HWND, rc *RECT) bool {
	r, _, _ := callX(procGetClientRect, 2, uintptr(hwnd),
		uintptr(unsafe.Pointer(rc)), 0)
	return r != 0
}

func getWindowRect(hwnd windows.HWND) (RECT, error) {
	var rc RECT
	r, _, err := callX(procGetWindowRect, 2, uintptr(hwnd),
		uintptr(unsafe.Pointer(&rc)), 0)
	if r == 0 {
		return rc, err
	}
	return rc, nil
}

func moveWindow(hwnd windows.HWND, x, y, w, h int32, repaint bool) bool {
	b := 0
	if repaint {
		b = 1
	}
	r, _, _ := callX(procMoveWindow, 6,
		uintptr(hwnd), uintptr(x), uintptr(y), uintptr(w), uintptr(h), uintptr(b))
	return r != 0
}

func setWindowPos(hwnd windows.HWND, insertAfter windows.HWND, x, y, cx, cy int32, flags uint32) bool {
	r, _, _ := callX(procSetWindowPos, 7,
		uintptr(hwnd), uintptr(insertAfter), uintptr(x), uintptr(y),
		uintptr(cx), uintptr(cy), uintptr(flags), 0, 0)
	return r != 0
}

func loadCursor(inst HINSTANCE, cursorID uint16) HCURSOR {
	r, _, _ := callX(procLoadCursorW, 2, uintptr(inst),
		uintptr(uint32(cursorID)), 0)
	return HCURSOR(r)
}

func loadIcon(inst HINSTANCE, iconID uint16) HICON {
	r, _, _ := callX(procLoadIconW, 2, uintptr(inst),
		uintptr(uint32(iconID)), 0)
	return HICON(r)
}

func getDC(hwnd windows.HWND) HDC {
	r, _, _ := callX(procGetDC, 1, uintptr(hwnd), 0, 0)
	return HDC(r)
}

func releaseDC(hwnd windows.HWND, hdc HDC) int {
	r, _, _ := callX(procReleaseDC, 2, uintptr(hwnd), uintptr(hdc), 0)
	return int(int32(r))
}

func beginPaint(hwnd windows.HWND, ps *PAINTSTRUCT) HDC {
	r, _, _ := callX(procBeginPaint, 2, uintptr(hwnd),
		uintptr(unsafe.Pointer(ps)), 0)
	return HDC(r)
}

func endPaint(hwnd windows.HWND, ps *PAINTSTRUCT) {
	callX(procEndPaint, 2, uintptr(hwnd), uintptr(unsafe.Pointer(ps)), 0)
}

func getSysColor(idx int32) COLORREF {
	r, _, _ := callX(procGetSysColor, 1, uintptr(idx), 0, 0)
	return COLORREF(r)
}

func invalidateRect(hwnd windows.HWND, rect *RECT, erase bool) bool {
	b := 0
	if erase {
		b = 1
	}
	r, _, _ := callX(procInvalidateRect, 3, uintptr(hwnd),
		uintptr(unsafe.Pointer(rect)), uintptr(b))
	return r != 0
}

func getWindowLongPtr(hwnd windows.HWND, index int32) uintptr {
	r, _, _ := callX(procGetWindowLongPtrW, 2, uintptr(hwnd), uintptr(index), 0)
	return r
}

func setWindowLongPtr(hwnd windows.HWND, index int32, value uintptr) uintptr {
	r, _, _ := callX(procSetWindowLongPtrW, 3, uintptr(hwnd),
		uintptr(index), value)
	return r
}

func getSystemMetrics(index int32) int32 {
	r, _, _ := callX(procGetSystemMetrics, 1, uintptr(index), 0, 0)
	return int32(r)
}

func getKeyState(vk int32) int16 {
	r, _, _ := callX(procGetKeyState, 1, uintptr(vk))
	return int16(r)
}

func setScrollInfo(hwnd windows.HWND, bar int32, si *SCROLLINFO) bool {
	r, _, _ := callX(procSetScrollInfo, 4, uintptr(hwnd),
		uintptr(bar), uintptr(unsafe.Pointer(si)), 0, 0, 0)
	return r != 0
}

func getScrollInfo(hwnd windows.HWND, bar int32, si *SCROLLINFO) bool {
	r, _, _ := callX(procGetScrollInfo, 4, uintptr(hwnd),
		uintptr(bar), uintptr(unsafe.Pointer(si)), 0, 0, 0)
	return r != 0
}

func screenToClient(hwnd windows.HWND, pt *POINT) bool {
	r, _, _ := callX(procScreenToClient, 2, uintptr(hwnd),
		uintptr(unsafe.Pointer(pt)), 0)
	return r != 0
}

func clientToScreen(hwnd windows.HWND, pt *POINT) bool {
	r, _, _ := callX(procClientToScreen, 2, uintptr(hwnd),
		uintptr(unsafe.Pointer(pt)), 0)
	return r != 0
}

func setCapture(hwnd windows.HWND) windows.HWND {
	r, _, _ := callX(procSetCapture, 1, uintptr(hwnd), 0, 0)
	return windows.HWND(r)
}

func releaseCapture() bool {
	r, _, _ := callX(procReleaseCapture, 0, 0, 0, 0)
	return r != 0
}

func setCursor(cur HCURSOR) HCURSOR {
	r, _, _ := callX(procSetCursor, 1, uintptr(cur), 0, 0)
	return HCURSOR(r)
}

func enableWindow(hwnd windows.HWND, enable bool) bool {
	b := 0
	if enable {
		b = 1
	}
	r, _, _ := callX(procEnableWindow, 2, uintptr(hwnd), uintptr(b), 0)
	return r != 0
}

func setFocus(hwnd windows.HWND) windows.HWND {
	r, _, _ := callX(procSetFocus, 1, uintptr(hwnd), 0, 0)
	return windows.HWND(r)
}

func setForegroundWindow(hwnd windows.HWND) bool {
	r, _, _ := callX(procSetForegroundWindow, 1, uintptr(hwnd), 0, 0)
	return r != 0
}

func getParent(hwnd windows.HWND) windows.HWND {
	r, _, _ := callX(procGetParent, 1, uintptr(hwnd), 0, 0)
	return windows.HWND(r)
}

func isWindow(hwnd windows.HWND) bool {
	r, _, _ := callX(procIsWindow, 1, uintptr(hwnd), 0, 0)
	return r != 0
}

func getDlgItem(hwnd windows.HWND, id int32) windows.HWND {
	r, _, _ := callX(procGetDlgItem, 2, uintptr(hwnd), uintptr(id), 0)
	return windows.HWND(r)
}

func getWindow(hwnd windows.HWND, cmd uint32) windows.HWND {
	r, _, _ := callX(procGetWindow, 2, uintptr(hwnd), uintptr(cmd), 0)
	return windows.HWND(r)
}

func setWindowLong(hwnd windows.HWND, index int32, value uintptr) uintptr {
	return setWindowLongPtr(hwnd, index, value)
}

func getWindowLong(hwnd windows.HWND, index int32) uintptr {
	return getWindowLongPtr(hwnd, index)
}

func messageBox(hwnd windows.HWND, text, caption string, flags uint32) int {
	pt, _ := windows.UTF16PtrFromString(text)
	pc, _ := windows.UTF16PtrFromString(caption)
	r, _, _ := callX(procMessageBoxW, 4, uintptr(hwnd),
		uintptr(unsafe.Pointer(pt)), uintptr(unsafe.Pointer(pc)), uintptr(flags), 0, 0)
	return int(int32(r))
}

// ---------------------------------------------------------------- gdi wrappers
func createFontIndirect(lf *LOGFONT) HFONT {
	r, _, _ := callX(procCreateFontIndirectW, 1, uintptr(unsafe.Pointer(lf)), 0, 0)
	return HFONT(r)
}

// createCompatibleDC / createDIBSection / deleteDC —— 供离屏渲染诊断用
func createCompatibleDC(hdc HDC) HDC {
	r, _, _ := callX(procCreateCompatibleDC, 1, uintptr(hdc), 0, 0)
	return HDC(r)
}

func deleteDC(hdc HDC) bool {
	r, _, _ := callX(procDeleteDC, 1, uintptr(hdc), 0, 0)
	return r != 0
}

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

func createDIBSection(hdc HDC, hdr *BITMAPINFOHEADER, usage uint32, bitsOut *uintptr, section uintptr, offset uint32) HBITMAP {
	r, _, _ := callX(procCreateDIBSection, 6, uintptr(hdc),
		uintptr(unsafe.Pointer(hdr)), uintptr(usage),
		uintptr(unsafe.Pointer(bitsOut)), section, uintptr(offset))
	return HBITMAP(r)
}

func selectObject(hdc HDC, obj HGDIOBJ) HGDIOBJ {
	r, _, _ := callX(procSelectObject, 2, uintptr(hdc), uintptr(obj), 0)
	return HGDIOBJ(r)
}

func deleteObject(obj HGDIOBJ) bool {
	r, _, _ := callX(procDeleteObject, 1, uintptr(obj), 0, 0)
	return r != 0
}

func getTextExtentPoint32(hdc HDC, text string) (int32, int32) {
	pts, _ := windows.UTF16FromString(text)
	var sz SIZE
	// 注意：UTF16FromString 返回的切片带 1 个结尾 NUL；必须把长度减 1，
	// 否则 GetTextExtentPoint32W 会把 NUL 字形的步进（雅黑约 7px）也算进宽度，
	// 每个排版单元都虚增 ~7px，累加后表现为“字间距偏大”。
	callX(procGetTextExtentPoint32W, 4, uintptr(hdc),
		uintptr(unsafe.Pointer(&pts[0])), uintptr(len(pts)-1), uintptr(unsafe.Pointer(&sz)), 0, 0)
	return sz.X, sz.Y
}

func setTextColor(hdc HDC, color COLORREF) COLORREF {
	r, _, _ := callX(procSetTextColor, 2, uintptr(hdc), uintptr(color), 0)
	return COLORREF(r)
}

func setBkMode(hdc HDC, mode int32) int32 {
	r, _, _ := callX(procSetBkMode, 2, uintptr(hdc), uintptr(mode), 0)
	return int32(r)
}

func setBkColor(hdc HDC, color COLORREF) COLORREF {
	r, _, _ := callX(procSetBkColor, 2, uintptr(hdc), uintptr(color), 0)
	return COLORREF(r)
}

func textOut(hdc HDC, x, y int32, text string) bool {
	pts, _ := windows.UTF16FromString(text)
	r, _, _ := callX(procTextOutW, 4, uintptr(hdc), uintptr(x), uintptr(y),
		uintptr(unsafe.Pointer(&pts[0])), uintptr(len(pts)-1), 0)
	return r != 0
}

func extTextOut(hdc HDC, x, y int32, options uint32, rect *RECT, text string, dx []int32) bool {
	pts, _ := windows.UTF16FromString(text)
	var pdx uintptr
	if len(dx) > 0 {
		pdx = uintptr(unsafe.Pointer(&dx[0]))
	}
	r, _, _ := callX(procExtTextOutW, 7, uintptr(hdc), uintptr(x), uintptr(y),
		uintptr(options), uintptr(unsafe.Pointer(rect)),
		uintptr(unsafe.Pointer(&pts[0])), uintptr(len(pts)-1), pdx, 0)
	return r != 0
}

func createSolidBrush(color COLORREF) HBRUSH {
	r, _, _ := callX(procCreateSolidBrush, 1, uintptr(color), 0, 0)
	return HBRUSH(r)
}

func fillRect(hdc HDC, rect *RECT, brush HBRUSH) int32 {
	r, _, _ := callX(procFillRect, 3, uintptr(hdc),
		uintptr(unsafe.Pointer(rect)), uintptr(brush))
	return int32(r)
}

func rectangle(hdc HDC, left, top, right, bottom int32) bool {
	r, _, _ := callX(procRectangle, 5, uintptr(hdc),
		uintptr(left), uintptr(top), uintptr(right), uintptr(bottom), 0)
	return r != 0
}

func roundRect(hdc HDC, left, top, right, bottom, w, h int32) bool {
	r, _, _ := callX(procRoundRect, 7, uintptr(hdc),
		uintptr(left), uintptr(top), uintptr(right), uintptr(bottom), uintptr(w), uintptr(h), 0)
	return r != 0
}

func ellipse(hdc HDC, left, top, right, bottom int32) bool {
	r, _, _ := callX(procEllipse, 5, uintptr(hdc),
		uintptr(left), uintptr(top), uintptr(right), uintptr(bottom), 0)
	return r != 0
}

// ---- image list ----
func imageListCreate(cx, cy int32, flags, initial int32) HIMAGELIST {
	r, _, _ := callX(procImageList_Create, 5, uintptr(cx), uintptr(cy),
		uintptr(flags), uintptr(initial), uintptr(4), 0)
	return HIMAGELIST(r)
}

func imageListAddMasked(himl HIMAGELIST, hbm HBITMAP, mask COLORREF) int32 {
	r, _, _ := callX(procImageList_AddMasked, 3, uintptr(himl),
		uintptr(hbm), uintptr(mask), 0)
	return int32(r)
}

func imageListDestroy(himl HIMAGELIST) bool {
	r, _, _ := callX(procImageList_Destroy, 1, uintptr(himl), 0, 0)
	return r != 0
}

func createPen(style int32, width int32, color COLORREF) HPEN {
	r, _, _ := callX(procCreatePen, 3, uintptr(style), uintptr(width), uintptr(color))
	return HPEN(r)
}

func moveToEx(hdc HDC, x, y int32, pt *POINT) bool {
	r, _, _ := callX(procMoveToEx, 4, uintptr(hdc), uintptr(x), uintptr(y),
		uintptr(unsafe.Pointer(pt)), 0, 0)
	return r != 0
}

func lineTo(hdc HDC, x, y int32) bool {
	r, _, _ := callX(procLineTo, 3, uintptr(hdc), uintptr(x), uintptr(y))
	return r != 0
}

func getDeviceCaps(hdc HDC, index int32) int32 {
	r, _, _ := callX(procGetDeviceCaps, 2, uintptr(hdc), uintptr(index), 0)
	return int32(r)
}

func getStockObject(index int32) HGDIOBJ {
	r, _, _ := callX(procGetStockObject, 1, uintptr(index), 0, 0)
	return HGDIOBJ(r)
}

func setTextAlign(hdc HDC, align int32) int32 {
	r, _, _ := callX(procSetTextAlign, 2, uintptr(hdc), uintptr(align), 0)
	return int32(r)
}

func getTextMetrics(hdc HDC, tm *TEXTMETRIC) bool {
	r, _, _ := callX(procGetTextMetricsW, 2, uintptr(hdc),
		uintptr(unsafe.Pointer(tm)), 0)
	return r != 0
}

// ---------------------------------------------------------------- comctl32
type INITCOMMONCONTROLSEX struct {
	DwSize uint32
	DwICC  uint32
}

func initCommonControlsEx(icc *INITCOMMONCONTROLSEX) bool {
	r, _, _ := callX(procInitCommonControlsEx, 1, uintptr(unsafe.Pointer(icc)), 0, 0)
	return r != 0
}

// ---------------------------------------------------------------- menu wrappers
func createMenu() HMENU {
	r, _, _ := callX(procCreateMenu, 0, 0, 0, 0)
	return HMENU(r)
}

func createPopupMenu() HMENU {
	r, _, _ := callX(procCreatePopupMenu, 0, 0, 0, 0)
	return HMENU(r)
}

func appendMenu(menu HMENU, flags uint32, idOrSubmenu uintptr, text string) bool {
	var p *uint16
	if text != "" {
		p, _ = windows.UTF16PtrFromString(text)
	}
	r, _, _ := callX(procAppendMenuW, 4, uintptr(menu), uintptr(flags),
		idOrSubmenu, uintptr(unsafe.Pointer(p)), 0, 0)
	return r != 0
}

func setMenu(hwnd windows.HWND, menu HMENU) bool {
	r, _, _ := callX(procSetMenu, 2, uintptr(hwnd), uintptr(menu), 0)
	return r != 0
}

func drawMenuBar(hwnd windows.HWND) bool {
	r, _, _ := callX(procDrawMenuBar, 1, uintptr(hwnd), 0, 0)
	return r != 0
}

func checkMenuItem(menu HMENU, id uint32, check bool) bool {
	f := uint32(MF_BYCOMMAND | MF_UNCHECKED)
	if check {
		f = MF_BYCOMMAND | MF_CHECKED
	}
	r, _, _ := callX(procCheckMenuItem, 3, uintptr(menu), uintptr(id), uintptr(f))
	return r != 0
}

func getSubMenu(menu HMENU, pos int32) HMENU {
	r, _, _ := callX(procGetSubMenu, 2, uintptr(menu), uintptr(pos), 0)
	return HMENU(r)
}

// ---------------------------------------------------------------- Structures not in x/sys/windows or needing W suffix
type TEXTMETRIC struct {
	TmHeight           int32
	TmAscent           int32
	TmDescent          int32
	TmInternalLeading  int32
	TmExternalLeading  int32
	TmAveCharWidth     int32
	TmMaxCharWidth     int32
	TmWeight           int32
	TmOverhang         int32
	TmDigitizedAspectX int32
	TmDigitizedAspectY int32
	TmFirstChar        uint16
	TmLastChar         uint16
	TmDefaultChar      uint16
	TmBreakChar        uint16
	TmItalic           byte
	TmUnderlined       byte
	TmStruckOut        byte
	TmPitchAndFamily   byte
	TmCharSet          byte
}

type SCROLLINFO struct {
	CbSize    uint32
	FMask     uint32
	NMin      int32
	NMax      int32
	NPage     uint32
	NPos      int32
	NTrackPos int32
}

type NMHDR struct {
	HwndFrom windows.HWND
	IdFrom   uintptr
	Code     uint32
}

type NMITEMACTIVATE struct {
	Hdr       NMHDR
	IItem     int32
	ISubItem  int32
	UNewState uint32
	UOldState uint32
	UChanged  uint32
	PtAction  POINT
	LParam    uintptr
}

type LVITEM struct {
	Mask       uint32
	IItem      int32
	ISubItem   int32
	State      uint32
	StateMask  uint32
	PszText    *uint16
	CchTextMax int32
	IImage     int32
	LParam     uintptr
	IIndent    int32
}

type LVCOLUMN struct {
	Mask       uint32
	Fmt        int32
	Cx         int32
	PszText    *uint16
	CchTextMax int32
	ISubItem   int32
	IImage     int32
	IOrder     int32
}

type TBBUTTON struct {
	IBitmap   int32
	IdCommand int32
	FsState   byte
	FsStyle   byte
	BReserved [2]byte
	DwData    uintptr
	IString   int32
}

type TOOLINFO struct {
	CbSize   uint32
	UFlags   uint32
	Hwnd     windows.HWND
	UId      uintptr
	Rect     RECT
	Hinst    HINSTANCE
	LpszText *uint16
	LParam   uintptr
}

type OPENFILENAME struct {
	LStructSize       uint32
	HwndOwner         windows.HWND
	HInstance         HINSTANCE
	LpstrFilter       *uint16
	LpstrCustomFilter *uint16
	NMaxCustFilter    uint32
	NFilterIndex      uint32
	LpstrFile         *uint16
	NMaxFile          uint32
	LpstrFileTitle    *uint16
	NMaxFileTitle     uint32
	LpstrInitialDir   *uint16
	LpstrTitle        *uint16
	Flags             uint32
	NFileOffset       uint16
	NFileExtension    uint16
	LpstrDefExt       *uint16
	LCustData         uintptr
	LpfnHook          uintptr
	LpTemplateName    *uint16
	PvReserved        uintptr
	DwReserved        uint32
	FlagsEx           uint32
}

// 注意：x64 上 sizeof(OPENFILENAMEW) = 152（见 SDK commdlg.h：
// 结尾只有 pvReserved + dwReserved + FlagsEx，没有 lpEditInfo/lpstrPrompt，
// dwReserved 也只是单个 DWORD）。此前误写成 [3]uint32 使结构体变成 160 字节、
// FlagsEx 偏移错位 —— 这正是当年 GetOpenFileNameW “未弹窗即崩”的头号嫌疑。

type SHFILEINFO struct {
	HIcon         HICON
	IIcon         int32
	DwAttributes  uint32
	SzDisplayName [260]uint16
	SzTypeName    [80]uint16
}

type TRACKMOUSEEVENT struct {
	CbSize      uint32
	DwFlags     uint32
	HwndTrack   windows.HWND
	DwHoverTime uint32
}

func trackMouseEvent(tme *TRACKMOUSEEVENT) bool {
	r, _, _ := callX(procTrackMouseEvent, 1, uintptr(unsafe.Pointer(tme)), 0, 0)
	return r != 0
}

func wideCharToMultiByte(str string) []byte {
	if str == "" {
		return []byte{0}
	}
	pts, _ := windows.UTF16FromString(str)
	// convert UTF16 to UTF8
	var buf []byte
	for _, c := range pts {
		if c == 0 {
			break
		}
		// encode rune
		r := rune(c)
		if r >= 0xD800 && r <= 0xDBFF && false {
		}
		buf = appendRune(buf, r)
	}
	buf = append(buf, 0)
	return buf
}

func appendRune(b []byte, r rune) []byte {
	if r < 0x80 {
		return append(b, byte(r))
	}
	var tmp [4]byte
	n := encodeRune(tmp[:], r)
	return append(b, tmp[:n]...)
}

func encodeRune(p []byte, r rune) int {
	switch {
	case r < 0x80:
		p[0] = byte(r)
		return 1
	case r < 0x800:
		p[0] = 0xC0 | byte(r>>6)
		p[1] = 0x80 | byte(r)&0x3F
		return 2
	case r < 0x10000:
		p[0] = 0xE0 | byte(r>>12)
		p[1] = 0x80 | byte(r>>6)&0x3F
		p[2] = 0x80 | byte(r)&0x3F
		return 3
	default:
		p[0] = 0xF0 | byte(r>>18)
		p[1] = 0x80 | byte(r>>12)&0x3F
		p[2] = 0x80 | byte(r>>6)&0x3F
		p[3] = 0x80 | byte(r)&0x3F
		return 4
	}
}

func getOpenFileName(ofn *OPENFILENAME) bool {
	r, _, _ := callX(procGetOpenFileNameW, 1, uintptr(unsafe.Pointer(ofn)), 0, 0)
	return r != 0
}

func commDlgExtendedError() uint32 {
	r, _, _ := callX(procCommDlgExtendedError, 0, 0, 0, 0)
	return uint32(r)
}

// ---------------------------------------------------------------- local Win32 type aliases (not exported by this x/sys/windows version)
type (
	HWND       = windows.HWND
	HINSTANCE  = windows.Handle
	HICON      = windows.Handle
	HCURSOR    = windows.Handle
	HDC        = windows.Handle
	HBRUSH     = windows.Handle
	HGDIOBJ    = windows.Handle
	HMENU      = windows.Handle
	HFONT      = windows.Handle
	HPEN       = windows.Handle
	HBITMAP    = windows.Handle
	HIMAGELIST = windows.Handle
	COLORREF   = uint32
	ATOM       = uint16
)

type RECT struct {
	Left   int32
	Top    int32
	Right  int32
	Bottom int32
}

type POINT struct {
	X int32
	Y int32
}

type SIZE struct {
	X int32
	Y int32
}

type WNDCLASSEX struct {
	CbSize        uint32
	Style         uint32
	LpfnWndProc   uintptr
	CbClsExtra    int32
	CbWndExtra    int32
	HInstance     HINSTANCE
	HIcon         HICON
	HCursor       HCURSOR
	HbrBackground HBRUSH
	LpszMenuName  *uint16
	LpszClassName *uint16
	HIconSm       HICON
}

type MSG struct {
	Hwnd    windows.HWND
	Message uint32
	WParam  uintptr
	LParam  uintptr
	Time    uint32
	Pt      POINT
}

type PAINTSTRUCT struct {
	Hdc         HDC
	FErase      int32
	RcPaint     RECT
	FRestore    int32
	FIncUpdate  int32
	RgbReserved [32]byte
}

type LOGFONT struct {
	Height         int32
	Width          int32
	Escapement     int32
	Orientation    int32
	Weight         int32
	Italic         byte
	Underline      byte
	StrikeOut      byte
	CharSet        byte
	OutPrecision   byte
	ClipPrecision  byte
	Quality        byte
	PitchAndFamily byte
	FaceName       [32]uint16
}

// helper to build LOGFONT face name
func fontFace(name string) [32]uint16 {
	var f [32]uint16
	c := []uint16{}
	if name != "" {
		c, _ = windows.UTF16FromString(name)
	}
	copy(f[:], c)
	return f
}

// callX dispatches to the appropriate std-lib syscall wrapper based on the
// number of real arguments (nargs). Extra trailing zero args are harmless on
// the Windows x64 calling convention (caller-cleaned).
func callX(proc *windows.LazyProc, nargs uintptr, args ...uintptr) (uintptr, uintptr, error) {
	_ = nargs
	return proc.Call(args...)
}

// ---------------------------------------------------------------- ShellExecute / folder browse
const (
	BIF_RETURNONLYFSDIRS  = 0x00000001
	BIF_NEWDIALOGSTYLE    = 0x00000040
	BIF_USENEWUI          = BIF_NEWDIALOGSTYLE | BIF_RETURNONLYFSDIRS
	BIF_NONEWFOLDERBUTTON = 0x00000200
	SW_SHOWNORMAL_F       = 1
)

type BROWSEINFO struct {
	HwndOwner      HWND
	PidlRoot       uintptr
	PszDisplayName *uint16
	LpszTitle      *uint16
	UlFlags        uint32
	Lpfn           uintptr
	LParam         uintptr
	IImage         int32
}

func shellExecute(hwnd HWND, verb, file, params, dir string, showCmd int) uintptr {
	pv, _ := windows.UTF16PtrFromString(verb)
	pf, _ := windows.UTF16PtrFromString(file)
	pp, _ := windows.UTF16PtrFromString(params)
	pd, _ := windows.UTF16PtrFromString(dir)
	r, _, _ := callX(procShellExecuteW, 6,
		uintptr(hwnd), uintptr(unsafe.Pointer(pv)),
		uintptr(unsafe.Pointer(pf)), uintptr(unsafe.Pointer(pp)),
		uintptr(unsafe.Pointer(pd)), uintptr(showCmd))
	return r
}

func browseFolder(owner HWND, title string) string {
	var bi BROWSEINFO
	bi.HwndOwner = owner
	bi.UlFlags = BIF_USENEWUI
	pt, _ := windows.UTF16PtrFromString(title)
	bi.LpszTitle = pt
	pidl, _, _ := callX(procSHBrowseForFolderW, 1, uintptr(unsafe.Pointer(&bi)))
	if pidl == 0 {
		return ""
	}
	buf := make([]uint16, 260)
	r, _, _ := callX(procSHGetPathFromIDListW, 2, pidl, uintptr(unsafe.Pointer(&buf[0])))
	if r == 0 {
		return ""
	}
	return windows.UTF16ToString(buf)
}

// ---------------------------------------------------------------- registry
const (
	HKEY_CURRENT_USER = 0x80000001

	KEY_QUERY_VALUE         = 0x0001
	KEY_SET_VALUE           = 0x0002
	KEY_READ                = 0x00020019
	KEY_WRITE               = 0x00020006
	REG_OPTION_NON_VOLATILE = 0x00000000
	REG_SZ                  = 1
)

// regSetString 在 root\subKey 下写入 REG_SZ 值 name=value（键不存在则创建）。
func regSetString(root uintptr, subKey, name, value string) error {
	var hkey uintptr
	var disp uint32
	sk, _ := windows.UTF16PtrFromString(subKey)
	ret, _, _ := callX(procRegCreateKeyExW, 9,
		root, uintptr(unsafe.Pointer(sk)), 0, 0,
		REG_OPTION_NON_VOLATILE, KEY_WRITE, 0,
		uintptr(unsafe.Pointer(&hkey)), uintptr(unsafe.Pointer(&disp)))
	if ret != 0 {
		return fmt.Errorf("RegCreateKeyExW(%s) 失败: %d", subKey, ret)
	}
	defer callX(procRegCloseKey, 1, hkey)

	nm, _ := windows.UTF16PtrFromString(name)
	// Windows 要求 REG_SZ 的数据长度包含结尾 NUL。
	data, _ := windows.UTF16FromString(value)
	cb := uint32(len(data) * 2)
	ret, _, _ = callX(procRegSetValueExW, 6,
		hkey, uintptr(unsafe.Pointer(nm)), 0, REG_SZ,
		uintptr(unsafe.Pointer(&data[0])), uintptr(cb))
	if ret != 0 {
		return fmt.Errorf("RegSetValueExW(%s) 失败: %d", name, ret)
	}
	return nil
}

// regGetString 读取 REG_SZ 值；键或值不存在时返回错误。
func regGetString(root uintptr, subKey, name string) (string, error) {
	var hkey uintptr
	sk, _ := windows.UTF16PtrFromString(subKey)
	ret, _, _ := callX(procRegOpenKeyExW, 5,
		root, uintptr(unsafe.Pointer(sk)), 0, KEY_READ, uintptr(unsafe.Pointer(&hkey)))
	if ret != 0 {
		return "", fmt.Errorf("RegOpenKeyExW(%s) 失败: %d", subKey, ret)
	}
	defer callX(procRegCloseKey, 1, hkey)

	nm, _ := windows.UTF16PtrFromString(name)
	var size uint32
	ret, _, _ = callX(procRegQueryValueExW, 6,
		hkey, uintptr(unsafe.Pointer(nm)), 0, 0, 0, uintptr(unsafe.Pointer(&size)))
	if ret != 0 {
		return "", fmt.Errorf("RegQueryValueExW(%s) 取长度失败: %d", name, ret)
	}
	if size == 0 {
		return "", nil
	}
	buf := make([]uint16, size/2+1)
	ret, _, _ = callX(procRegQueryValueExW, 6,
		hkey, uintptr(unsafe.Pointer(nm)), 0, 0,
		uintptr(unsafe.Pointer(&buf[0])), uintptr(unsafe.Pointer(&size)))
	if ret != 0 {
		return "", fmt.Errorf("RegQueryValueExW(%s) 取数据失败: %d", name, ret)
	}
	return windows.UTF16ToString(buf), nil
}

// regDeleteValue 删除某个值（供测试清理用）。
func regDeleteValue(root uintptr, subKey, name string) {
	var hkey uintptr
	sk, _ := windows.UTF16PtrFromString(subKey)
	ret, _, _ := callX(procRegOpenKeyExW, 5,
		root, uintptr(unsafe.Pointer(sk)), 0, KEY_SET_VALUE, uintptr(unsafe.Pointer(&hkey)))
	if ret != 0 {
		return
	}
	defer callX(procRegCloseKey, 1, hkey)
	nm, _ := windows.UTF16PtrFromString(name)
	callX(procRegDeleteValueW, 2, hkey, uintptr(unsafe.Pointer(nm)))
}

// ---------------------------------------------------------------- drag & drop
// dragAcceptFiles 声明窗口接受拖放的文件（WM_DROPFILES）。
func dragAcceptFiles(hwnd HWND, accept bool) {
	v := uintptr(0)
	if accept {
		v = 1
	}
	callX(procDragAcceptFiles, 2, uintptr(hwnd), v)
}

// dragQueryFileCount 返回拖放中的文件数。
func dragQueryFileCount(hdrop uintptr) int {
	r, _, _ := callX(procDragQueryFileW, 4, hdrop, 0xFFFFFFFF, 0, 0)
	return int(r)
}

// dragQueryFileName 返回拖放中第 i 个文件的完整路径。
func dragQueryFileName(hdrop uintptr, i int) string {
	n, _, _ := callX(procDragQueryFileW, 4, hdrop, uintptr(i), 0, 0)
	if n == 0 {
		return ""
	}
	buf := make([]uint16, n+1)
	callX(procDragQueryFileW, 4, hdrop, uintptr(i), uintptr(unsafe.Pointer(&buf[0])), uintptr(n+1))
	return windows.UTF16ToString(buf)
}

func dragFinish(hdrop uintptr) {
	callX(procDragFinish, 1, hdrop)
}

// ---------------------------------------------------------------- clipboard
const CF_UNICODETEXT = 13

// setClipboardText 把文本以 CF_UNICODETEXT 放入剪贴板。
// 交给剪贴板的内存由系统接管，成功后不能再释放。
func setClipboardText(s string) bool {
	data, _ := windows.UTF16FromString(s)
	h := globalAlloc(0x0002 /*GMEM_MOVEABLE*/, uintptr(len(data)*2))
	if h == 0 {
		return false
	}
	p := globalLock(h)
	if p == 0 {
		globalFree(h)
		return false
	}
	dst := unsafe.Slice((*uint16)(unsafe.Pointer(p)), len(data))
	copy(dst, data)
	globalUnlock(h)

	if r, _, _ := callX(procOpenClipboard, 1, 0, 0, 0); r == 0 {
		globalFree(h)
		return false
	}
	callX(procEmptyClipboard, 0)
	callX(procSetClipboardData, 2, CF_UNICODETEXT, h, 0)
	callX(procCloseClipboard, 0)
	return true
}

// globalAlloc / GlobalLock / GlobalUnlock / GlobalFree
func globalAlloc(flags uint32, bytes uintptr) uintptr {
	r, _, _ := callX(procGlobalAlloc, 2, uintptr(flags), bytes, 0)
	return r
}

func globalLock(h uintptr) uintptr {
	r, _, _ := callX(procGlobalLock, 1, h, 0, 0)
	return r
}

func globalUnlock(h uintptr) bool {
	r, _, _ := callX(procGlobalUnlock, 1, h, 0, 0)
	return r != 0
}

func globalFree(h uintptr) uintptr {
	r, _, _ := callX(procGlobalFree, 1, h, 0, 0)
	return r
}

// loadImageIcon 从可执行文件资源加载图标（id 为资源 ID，即 MAKEINTRESOURCE）。
func loadImageIcon(inst HINSTANCE, id uint16, cx, cy int32) HICON {
	r, _, _ := callX(procLoadImageW, 6,
		uintptr(inst), uintptr(uint32(id)), IMAGE_ICON,
		uintptr(cx), uintptr(cy), 0)
	return HICON(r)
}
