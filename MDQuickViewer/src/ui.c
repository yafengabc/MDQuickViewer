/* MDQuickViewer 窗口外壳：主窗口 + 预览子窗口、工具栏、状态栏、左侧文件列表、
 * 菜单、布局、文件对话框、拖放、缩放、滚动、文本选择与复制、注册表记忆、
 * 命令行参数。
 *
 * 主窗口逻辑；纯 C + Win32，不依赖框架。
 *
 * 关键修复（与 Go 版崩溃同源）：所有传给 MoveWindow 的宽高都先夹到 >= 0，
 * 列表/预览宽度拆分用 split_list_preview 保证两者之和不超过 cw（见布局函数）。 */
#include "ui.h"
#include "win32.h"
#include "model.h"
#include "render.h"
#include "settings.h"
#include "dlg.h"
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifndef TB_ADDSTRINGW
#define TB_ADDSTRINGW (WM_USER + 38)
#endif
#ifndef TB_ADDBUTTONSW
#define TB_ADDBUTTONSW (WM_USER + 68)
#endif
#ifndef ICC_WIN95_CLASSES
#define ICC_WIN95_CLASSES 0x000000FF
#endif

/* 命令 ID */
#define IDM_OPENFILE   1001
#define IDM_OPENFOLDER 1002
#define IDM_RELOAD     1003
#define IDM_COPY       1004
#define IDM_SELECTALL  1005
#define IDM_EXIT       1009
#define IDM_TOGGLELIST 1010
#define IDM_ZOOMIN     1020
#define IDM_ZOOMOUT    1021
#define IDM_ZOOMRESET  1022
#define IDM_ABOUT      1030

#define MDQV_CLASS_MAIN    L"MDQuickViewerMainWindow"
#define MDQV_CLASS_PREVIEW L"MDQuickViewerPreviewWindow"
#define MIN_PREVIEW_W      100

/* 全局 UI 状态 */
typedef struct {
    HINSTANCE hinst;
    HWND main, toolbar, status, list, preview;
    HMENU menu;
    HIMAGELIST il;   /* 工具栏图标列表（应用生命周期内持有） */
    char *folder;    /* 当前目录（UTF-8，malloc） */
    char **files;    /* 目录下的 md 文件名（UTF-8，malloc） */
    int nfiles, capfiles;
    Doc *doc;
    int scroll;
    int panelW;
    int showList;
    int mouseX, mouseY;
    /* 文本选择状态 */
    SelPos anchor, head;
    int selOn, selDrag;
} App;

static App g;

/* 前向声明 */
static void set_status(const char *fmt, ...);
static void populate_list(const char *folder);
static void relayout(void);
static void layout_children(void);
static void clamp_scroll(void);
static void update_scroll_range(void);
static void on_command(int id);
static void on_keydown(WPARAM wp);
static void open_link(const char *dest);
static void copy_selection(void);
static void select_all_text(void);
static int is_markdown(const char *name);

/* ----------------------------------------------------------- 小工具 */
static char *w16to8(const wchar_t *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return NULL;
    char *out = (char *)malloc((size_t)n);
    if (!out) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, n, NULL, NULL);
    return out;
}

static void set_wtext_utf8(HWND hwnd, const char *s) {
    wchar_t *w = u8s(s);
    if (w) { SetWindowTextW(hwnd, w); u16free(w); }
}

/* 取 path 的目录部分到 out */
static void dirname_of(const char *path, char *out, int outsz) {
    char tmp[4096];
    if ((int)strlen(path) >= (int)sizeof(tmp)) tmp[0] = '\0';
    else strcpy(tmp, path);
    char *bs = strrchr(tmp, '\\');
    if (bs) *bs = '\0';
    else { out[0] = '.'; out[1] = '\0'; return; }
    size_t plen = strlen(tmp);
    size_t n = plen < (size_t)outsz - 1 ? plen : (size_t)outsz - 1;
    memcpy(out, tmp, n);
    out[n] = '\0';
}

static int is_markdown(const char *name) {
    int len = (int)strlen(name);
    if (len >= 3 && _stricmp(name + len - 3, ".md") == 0) return 1;
    if (len >= 9 && _stricmp(name + len - 9, ".markdown") == 0) return 1;
    return 0;
}

/* 把命令行参数字符串规范化成绝对路径（UTF-8，malloc） */
static char *abs_path_w(const wchar_t *a) {
    wchar_t out[MAX_PATH];
    DWORD n = GetFullPathNameW(a, MAX_PATH, out, NULL);
    if (n == 0 || n >= MAX_PATH) return NULL;
    return w16to8(out);
}

/* ----------------------------------------------------------- 选择状态 */
static void clear_selection(void) {
    g.selOn = 0;
    g.selDrag = 0;
    g.anchor = (SelPos){0, 0, 0};
    g.head = (SelPos){0, 0, 0};
}

static void ordered_sel(SelPos *a, SelPos *b) {
    if (rd_sel_less(g.head, g.anchor)) { *a = g.head; *b = g.anchor; }
    else { *a = g.anchor; *b = g.head; }
}

static int set_clipboard_text(const char *utf8) {
    if (!OpenClipboard(g.main)) return 0;
    EmptyClipboard();
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (n <= 0) { CloseClipboard(); return 0; }
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)n * sizeof(wchar_t));
    if (!h) { CloseClipboard(); return 0; }
    wchar_t *w = (wchar_t *)GlobalLock(h);
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, n);
    GlobalUnlock(h);
    SetClipboardData(CF_UNICODETEXT, h);
    CloseClipboard();
    return 1;
}

static void copy_selection(void) {
    if (!g.doc || !g.selOn) {
        set_status("没有选中的文本（用鼠标拖选，或 Ctrl+A 全选）");
        return;
    }
    SelPos a, b;
    ordered_sel(&a, &b);
    char *txt = rd_selected_text(g.doc, a, b);
    if (!txt || txt[0] == '\0') {
        set_status("没有选中的文本");
        if (txt) free(txt);
        return;
    }
    if (set_clipboard_text(txt))
        set_status("已复制 %d 个字符到剪贴板", (int)strlen(txt));
    else
        set_status("复制失败：无法打开剪贴板");
    free(txt);
}

static void select_all_text(void) {
    if (!g.doc) return;
    rd_select_all(g.doc, &g.anchor, &g.head);
    g.selOn = !rd_sel_equal(g.anchor, g.head);
    InvalidateRect(g.preview, NULL, TRUE);
    set_status("已全选（Ctrl+C 复制）");
}

/* ----------------------------------------------------------- 状态栏 */
static void set_status(const char *fmt, ...) {
    if (!g.status) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    wchar_t *w = u8s(buf);
    if (w) {
        SendMessageW(g.status, SB_SETTEXTW, 0, (LPARAM)w);
        u16free(w);
    }
}

/* ----------------------------------------------------------- 列表 */
static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

static void populate_list(const char *folder) {
    if (g.folder) free(g.folder);
    g.folder = strdup(folder);

    SendMessageW(g.list, LVM_DELETEALLITEMS, 0, 0);
    for (int i = 0; i < g.nfiles; i++) free(g.files[i]);
    g.nfiles = 0;

    char pattern[4096];
    snprintf(pattern, sizeof(pattern), "%s\\*", folder);
    wchar_t *wpat = u8s(pattern);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(wpat, &fd);
    u16free(wpat);
    if (h == INVALID_HANDLE_VALUE) return;

    char **tmp = NULL;
    int cnt = 0, cap = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        char name[1024];
        if (!WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, sizeof(name), NULL, NULL))
            continue;
        if (!is_markdown(name)) continue;
        if (cnt == cap) {
            cap = cap ? cap * 2 : 16;
            tmp = (char **)realloc(tmp, (size_t)cap * sizeof(char *));
        }
        tmp[cnt++] = strdup(name);
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    qsort(tmp, (size_t)cnt, sizeof(char *), cmp_str);

    g.files = tmp;
    g.nfiles = cnt;
    for (int i = 0; i < cnt; i++) {
        LVITEMW it;
        ZeroMemory(&it, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = i;
        wchar_t *wn = u8s(g.files[i]);
        it.pszText = wn;
        it.lParam = i;
        SendMessageW(g.list, LVM_INSERTITEMW, 0, (LPARAM)&it);
        if (wn) u16free(wn);
    }
}

static void highlight_in_list(const char *path) {
    for (int i = 0; i < g.nfiles; i++) {
        char full[4096];
        snprintf(full, sizeof(full), "%s\\%s", g.folder, g.files[i]);
        if (_stricmp(full, path) == 0) {
            LVITEMW it;
            ZeroMemory(&it, sizeof(it));
            it.mask = LVIF_STATE;
            it.state = LVIS_SELECTED | LVIS_FOCUSED;
            it.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
            SendMessageW(g.list, LVM_SETITEMSTATE, (WPARAM)i, (LPARAM)&it);
            SendMessageW(g.list, LVM_ENSUREVISIBLE, (WPARAM)i, 0);
            return;
        }
    }
}

/* ----------------------------------------------------------- 加载文件 */
/* 读整个文件到 malloc 的缓冲（末尾补 '\0'）。成功返回指针，失败返回 NULL。
 * 路径是 UTF-8；必须 _wfopen + UTF-8→UTF-16 转宽字符，**不能用 ANSI fopen**：
 * ANSI 版会把 UTF-8 字节按当前代码页（GBK/CP936）解释，中文路径必然失败。
 * 单独抽出来是为了让中文路径回归测试能直接覆盖这段代码，不必依赖窗口。 */
char *ui_read_file(const char *path, long *out_len) {
    wchar_t *wpath = u8s(path);
    FILE *f = wpath ? _wfopen(wpath, L"rb") : NULL;
    if (!f) {
        u16free(wpath);
        return NULL;
    }
    u16free(wpath);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = '\0';
    fclose(f);
    if (out_len) *out_len = (long)rd;
    return buf;
}

void ui_load_file(const char *path) {
    long sz = 0;
    char *buf = ui_read_file(path, &sz);
    if (!buf) {
        /* 带上路径和 Win32 错误码，否则「打不开」无从排查。 */
        wchar_t werr[64];
        swprintf(werr, sizeof(werr) / sizeof(werr[0]), L"\n\n(GetLastError=%lu)", GetLastError());
        wchar_t *wpath = u8s(path);
        /* UTF-8 路径的字节数 >= 宽字符数（UTF-16 码元不会多于字节），加余量。 */
        size_t cap = ((size_t)u8len(path) + 96) * sizeof(wchar_t);
        wchar_t *wmsg = (wchar_t *)LocalAlloc(LMEM_FIXED, cap);
        if (wmsg) {
            wcscpy(wmsg, L"无法读取文件：\n");
            if (wpath) wcscat(wmsg, wpath);
            wcscat(wmsg, werr);
            MessageBoxW(g.main, wmsg, L"MDQuickViewer", MB_ICONERROR);
            LocalFree(wmsg);
        }
        u16free(wpath);
        return;
    }

    Doc *d = md_parse(buf, path);
    free(buf);
    if (!d) return;

    if (g.doc) md_doc_free(g.doc);
    g.doc = d;
    g.scroll = 0;
    clear_selection();
    relayout();

    const char *base = strrchr(path, '\\');
    base = base ? base + 1 : path;
    char title[1024];
    snprintf(title, sizeof(title), "MDQuickViewer — %s", base);
    set_wtext_utf8(g.main, title);
    set_status("%s · %ld 字节 · 已加载", base, sz);
    highlight_in_list(path);
    InvalidateRect(g.preview, NULL, TRUE);
}

/* ----------------------------------------------------------- 布局 / 滚动 */
static void clamp_scroll(void) {
    if (!g.doc) { g.scroll = 0; return; }
    int content = (int)g.doc->content_height;
    RECT rc;
    GetClientRect(g.preview, &rc);
    int h = rc.bottom - rc.top;
    int maxs = content - h;
    if (maxs < 0) maxs = 0;
    if (g.scroll < 0) g.scroll = 0;
    if (g.scroll > maxs) g.scroll = maxs;
}

static void update_scroll_range(void) {
    if (!g.doc) return;
    RECT rc;
    GetClientRect(g.preview, &rc);
    int h = rc.bottom - rc.top;
    SCROLLINFO si;
    ZeroMemory(&si, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = (int)g.doc->content_height;
    si.nPage = h > 0 ? h : 1;
    si.nPos = g.scroll;
    SetScrollInfo(g.preview, SB_VERT, &si, TRUE);
}

static void relayout(void) {
    if (!g.doc || !g.preview) return;
    RECT rc;
    GetClientRect(g.preview, &rc);
    int w = rc.right - rc.left;
    if (w <= 0) return;
    rd_layout(g.doc, w);
    update_scroll_range();
}

void split_list_preview(int cw, int panelW, int showList, int *listW, int *prevW) {
    if (cw < 0) cw = 0;
    int lw = panelW;
    if (!showList) lw = 0;
    else {
        int max = cw - MIN_PREVIEW_W;
        if (max > 0 && lw > max) lw = max;
    }
    if (lw < 0) lw = 0;
    if (lw > cw) lw = cw;
    int pw = cw - lw;
    if (pw < 0) pw = 0;
    *listW = lw;
    *prevW = pw;
}

static void layout_children(void) {
    RECT rc;
    GetClientRect(g.main, &rc);
    int cw = rc.right - rc.left; if (cw < 0) cw = 0;
    int ch = rc.bottom - rc.top; if (ch < 0) ch = 0;

    int tbh = 0;
    { RECT t; GetClientRect(g.toolbar, &t); tbh = t.bottom - t.top; }
    if (tbh < 0) tbh = 0;

    int sbh = 22;
    { RECT s; GetClientRect(g.status, &s); sbh = s.bottom - s.top; }
    if (sbh < 18) sbh = 22;

    int parts[1] = { cw };
    SendMessageW(g.status, SB_SETPARTS, 1, (LPARAM)parts);

    int sbY = ch - sbh; if (sbY < 0) sbY = 0;
    MoveWindow(g.toolbar, 0, 0, cw, tbh, TRUE);
    MoveWindow(g.status, 0, sbY, cw, sbh, TRUE);

    int top = tbh;
    int availH = ch - tbh - sbh; if (availH < 0) availH = 0;
    int listW, prevW;
    split_list_preview(cw, g.panelW, g.showList, &listW, &prevW);

    MoveWindow(g.list, 0, top, listW, availH, TRUE);
    if (listW > 0)
        SendMessageW(g.list, LVM_SETCOLUMNWIDTH, 0, (LPARAM)(listW - 6));
    MoveWindow(g.preview, listW, top, prevW, availH, TRUE);

    relayout();
}

static void handle_vscroll(WPARAM wp) {
    RECT rc;
    GetClientRect(g.preview, &rc);
    int page = (rc.bottom - rc.top) * 9 / 10;
    int line = 40;
    switch (LOWORD(wp)) {
        case SB_LINEUP:    g.scroll -= line; break;
        case SB_LINEDOWN:  g.scroll += line; break;
        case SB_PAGEUP:    g.scroll -= page; break;
        case SB_PAGEDOWN:  g.scroll += page; break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK: g.scroll = (int)(short)HIWORD(wp); break;
        default: break;
    }
    clamp_scroll();
    update_scroll_range();
    InvalidateRect(g.preview, NULL, TRUE);
}

static void toggle_list(void) {
    g.showList = !g.showList;
    if (g.showList) { ShowWindow(g.list, SW_SHOW); EnableWindow(g.list, TRUE); }
    else { ShowWindow(g.list, SW_HIDE); EnableWindow(g.list, FALSE); }
    layout_children();
}

static void zoom(double factor) {
    float s = rd_scale() * (float)factor;
    if (s < 0.4f) s = 0.4f;
    if (s > 4.0f) s = 4.0f;
    rd_set_scale(s);
    relayout();
    InvalidateRect(g.preview, NULL, TRUE);
    set_status("缩放 %.0f%%", s * 100.0f);
}

static void zoom_reset(void) {
    rd_set_scale(1.0f);
    relayout();
    InvalidateRect(g.preview, NULL, TRUE);
    set_status("缩放 100%%");
}

static void about(void) {
    MessageBoxW(g.main,
                L"MDQuickViewer — 纯 C + Win32 原生 Markdown 浏览器\n\n"
                L"不依赖 WebView / Electron，GDI 自绘排版。\n"
                L"用 C 语言从零实现，单文件可执行。",
                L"关于 MDQuickViewer", MB_OK | MB_ICONINFORMATION);
}

/* ----------------------------------------------------------- 链接 / 光标 */
static void open_link(const char *dest) {
    if (strncmp(dest, "http://", 7) == 0 || strncmp(dest, "https://", 8) == 0 ||
        strncmp(dest, "mailto:", 7) == 0) {
        wchar_t *w = u8s(dest);
        if (w) {
            ShellExecuteW(g.main, L"open", w, NULL, NULL, SW_SHOWNORMAL);
            u16free(w);
        }
    }
}

static void update_preview_cursor(void) {
    HCURSOR cur;
    if (g.doc) {
        int cy = g.mouseY + g.scroll;
        char buf[1024];
        if (rd_hit_link(g.doc, g.mouseX, cy, buf, (int)sizeof(buf)) > 0)
            cur = LoadCursorW(NULL, IDC_HAND);
        else
            cur = LoadCursorW(NULL, IDC_IBEAM);
    } else {
        cur = LoadCursorW(NULL, IDC_IBEAM);
    }
    SetCursor(cur);
}

/* ----------------------------------------------------------- 命令 / 键盘 */
static void on_command(int id) {
    switch (id) {
        case IDM_OPENFILE: {
            char *p = dlg_open_file(settings_initial_folder(g.folder));
            if (p) {
                char dir[4096];
                dirname_of(p, dir, sizeof(dir));
                settings_save_last_folder(dir);
                populate_list(dir);
                ui_load_file(p);
                free(p);
            }
            break;
        }
        case IDM_OPENFOLDER: {
            char *p = dlg_open_folder(settings_initial_folder(g.folder));
            if (p) {
                settings_save_last_folder(p);
                populate_list(p);
                free(p);
            }
            break;
        }
        case IDM_RELOAD:
            if (g.doc && g.doc->path) ui_load_file(g.doc->path);
            break;
        case IDM_COPY:       copy_selection(); break;
        case IDM_SELECTALL:   select_all_text(); break;
        case IDM_EXIT:       DestroyWindow(g.main); break;
        case IDM_TOGGLELIST: toggle_list(); break;
        case IDM_ZOOMIN:     zoom(1.1); break;
        case IDM_ZOOMOUT:    zoom(1.0 / 1.1); break;
        case IDM_ZOOMRESET:  zoom_reset(); break;
        case IDM_ABOUT:      about(); break;
        default: break;
    }
}

static void on_keydown(WPARAM wp) {
    int ctrl = (GetKeyState(VK_CONTROL) & 0x8000) ? 1 : 0;
    switch ((int)wp) {
        case 'O': if (ctrl) on_command(IDM_OPENFILE); break;
        case 'F': if (ctrl) on_command(IDM_OPENFOLDER); break;
        case 'L': if (ctrl) on_command(IDM_TOGGLELIST); break;
        case 'C': if (ctrl) copy_selection(); break;
        case 'A': if (ctrl) select_all_text(); break;
        case VK_F5: if (g.doc && g.doc->path) ui_load_file(g.doc->path); break;
        case '=': if (ctrl) zoom(1.1); break;
        case '+': if (ctrl) zoom(1.1); break;
        case '-': if (ctrl) zoom(1.0 / 1.1); break;
        case '0': if (ctrl) zoom_reset(); break;
        default: break;
    }
}

static void on_get_minmax_info(LPARAM lp) {
    MINMAXINFO *m = (MINMAXINFO *)lp;
    if (m->ptMinTrackSize.x < MIN_PREVIEW_W + 40) m->ptMinTrackSize.x = MIN_PREVIEW_W + 40;
    if (m->ptMinTrackSize.y < 120) m->ptMinTrackSize.y = 120;
}

static void on_drop_files(HDROP hdrop) {
    int n = DragQueryFileW(hdrop, 0xFFFFFFFF, NULL, 0);
    if (n > 0) {
        wchar_t wbuf[MAX_PATH];
        if (DragQueryFileW(hdrop, 0, wbuf, MAX_PATH)) {
            char *p = w16to8(wbuf);
            if (p) {
                DWORD attr = GetFileAttributesW(wbuf);
                if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
                    settings_save_last_folder(p);
                    populate_list(p);
                } else if (is_markdown(p)) {
                    char dir[4096];
                    dirname_of(p, dir, sizeof(dir));
                    settings_save_last_folder(dir);
                    populate_list(dir);
                    ui_load_file(p);
                }
                free(p);
            }
        }
    }
    DragFinish(hdrop);
}

static void on_list_notify(UINT code) {
    if (code == NM_DBLCLK) {
        int i = (int)SendMessageW(g.list, LVM_GETNEXTITEM, (WPARAM)-1, (LPARAM)LVNI_SELECTED);
        if (i >= 0 && i < g.nfiles) {
            char full[4096];
            snprintf(full, sizeof(full), "%s\\%s", g.folder, g.files[i]);
            ui_load_file(full);
        }
    }
}

/* ----------------------------------------------------------- 控件 / 菜单 */
/* ------------------------------------------------------------------ 工具栏图标
 * 纯 GDI 手绘 16x16 线稿图标，无外部图片依赖。背景填洋红作掩色，
 * 用 ImageList_AddMasked 把洋红变透明。 */

#ifndef TB_SETIMAGELIST
#define TB_SETIMAGELIST (WM_USER + 48)
#endif

#define TB_ICON_SIZE 16
#define TB_ICON_BG   RGB(255, 0, 255)   /* 掩色（透明） */

/* 以下 7 个图标 1:1 移植自 golang 版 src/ui.go 的 drawIconShape()，
 * 包括颜色与形状，确保 C 版工具栏与 Go 版视觉一致。
 * COLR(0xRRGGBB) 等价于 Go 的 COLORREF(0xRRGGBB)。 */

static void tb_glyph_openfile(HDC dc) {
    /* 白色文档 + 深灰边框，灰色三行文字 */
    HPEN pen = CreatePen(PS_SOLID, 1, COLR(0x222222));
    HBRUSH br  = CreateSolidBrush(RGB(255, 255, 255));
    SelectObject(dc, pen); SelectObject(dc, br);
    Rectangle(dc, 3, 2, 12, 14);
    DeleteObject(br);
    HPEN g = CreatePen(PS_SOLID, 1, COLR(0x999999));
    SelectObject(dc, g);
    MoveToEx(dc, 5, 6, NULL); LineTo(dc, 10, 6);
    MoveToEx(dc, 5, 9, NULL); LineTo(dc, 10, 9);
    MoveToEx(dc, 5, 12, NULL); LineTo(dc, 8, 12);
    DeleteObject(g); DeleteObject(pen);
}

static void tb_glyph_openfolder(HDC dc) {
    /* 黄填充 + 棕边的文件夹，棕线画标签页 */
    HPEN pen = CreatePen(PS_SOLID, 1, COLR(0x6B4F00));
    HBRUSH br  = CreateSolidBrush(COLR(0xE0A82A));
    SelectObject(dc, pen); SelectObject(dc, br);
    Rectangle(dc, 2, 5, 14, 13);
    MoveToEx(dc, 4, 5, NULL); LineTo(dc, 7, 5);
    LineTo(dc, 9, 3); LineTo(dc, 14, 3); LineTo(dc, 14, 5);
    DeleteObject(br); DeleteObject(pen);
}

static void tb_glyph_list(HDC dc) {
    /* 白面板 + 灰边，两个灰行块，两行灰文字 */
    HPEN p1 = CreatePen(PS_SOLID, 1, COLR(0x666666));
    HBRUSH b1 = CreateSolidBrush(RGB(255, 255, 255));
    SelectObject(dc, p1); SelectObject(dc, b1);
    Rectangle(dc, 2, 2, 14, 14);
    DeleteObject(b1);
    HBRUSH b2 = CreateSolidBrush(COLR(0x666666));
    SelectObject(dc, b2);
    Rectangle(dc, 4, 5, 6, 7);
    Rectangle(dc, 4, 9, 6, 11);
    DeleteObject(b2);
    HPEN p2 = CreatePen(PS_SOLID, 1, COLR(0x999999));
    SelectObject(dc, p2);
    MoveToEx(dc, 8, 6, NULL); LineTo(dc, 12, 6);
    MoveToEx(dc, 8, 10, NULL); LineTo(dc, 12, 10);
    DeleteObject(p2); DeleteObject(p1);
}

static void tb_glyph_zoomin(HDC dc) {
    /* 放大镜：白圆深边 + 十字 + 灰手柄 */
    HPEN p1 = CreatePen(PS_SOLID, 1, COLR(0x222222));
    HBRUSH b1 = CreateSolidBrush(RGB(255, 255, 255));
    SelectObject(dc, p1); SelectObject(dc, b1);
    Ellipse(dc, 3, 3, 11, 11);
    MoveToEx(dc, 4, 7, NULL); LineTo(dc, 10, 7);
    MoveToEx(dc, 7, 4, NULL); LineTo(dc, 7, 10);
    DeleteObject(b1);
    HPEN p2 = CreatePen(PS_SOLID, 1, COLR(0x555555));
    SelectObject(dc, p2);
    MoveToEx(dc, 10, 10, NULL); LineTo(dc, 14, 14);
    DeleteObject(p2); DeleteObject(p1);
}

static void tb_glyph_zoomout(HDC dc) {
    /* 放大镜：白圆深边 + 横线（减号） + 灰手柄 */
    HPEN p1 = CreatePen(PS_SOLID, 1, COLR(0x222222));
    HBRUSH b1 = CreateSolidBrush(RGB(255, 255, 255));
    SelectObject(dc, p1); SelectObject(dc, b1);
    Ellipse(dc, 3, 3, 11, 11);
    MoveToEx(dc, 4, 7, NULL); LineTo(dc, 10, 7);
    DeleteObject(b1);
    HPEN p2 = CreatePen(PS_SOLID, 1, COLR(0x555555));
    SelectObject(dc, p2);
    MoveToEx(dc, 10, 10, NULL); LineTo(dc, 14, 14);
    DeleteObject(p2); DeleteObject(p1);
}

static void tb_glyph_reset(HDC dc) {
    /* 蓝白同心圆 */
    HPEN p1 = CreatePen(PS_SOLID, 1, COLR(0x0B57D0));
    HBRUSH b1 = CreateSolidBrush(RGB(255, 255, 255));
    SelectObject(dc, p1); SelectObject(dc, b1);
    Ellipse(dc, 3, 3, 13, 13);
    DeleteObject(b1);
    HBRUSH b2 = CreateSolidBrush(COLR(0x0B57D0));
    SelectObject(dc, b2);
    Ellipse(dc, 6, 6, 10, 10);
    DeleteObject(b2); DeleteObject(p1);
}

static void tb_glyph_about(HDC dc) {
    /* 蓝白圆 + 蓝 "i"（竖线 + 底部圆点） */
    HPEN p1 = CreatePen(PS_SOLID, 1, COLR(0x0B57D0));
    HBRUSH b1 = CreateSolidBrush(RGB(255, 255, 255));
    SelectObject(dc, p1); SelectObject(dc, b1);
    Ellipse(dc, 3, 3, 13, 13);
    DeleteObject(b1);
    HBRUSH b2 = CreateSolidBrush(COLR(0x0B57D0));
    SelectObject(dc, b2);
    MoveToEx(dc, 8, 5, NULL); LineTo(dc, 8, 9);
    Ellipse(dc, 7, 10, 9, 12);
    DeleteObject(b2); DeleteObject(p1);
}

static HBITMAP tb_make_glyph(void (*draw)(HDC)) {
    int S = TB_ICON_SIZE;
    HDC scr = GetDC(NULL);
    HDC dc = CreateCompatibleDC(scr);
    ReleaseDC(NULL, scr);
    BITMAPINFOHEADER bi;
    memset(&bi, 0, sizeof(bi));
    bi.biSize = sizeof(bi);
    bi.biWidth = S;
    bi.biHeight = -S;       /* 自顶向下 */
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;
    RGBQUAD *bits = NULL;
    HBITMAP bmp = CreateDIBSection(dc, (BITMAPINFO *)&bi, DIB_RGB_COLORS,
                                   (void **)&bits, NULL, 0);
    HGDIOBJ old = SelectObject(dc, bmp);
    RECT rc = {0, 0, S, S};
    HBRUSH mag = CreateSolidBrush(TB_ICON_BG);
    FillRect(dc, &rc, mag);
    DeleteObject(mag);
    /* 由每个 glyph 自行管理 pen/brush（支持填充与多色） */
    draw(dc);
    SelectObject(dc, old);
    DeleteDC(dc);
    return bmp;
}

static HIMAGELIST make_toolbar_imagelist(void) {
    int S = TB_ICON_SIZE;
    HIMAGELIST hil = ImageList_Create(S, S, ILC_COLOR32 | ILC_MASK, 8, 0);
    if (!hil) return NULL;
    void (*glyphs[])(HDC) = {
        tb_glyph_openfile, tb_glyph_openfolder, tb_glyph_list,
        tb_glyph_zoomin, tb_glyph_zoomout, tb_glyph_reset, tb_glyph_about,
    };
    for (int i = 0; i < 7; i++) {
        HBITMAP bmp = tb_make_glyph(glyphs[i]);
        if (bmp) {
            ImageList_AddMasked(hil, bmp, TB_ICON_BG);
            DeleteObject(bmp);
        }
    }
    return hil;
}

static void create_controls(void) {
    /* 工具栏（文本按钮） */
    g.toolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, NULL,
        WS_CHILD | WS_VISIBLE | TBSTYLE_LIST | TBSTYLE_TOOLTIPS,
        0, 0, 0, 0, g.main, NULL, g.hinst, NULL);
    SendMessageW(g.toolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);

    g.il = make_toolbar_imagelist();
    if (g.il) SendMessageW(g.toolbar, TB_SETIMAGELIST, 0, (LPARAM)g.il);

    static const char *labels[7] = {"打开文件", "打开文件夹", "文件列表",
                                    "放大", "缩小", "重置", "关于"};
    wchar_t pool[512];
    int pi = 0;
    for (int i = 0; i < 7; i++) {
        int n = MultiByteToWideChar(CP_UTF8, 0, labels[i], -1, pool + pi,
                                    (int)(sizeof(pool) / sizeof(wchar_t) - pi));
        if (n <= 0) break;
        pi += n; /* 含结尾 NUL */
    }
    pool[pi] = L'\0'; /* 双 NUL 结尾 */
    int first = (int)SendMessageW(g.toolbar, TB_ADDSTRINGW, 0, (LPARAM)pool);

    TBBUTTON btns[10];
    ZeroMemory(btns, sizeof(btns));
    int n = 0;
    #define TBTXT(id, str, img) do { \
        btns[n].iBitmap = (img); \
        btns[n].idCommand = (id); \
        btns[n].fsState = TBSTATE_ENABLED; \
        btns[n].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE; \
        btns[n].iString = first + (str); \
        n++; } while (0)
    #define TBSEP() do { btns[n].fsStyle = BTNS_SEP; n++; } while (0)
    TBTXT(IDM_OPENFILE, 0, 0);
    TBTXT(IDM_OPENFOLDER, 1, 1);
    TBSEP();
    TBTXT(IDM_TOGGLELIST, 2, 2);
    TBSEP();
    TBTXT(IDM_ZOOMIN, 3, 3);
    TBTXT(IDM_ZOOMOUT, 4, 4);
    TBTXT(IDM_ZOOMRESET, 5, 5);
    TBSEP();
    TBTXT(IDM_ABOUT, 6, 6);
    #undef TBTXT
    #undef TBSEP
    SendMessageW(g.toolbar, TB_ADDBUTTONSW, (WPARAM)n, (LPARAM)btns);
    SendMessageW(g.toolbar, TB_AUTOSIZE, 0, 0);

    /* 状态栏 */
    g.status = CreateWindowExW(0, STATUSCLASSNAMEW, NULL, WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, g.main, NULL, g.hinst, NULL);
    int parts[1] = {-1};
    SendMessageW(g.status, SB_SETPARTS, 1, (LPARAM)parts);
    set_status("就绪");

    /* 文件列表 */
    g.list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, NULL,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
        0, 0, 0, 0, g.main, NULL, g.hinst, NULL);
    SendMessageW(g.list, LVM_SETEXTENDEDLISTVIEWSTYLE,
                 LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER,
                 LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    LVCOLUMNW col;
    ZeroMemory(&col, sizeof(col));
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    col.fmt = LVCFMT_LEFT;
    col.cx = 280;
    col.pszText = L"名称";
    SendMessageW(g.list, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);
    if (!g.showList) {
        EnableWindow(g.list, FALSE);
        ShowWindow(g.list, SW_HIDE);
    }

    /* 预览 */
    g.preview = CreateWindowExW(0, MDQV_CLASS_PREVIEW, NULL,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL, 0, 0, 0, 0, g.main, NULL, g.hinst, NULL);
}

static void create_menu(void) {
    HMENU m = CreateMenu();
    HMENU fm = CreatePopupMenu();
    AppendMenuW(fm, MF_STRING, IDM_OPENFILE, L"打开文件...\tCtrl+O");
    AppendMenuW(fm, MF_STRING, IDM_OPENFOLDER, L"打开文件夹...\tCtrl+F");
    AppendMenuW(fm, MF_STRING, IDM_RELOAD, L"重新加载\tF5");
    AppendMenuW(fm, MF_SEPARATOR, 0, NULL);
    AppendMenuW(fm, MF_STRING, IDM_EXIT, L"退出\tAlt+F4");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)fm, L"文件");

    HMENU em = CreatePopupMenu();
    AppendMenuW(em, MF_STRING, IDM_COPY, L"复制选中文本\tCtrl+C");
    AppendMenuW(em, MF_STRING, IDM_SELECTALL, L"全选\tCtrl+A");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)em, L"编辑");

    HMENU vm = CreatePopupMenu();
    AppendMenuW(vm, MF_STRING, IDM_TOGGLELIST, L"显示/隐藏文件列表\tCtrl+L");
    AppendMenuW(vm, MF_SEPARATOR, 0, NULL);
    AppendMenuW(vm, MF_STRING, IDM_ZOOMIN, L"放大\tCtrl++");
    AppendMenuW(vm, MF_STRING, IDM_ZOOMOUT, L"缩小\tCtrl+-");
    AppendMenuW(vm, MF_STRING, IDM_ZOOMRESET, L"重置缩放\tCtrl+0");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)vm, L"视图");

    HMENU hm = CreatePopupMenu();
    AppendMenuW(hm, MF_STRING, IDM_ABOUT, L"关于 MDQuickViewer");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)hm, L"帮助");

    SetMenu(g.main, m);
    g.menu = m;
}

/* ----------------------------------------------------------- 预览窗口过程 */
static LRESULT CALLBACK wnd_proc_preview(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;
            if (g.doc) {
                SelPos a, b;
                ordered_sel(&a, &b);
                rd_paint_buffered(g.doc, hdc, g.scroll, w, h, a, b, g.selOn);
            } else {
                HBRUSH br = CreateSolidBrush(0x00FFFFFF);
                RECT full = {0, 0, w, h};
                FillRect(hdc, &full, br);
                DeleteObject(br);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(0x99, 0x99, 0x99));
                wchar_t *wh = u8s("从菜单或工具栏打开一个 Markdown 文件开始阅读");
                if (wh) {
                    TextOutW(hdc, RD_LM, RD_TM, wh, (int)wcslen(wh));
                    u16free(wh);
                }
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_VSCROLL:
            handle_vscroll(wp);
            return 0;
        case WM_MOUSEWHEEL: {
            if (!g.doc) return 0;
            int delta = (short)HIWORD(wp);
            g.scroll -= delta / 120 * 40;
            clamp_scroll();
            update_scroll_range();
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            if (!g.doc) return 0;
            int x = (short)LOWORD(lp);
            int y = (short)HIWORD(lp);
            g.anchor = rd_hit_pos(g.doc, x, y + g.scroll);
            g.head = g.anchor;
            g.selOn = 0;
            g.selDrag = 1;
            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!g.doc) return 0;
            int x = (short)LOWORD(lp);
            int y = (short)HIWORD(lp);
            g.mouseX = x;
            g.mouseY = y;
            if (g.selDrag) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                int hh = rc.bottom - rc.top;
                if (y < 0) g.scroll -= 24;
                else if (y > hh) g.scroll += 24;
                g.head = rd_hit_pos(g.doc, x, y + g.scroll);
                g.selOn = !rd_sel_equal(g.anchor, g.head);
                clamp_scroll();
                InvalidateRect(hwnd, NULL, FALSE);
            }
            update_preview_cursor();
            return 0;
        }
        case WM_LBUTTONUP: {
            if (!g.doc || !g.selDrag) return 0;
            int x = (short)LOWORD(lp);
            int y = (short)HIWORD(lp);
            g.selDrag = 0;
            ReleaseCapture();
            g.head = rd_hit_pos(g.doc, x, y + g.scroll);
            g.selOn = !rd_sel_equal(g.anchor, g.head);
            if (!g.selOn) {
                char buf[1024];
                if (rd_hit_link(g.doc, x, y + g.scroll, buf, (int)sizeof(buf)) > 0)
                    open_link(buf);
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_KEYDOWN:
            on_keydown(wp);
            return 0;
        case WM_SETCURSOR:
            update_preview_cursor();
            return 1;
        case WM_DROPFILES:
            on_drop_files((HDROP)wp);
            return 0;
        case WM_CONTEXTMENU: {
            /* 右键菜单：复制选中文本 / 全选（无选区时复制项灰掉） */
            POINT pt;
            if (lp == (LPARAM)-1) {
                GetCursorPos(&pt);
            } else {
                pt.x = (int)(short)LOWORD(lp);
                pt.y = (int)(short)HIWORD(lp);
            }
            HMENU pop = CreatePopupMenu();
            if (!pop) return 0;
            AppendMenuW(pop, MF_STRING | (g.selOn ? 0 : MF_GRAYED),
                        IDM_COPY, L"复制选中文本\tCtrl+C");
            AppendMenuW(pop, MF_STRING | (g.doc ? 0 : MF_GRAYED),
                        IDM_SELECTALL, L"全选\tCtrl+A");
            SetForegroundWindow(hwnd);
            int id = TrackPopupMenu(pop, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD,
                                    pt.x, pt.y, 0, hwnd, NULL);
            PostMessageW(hwnd, WM_NULL, 0, 0);
            DestroyMenu(pop);
            if (id) on_command(id);
            return 0;
        }
        case WM_DESTROY:
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ----------------------------------------------------------- 主窗口过程 */
static LRESULT CALLBACK wnd_proc_main(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            g.main = hwnd;
            create_controls();
            create_menu();
            DragAcceptFiles(hwnd, TRUE);
            DragAcceptFiles(g.preview, TRUE);
            return 0;
        case WM_DROPFILES:
            on_drop_files((HDROP)wp);
            return 0;
        case WM_SIZE:
            layout_children();
            return 0;
        case WM_GETMINMAXINFO:
            on_get_minmax_info(lp);
            return 0;
        case WM_COMMAND:
            on_command((int)LOWORD(wp));
            return 0;
        case WM_NOTIFY: {
            NMHDR *nm = (NMHDR *)lp;
            on_list_notify((UINT)nm->code);
            return 0;
        }
        case WM_KEYDOWN:
            on_keydown(wp);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ----------------------------------------------------------- 命令行 */
static void open_from_args(void) {
    wchar_t *cmd = GetCommandLineW();
    int argc;
    wchar_t **argv = CommandLineToArgvW(cmd, &argc);
    int handled = 0;
    if (argv) {
        for (int i = 1; i < argc; i++) {
            if (!argv[i] || argv[i][0] == L'\0') continue;
            char *p = abs_path_w(argv[i]);
            if (!p) continue;
            DWORD attr = GetFileAttributesW(argv[i]);
            if (attr == INVALID_FILE_ATTRIBUTES) {
                MessageBoxW(g.main, L"找不到文件或文件夹", L"MDQuickViewer", MB_ICONERROR);
                free(p);
                continue;
            }
            if (attr & FILE_ATTRIBUTE_DIRECTORY) {
                settings_save_last_folder(p);
                populate_list(p);
            } else if (is_markdown(p)) {
                char dir[4096];
                dirname_of(p, dir, sizeof(dir));
                settings_save_last_folder(dir);
                populate_list(dir);
                ui_load_file(p);
            }
            free(p);
            handled = 1;
            break; /* 只取第一个有效参数 */
        }
        LocalFree(argv);
    }
    if (handled) return;

    /* 没有参数：回退到 exe 旁 sample.md。
     * 全程用宽字符：exe 可能装在中文路径下（"D:\下载\MDQuickViewer.exe"），
     * ANSI 版 API 在那里同样会失败。 */
    wchar_t wexe[MAX_PATH];
    if (GetModuleFileNameW(NULL, wexe, MAX_PATH)) {
        wchar_t *wsample = (wchar_t *)LocalAlloc(LMEM_FIXED, (MAX_PATH + 16) * sizeof(wchar_t));
        if (wsample) {
            wcscpy(wsample, wexe);
            wchar_t *wbs = wcsrchr(wsample, L'\\');
            if (wbs) wcscpy(wbs + 1, L"sample.md");
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW(wsample, &fd);
            if (h != INVALID_HANDLE_VALUE) {
                FindClose(h);
                char *sample = w16to8(wsample);
                if (sample) {
                    /* dirname_of 按 UTF-8 处理，这里传回的正是 UTF-8 */
                    char dir[4096];
                    dirname_of(sample, dir, sizeof(dir));
                    populate_list(dir);
                    ui_load_file(sample);
                    free(sample);
                }
            }
            LocalFree(wsample);
        }
    }
}

/* ----------------------------------------------------------- 入口 */
int ui_main(void) {
    g.hinst = GetModuleHandleW(NULL);
    g.panelW = 280;
    g.showList = 0;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc_main;
    wc.hInstance = g.hinst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = MDQV_CLASS_MAIN;
    wc.hIcon = LoadIconW(g.hinst, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(g.hinst, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16,
                                   LR_DEFAULTCOLOR);
    if (!RegisterClassExW(&wc)) {
        MessageBoxW(NULL, L"注册主窗口类失败", L"MDQuickViewer", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    WNDCLASSEXW pwc;
    ZeroMemory(&pwc, sizeof(pwc));
    pwc.cbSize = sizeof(pwc);
    pwc.style = CS_HREDRAW | CS_VREDRAW;
    pwc.lpfnWndProc = wnd_proc_preview;
    pwc.hInstance = g.hinst;
    pwc.hCursor = LoadCursorW(NULL, IDC_IBEAM);
    pwc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    pwc.lpszClassName = MDQV_CLASS_PREVIEW;
    if (!RegisterClassExW(&pwc)) {
        MessageBoxW(NULL, L"注册预览窗口类失败", L"MDQuickViewer", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    HWND hwnd = CreateWindowExW(0, MDQV_CLASS_MAIN, L"MDQuickViewer — Markdown 浏览器",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
        1000, 700, NULL, NULL, g.hinst, NULL);
    if (!hwnd) {
        CoUninitialize();
        return 1;
    }

    open_from_args();
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    /* 清理 */
    if (g.doc) md_doc_free(g.doc);
    if (g.folder) free(g.folder);
    for (int i = 0; i < g.nfiles; i++) free(g.files[i]);
    free(g.files);
    rd_font_cleanup();
    buf_release();
    if (g.il) ImageList_Destroy(g.il);
    CoUninitialize();
    return 0;
}
