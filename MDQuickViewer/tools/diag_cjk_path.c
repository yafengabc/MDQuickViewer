/* 诊断：为什么 MSVC 侧 Doc.path 里没有 UTF-8 中文？
 * 直接把各阶段字节打出来对比。
 * 不链接 ui.c（它有 wWinMain，会和这里的 main 冲突），
 * 读文件部分照抄 ui_read_file 的实现。 */
#include "win32.h"
#include "model.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_like_ui(const char *u8path, long *out_len) {
    wchar_t *wpath = u8s(u8path);
    FILE *f = wpath ? _wfopen(wpath, L"rb") : NULL;
    if (!f) { u16free(wpath); return NULL; }
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

static void hexdump(const char *label, const char *s) {
    printf("%-28s len=%3d  ", label, (int)(s ? strlen(s) : -1));
    if (!s) { printf("(null)\n"); return; }
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p >= 0x20 && *p < 0x7F) putchar(*p);
        else printf("\\x%02X", *p);
    }
    putchar('\n');
}

int main(void) {
    wchar_t *base = (wchar_t *)LocalAlloc(LMEM_FIXED, MAX_PATH * sizeof(wchar_t));
    GetTempPathW(MAX_PATH, base);
    wchar_t *root = (wchar_t *)LocalAlloc(LMEM_FIXED, MAX_PATH * sizeof(wchar_t));
    swprintf(root, MAX_PATH, L"%sMDQV-DIAG", base);
    CreateDirectoryW(root, NULL);

    wchar_t *wn = (wchar_t *)LocalAlloc(LMEM_FIXED, MAX_PATH * sizeof(wchar_t));
    swprintf(wn, MAX_PATH, L"%s\\%s", root, L"\x4e2d\x6587\x6587\x6863.md");

    /* 写文件 */
    HANDLE h = CreateFileW(wn, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    const char *C = "# \xe4\xb8\xad\xe6\x96\x87\xe6\xa0\x87\xe9\xa2\x98\n\nbody\n";
    DWORD wr = 0; WriteFile(h, C, (DWORD)strlen(C), &wr, NULL); CloseHandle(h);

    /* UTF-16 -> UTF-8 */
    char u8[MAX_PATH * 4];
    WideCharToMultiByte(CP_UTF8, 0, wn, -1, u8, sizeof(u8), NULL, NULL);
    hexdump("wchar 路径(转UTF-8后)", u8);
    printf("  strstr(u8, 中文UTF-8) = %s\n", strstr(u8, "\xe4\xb8\xad\xe6\x96\x87") ? "找到" : "没找到");

    /* u8dup */
    char *dup = u8dup(u8);
    hexdump("u8dup 结果", dup);
    printf("  strstr(dup) = %s\n", dup && strstr(dup, "\xe4\xb8\xad\xe6\x96\x87") ? "找到" : "没找到");

    /* 读文件 -> md_parse */
    long len = 0;
    char *buf = read_like_ui(u8, &len);
    printf("read_like_ui = %s (len=%ld)\n", buf ? "OK" : "NULL", len);

    if (buf) {
        Doc *d = md_parse(buf, u8);
        if (d) {
            hexdump("Doc.path", d->path);
            printf("  strstr(Doc.path) = %s\n",
                   (d->path && strstr(d->path, "\xe4\xb8\xad\xe6\x96\x87")) ? "找到" : "没找到");
            md_doc_free(d);
        } else printf("md_parse 返回 NULL\n");
        free(buf);
    }
    free(dup);
    DeleteFileW(wn);
    RemoveDirectoryW(root);
    return 0;
}
