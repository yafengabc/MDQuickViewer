/* 中文（含非 ASCII）路径端到端回归测试。
 *
 * 背景：本项目内部路径统一存 UTF-8，而 ANSI 版 Win32 API
 * （fopen / FindFirstFileA / GetModuleFileNameA）会把 UTF-8 字节按当前
 * 代码页解释，中文路径必然失败。这个测试在真实的中文目录里造 .md 文件，
 * 走真实的读文件代码路径，断言能读到内容。
 *
 * 覆盖三种典型场景：
 *   1. 中文目录 + ASCII 文件名
 *   2. ASCII 目录 + 中文文件名
 *   3. 中文目录 + 中文文件名 + 非 ASCII 内容
 * 另外顺带验证「整条链路」：Markdown 解析后标题/正文能取出中文。
 */
#include "ui.h"
#include "win32.h"
#include "model.h"
#include "cjk_path_test.h"
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_fail;
static FILE *g_out;

static void tout(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    if (g_out) {
        va_start(ap, fmt);
        vfprintf(g_out, fmt, ap);
        va_end(ap);
        fflush(g_out);
    }
}

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; tout("  FAIL: %s\n", msg); } \
} while (0)

/* 建目录（宽字符），返回 0 成功 */
static int mk_dir_w(const wchar_t *w) {
    if (CreateDirectoryW(w, NULL)) return 0;
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    return -1;
}

/* 写文件（宽字符路径 + UTF-8 内容） */
static int write_file_w(const wchar_t *wpath, const char *utf8) {
    HANDLE h = CreateFileW(wpath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    DWORD wr = 0;
    BOOL ok = WriteFile(h, utf8, (DWORD)strlen(utf8), &wr, NULL);
    CloseHandle(h);
    return ok ? 0 : -1;
}

/* 从 UTF-8 字节直接构造 UTF-16 路径（模拟本项目内部约定） */
static wchar_t *w_from_u8(const char *u8) {
    int n = MultiByteToWideChar(CP_UTF8, 0, u8, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, u8, -1, w, n);
    return w;
}

/* 读文件（UTF-8 路径）—— 直接调 ui.c 里真实的那份，不在测试里重写，
 * 否则测到的可能是测试自己的实现而不是产品代码。 */
static char *read_file_u8(const char *u8path, long *out_len) {
    return ui_read_file(u8path, out_len);
}

static void rm_tree_w(const wchar_t *wdir) {
    wchar_t pat[MAX_PATH];
    swprintf(pat, MAX_PATH, L"%s\\*", wdir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            wchar_t sub[MAX_PATH];
            swprintf(sub, MAX_PATH, L"%s\\%s", wdir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                rm_tree_w(sub);
            } else {
                DeleteFileW(sub);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(wdir);
}

int cjk_path_tests_run(void) {
    g_pass = g_fail = 0;
    /* GUI 子系统下 stdout 接不到调用方管道，追加写同一份日志 */
    wchar_t logpath[MAX_PATH];
    if (GetTempPathW(MAX_PATH, logpath)) {
        wcscat_s(logpath, MAX_PATH, L"MDQuickViewer-test.log");
        g_out = _wfopen(logpath, L"a");
    }
    tout("=== 中文路径测试 ===\n");

    wchar_t *base = (wchar_t *)LocalAlloc(LMEM_FIXED, MAX_PATH * sizeof(wchar_t));
    GetTempPathW(MAX_PATH, base);
    wchar_t *root = (wchar_t *)LocalAlloc(LMEM_FIXED, MAX_PATH * sizeof(wchar_t));
    /* 根目录名含中文，模拟用户把文档放在 "D:\下载\文档\" 下 */
    swprintf(root, MAX_PATH, L"%sMDQV-CJK-测试根目录", base);

    /* 三个子目录，分别覆盖三种组合 */
    wchar_t *d1 = (wchar_t *)LocalAlloc(LMEM_FIXED, MAX_PATH * sizeof(wchar_t));
    wchar_t *d2 = (wchar_t *)LocalAlloc(LMEM_FIXED, MAX_PATH * sizeof(wchar_t));
    wchar_t *d3 = (wchar_t *)LocalAlloc(LMEM_FIXED, MAX_PATH * sizeof(wchar_t));
    swprintf(d1, MAX_PATH, L"%s\\中文子目录", root);   /* 中文目录 + ASCII 名 */
    swprintf(d2, MAX_PATH, L"%s\\ascii-subdir", root); /* ASCII 目录 + 中文名 */
    swprintf(d3, MAX_PATH, L"%s\\混合 目录 名", root);  /* 中文+空格 目录 + 中文名 */

    if (mk_dir_w(root) != 0 || mk_dir_w(d1) != 0 ||
        mk_dir_w(d2) != 0 || mk_dir_w(d3) != 0) {
        tout("  FAIL: 无法创建测试目录（error=%lu）\n", GetLastError());
        g_fail++;
        rm_tree_w(root);
        LocalFree(d1); LocalFree(d2); LocalFree(d3);
        LocalFree(root); LocalFree(base);
        return 1;
    }
    CHECK(1, "创建含中文的测试目录");

    /* 内容也含中文，验证不只是路径、连内容链路也要正确 */
    static const char CONTENT[] =
        "# 中文标题\n"
        "\n"
        "这是一段中文正文，用于验证 UTF-8 内容读取。\n"
        "\n"
        "- 列表项一\n"
        "- 列表项二\n";

    struct { const wchar_t *dir; const char *name; const char *label; } cases[] = {
        { d1, "plain.md",            "中文目录 + ASCII 文件名" },
        { d2, "\xe4\xb8\xad\xe6\x96\x87\xe6\x96\x87\xe6\xa1\xa3.md", "ASCII 目录 + 中文文件名" },
        { d3, "\xe4\xb8\xad\xe6\x96\x87\xe6\x96\x87\xe6\xa1\xa3\xe5\x86\x99.md", "中文目录 + 中文文件名" },
    };

    for (int i = 0; i < 3; i++) {
        /* 构造 UTF-16 路径：目录 + 文件名 */
        wchar_t *wname = w_from_u8(cases[i].name);
        wchar_t *wpath = (wchar_t *)LocalAlloc(LMEM_FIXED, MAX_PATH * sizeof(wchar_t));
        swprintf(wpath, MAX_PATH, L"%s\\%s", cases[i].dir, wname ? wname : L"x.md");

        int wrote = (write_file_w(wpath, CONTENT) == 0);
        CHECK(wrote, cases[i].label);
        if (!wrote) { LocalFree(wpath); free(wname); continue; }

        /* 转成 UTF-8 路径（项目内部存储形式），再走读文件路径 */
        char u8path[MAX_PATH * 4];
        WideCharToMultiByte(CP_UTF8, 0, wpath, -1, u8path, sizeof(u8path), NULL, NULL);

        /* 模拟命令行/拖放入口：ui.c 的 abs_path_w 走 GetFullPathNameW。
         * 验证「宽字符绝对路径 → UTF-8 → 读文件」这条链路，
         * 也就是用户把文件拖到 exe 图标上的真实路径。 */
        wchar_t wfull[MAX_PATH];
        DWORD gn = GetFullPathNameW(wpath, MAX_PATH, wfull, NULL);
        CHECK(gn != 0 && gn < MAX_PATH, "GetFullPathNameW 能处理中文路径");
        if (gn != 0 && gn < MAX_PATH) {
            char u8full[MAX_PATH * 4];
            WideCharToMultiByte(CP_UTF8, 0, wfull, -1, u8full, sizeof(u8full), NULL, NULL);
            long l2 = 0;
            char *b2 = read_file_u8(u8full, &l2);
            CHECK(b2 != NULL, "命令行/拖放入口能读出中文路径文件");
            if (b2) free(b2);
        }

        long len = 0;
        char *buf = read_file_u8(u8path, &len);
        if (!buf) {
            tout("  FAIL: 无法读取 %s（读文件返回 NULL）\n", cases[i].label);
            g_fail++;
        } else {
            CHECK(len == (long)strlen(CONTENT), cases[i].label);
            CHECK(buf && strstr(buf, "\xe4\xb8\xad\xe6\x96\x87\xe6\xa0\x87\xe9\xa2\x98") != NULL,
                  "中文标题字节完整（未被代码页破坏）");
            CHECK(buf && strstr(buf, "\xe8\xbf\x99\xe6\x98\xaf\xe4\xb8\x80\xe6\xae\xb5\xe4\xb8\xad\xe6\x96\x87") != NULL,
                  "中文正文字节完整");
            free(buf);
        }

        /* 再走一遍 Markdown 解析，确认中文标题能被解析出来。
         * 标题不是 Doc 的独立字段，而是 kind="h1" 的 block，其 span 存文本。 */
        buf = read_file_u8(u8path, &len);
        if (buf) {
            Doc *d = md_parse(buf, u8path);
            CHECK(d != NULL, "中文内容能解析出 Doc");
            if (d) {
                /* 找到 h1 并检查其文本含中文 */
                int found = 0;
                for (int bi = 0; bi < d->nblock; bi++) {
                    const Block *b = &d->blocks[bi];
                    if (!b->kind || strcmp(b->kind, "h1") != 0) continue;
                    for (int si = 0; si < b->nspan; si++) {
                        const Span *sp = &b->spans[si];
                        if (sp->text && sp->len > 0) {
                            char tmp[256];
                            int n = sp->len < (int)sizeof(tmp) - 1 ? sp->len : (int)sizeof(tmp) - 1;
                            memcpy(tmp, sp->text, (size_t)n);
                            tmp[n] = '\0';
                            if (strstr(tmp, "\xe4\xb8\xad\xe6\x96\x87")) { found = 1; break; }
                        }
                    }
                    if (found) break;
                }
                CHECK(found, "h1 标题含中文（UTF-8 未损坏）");
                /* 路径本身也要原样保存在 Doc 里（状态栏/最近目录会用） */
                CHECK(d->path && strstr(d->path, "\xe4\xb8\xad\xe6\x96\x87") != NULL,
                      "Doc.path 保留中文路径（未被 ANSI 转换破坏）");
                md_doc_free(d);
            }
            free(buf);
        }

        DeleteFileW(wpath);
        LocalFree(wpath);
        free(wname);
    }

    rm_tree_w(root);
    LocalFree(d1); LocalFree(d2); LocalFree(d3);
    LocalFree(root); LocalFree(base);

    tout("=== 结果: %d 通过, %d 失败 ===\n", g_pass, g_fail);
    if (g_out) { fclose(g_out); g_out = NULL; }
    return g_fail ? 1 : 0;
}
