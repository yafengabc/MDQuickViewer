/* 截图工具：启动 MDQuickViewer 并把窗口/客户区抓成 BMP。
 *
 * 沙箱里没有交互桌面，但可以：
 *   1. 启动 MDQuickViewer.exe <sample.md>
 *   2. 等它建好窗口
 *   3. PrintWindow 到内存 DIB（能抓到工具栏、状态栏、列表等子控件）
 *   4. 落 BMP，再由 tools_bmp2png.py 转 PNG
 *
 * 用法：
 *   scap.exe <exe> <file.md> <out.bmp> [宽度] [高度] [额外命令...]
 *
 * 额外命令用于抓不同界面状态，例如：
 *   scap.exe MDQuickViewer.exe sample.md out.bmp 1000 700 IDM_TOGGLELIST
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#ifndef GOMD_CLASS_MAIN_FALLBACK
#define GOMD_CLASS_MAIN_FALLBACK L"MDQuickViewerMainWindow"
#endif

/* 与 ui.c 里的命令 ID 保持一致（用于抓特定界面状态） */
#define CMD_TOGGLELIST 1010
#define CMD_ZOOMIN     1020
#define CMD_ZOOMOUT    1021
#define CMD_ZOOMRESET  1022

static int parse_cmd(const char *s) {
    if (!s) return 0;
    if (!strcmp(s, "list"))   return CMD_TOGGLELIST;
    if (!strcmp(s, "zoomin")) return CMD_ZOOMIN;
    return 0;
}

static int save_bmp(const wchar_t *path, HBITMAP bmp, int w, int h) {
    BITMAPINFOHEADER bi;
    memset(&bi, 0, sizeof(bi));
    bi.biSize = sizeof(bi);
    bi.biWidth = w;
    bi.biHeight = -h;          /* 自顶向下 */
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    int stride = w * 4;
    unsigned char *buf = (unsigned char *)malloc((size_t)stride * h);
    if (!buf) return 0;
    if (!GetDIBits(GetDC(NULL), bmp, 0, h, buf, (BITMAPINFO *)&bi, DIB_RGB_COLORS)) {
        free(buf);
        return 0;
    }

    /* BMP 文件头（14 字节）+ 像素 */
    DWORD dataSize = (DWORD)((size_t)stride * h);
    DWORD fileSize = 14 + 40 + dataSize;
    unsigned char *file = (unsigned char *)malloc(fileSize);
    if (!file) { free(buf); return 0; }
    memset(file, 0, 14 + 40);
    file[0] = 'B'; file[1] = 'M';
    memcpy(file + 2, &fileSize, 4);
    DWORD off = 14 + 40;
    memcpy(file + 10, &off, 4);
    memcpy(file + 14, &bi, 40);
    /* 32bpp BI_RGB 的 BITMAPINFOHEADER 大小写 40，方向已为自顶向下 */
    memcpy(file + 14 + 40, buf, dataSize);
    free(buf);

    FILE *f = NULL;
    _wfopen_s(&f, path, L"wb");
    if (!f) { free(file); return 0; }
    fwrite(file, 1, fileSize, f);
    fclose(f);
    free(file);
    return 1;
}

int wmain(int argc, wchar_t **argv) {
    if (argc < 4) {
        fwprintf(stderr, L"用法: scap.exe <exe> <file.md> <out.bmp> [宽] [高] [命令]\n");
        return 2;
    }
    const wchar_t *exe = argv[1];
    const wchar_t *doc = argv[2];
    const wchar_t *out = argv[3];   /* 宽字符路径，配 _wfopen_s */
    int W = (argc > 4) ? _wtoi(argv[4]) : 1000;
    int H = (argc > 5) ? _wtoi(argv[5]) : 700;
    int cmd = (argc > 6 && argv[6][0] != 0) ? parse_cmd("list") : 0;

    /* 启动应用 */
    wchar_t full[MAX_PATH];
    if (GetFullPathNameW(exe, MAX_PATH, full, NULL) == 0) wcscpy(full, exe);
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    wchar_t cmdline[MAX_PATH * 2];
    swprintf(cmdline, MAX_PATH * 2, L"\"%s\" \"%s\"", full, doc);
    if (!CreateProcessW(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        fwprintf(stderr, L"CreateProcess 失败: %lu\n", GetLastError());
        return 1;
    }

    /* 等主窗口出现（校验归属进程，避免抓到残留实例） */
    HWND hwnd = NULL;
    DWORD targetPid = pi.dwProcessId;
    for (int i = 0; i < 100; i++) {
        Sleep(100);
        HWND h = FindWindowW(GOMD_CLASS_MAIN_FALLBACK, NULL);
        if (h) {
            DWORD pid = 0;
            /* lParam 必须非空，否则只返回线程 id 不写 pid */
            GetWindowThreadProcessId(h, &pid);
            if (pid == targetPid) { hwnd = h; break; }
        }
    }
    if (!hwnd) {
        fwprintf(stderr, L"未找到主窗口（类 %ls，pid %lu）\n", GOMD_CLASS_MAIN_FALLBACK, targetPid);
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 1;
    }

    /* 摆到指定大小并前置，等 WM_PAINT 完成 */
    RECT wr = { 0, 0, W, H };
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    SetWindowPos(hwnd, HWND_TOP, 60, 60, wr.right - wr.left, wr.bottom - wr.top, SWP_SHOWWINDOW);
    Sleep(500);
    if (cmd) {
        PostMessageW(hwnd, WM_COMMAND, (WPARAM)cmd, 0);
        Sleep(400);
    }
    RedrawWindow(hwnd, NULL, NULL, RDW_UPDATENOW | RDW_ALLCHILDREN);

    /* 抓窗口（含边框/标题栏，能看到工具栏图标） */
    RECT rc;
    GetWindowRect(hwnd, &rc);
    int ww = rc.right - rc.left;
    int hh = rc.bottom - rc.top;

    HDC scr = GetDC(NULL);
    HDC mem = CreateCompatibleDC(scr);
    ReleaseDC(NULL, scr);
    BITMAPINFOHEADER bi;
    memset(&bi, 0, sizeof(bi));
    bi.biSize = sizeof(bi);
    bi.biWidth = ww;
    bi.biHeight = -hh;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;
    void *bits = NULL;
    HBITMAP bmp = CreateDIBSection(mem, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ old = SelectObject(mem, bmp);

    /* PW_RENDERFULLCONTENT(2) 才能抓到 DWM 合成的内容 */
    BOOL ok = PrintWindow(hwnd, mem, 2);
    if (!ok) ok = PrintWindow(hwnd, mem, 0);
    if (!ok) {
        /* 兜底：BitBlt 桌面区域 */
        BitBlt(mem, 0, 0, ww, hh, GetDC(NULL), rc.left, rc.top, SRCCOPY);
    }
    GdiFlush();

    int done = save_bmp(out, bmp, ww, hh);
    wprintf(L"%ls %dx%d printwindow=%d\n", out, ww, hh, ok);

    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);

    /* 抓完关掉应用 */
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
    Sleep(300);
    if (WaitForSingleObject(pi.hProcess, 1000) == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 0);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return done ? 0 : 1;
}
