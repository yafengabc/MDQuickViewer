/* 打开文件 / 文件夹对话框：用系统通用对话框（GetOpenFileNameW /
 * SHBrowseForFolderW），比自绘对话框更可靠、外观与系统一致。 */
#include "dlg.h"
#include "win32.h"
#include <windows.h>
#include <shlobj.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* UTF-16 路径 -> malloc 的 UTF-8 串 */
static char *wchar_to_utf8(const wchar_t *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return NULL;
    char *out = (char *)malloc((size_t)n);
    if (!out) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, n, NULL, NULL);
    return out;
}

char *dlg_open_file(const char *init_dir) {
    wchar_t fname[MAX_PATH];
    fname[0] = L'\0';
    /* 过滤器：双 NUL 结尾，每对之间是 label\0pattern\0 */
    wchar_t filter[] = L"Markdown 文件\0*.md;*.markdown\0所有文件\0*.*\0";
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = fname;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    wchar_t *winit = init_dir ? u8s(init_dir) : NULL;
    ofn.lpstrInitialDir = winit;
    BOOL ok = GetOpenFileNameW(&ofn);
    if (winit) u16free(winit);
    if (!ok) return NULL;
    return wchar_to_utf8(fname);
}

/* SHBrowseForFolder 的回调：初始化后定位到初始目录 */
static int CALLBACK browse_cb(HWND hwnd, UINT msg, LPARAM lp, LPARAM data) {
    (void)lp;
    if (msg == BFFM_INITIALIZED && data)
        SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, data);
    return 0;
}

char *dlg_open_folder(const char *init_dir) {
    wchar_t display[MAX_PATH];
    BROWSEINFOW bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.lpszTitle = L"选择要浏览的 Markdown 文件夹";
    bi.pszDisplayName = display;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    wchar_t *winit = init_dir ? u8s(init_dir) : NULL;
    bi.lParam = (LPARAM)winit;
    bi.lpfn = browse_cb;
    PIDLIST_ABSOLUTE pid = SHBrowseForFolderW(&bi);
    if (winit) u16free(winit);
    if (!pid) return NULL;
    wchar_t path[MAX_PATH];
    if (!SHGetPathFromIDListW(pid, path)) {
        CoTaskMemFree(pid);
        return NULL;
    }
    CoTaskMemFree(pid);
    return wchar_to_utf8(path);
}
