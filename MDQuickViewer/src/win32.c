/* UTF-8 ↔ UTF-16 转换与 UTF-8 字节切片工具。
 *
 * Go 版直接用 string（内部 UTF-8）和 windows.UTF16FromString，切片是按字节的
 * （goldmark 的 Segment.Start/Stop 就是字节偏移）。C 版没有 slice，必须显式
 * 复制，所以这里提供 u8slice() 模拟"按字节取子串"。
 */
#include "win32.h"
#include <stdlib.h>
#include <string.h>

wchar_t *u8s(const char *s) {
    if (!s) return NULL;
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = (wchar_t *)LocalAlloc(LMEM_FIXED, (size_t)n * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

void u16free(wchar_t *w) {
    if (w) LocalFree(w);
}

int u8len(const char *s) { return s ? (int)strlen(s) : 0; }

char *u8slice(const char *s, int start, int stop) {
    if (!s) return NULL;
    int len = u8len(s);
    if (start < 0) start = 0;
    if (stop > len) stop = len;
    if (stop < start) stop = start;
    int n = stop - start;
    char *out = (char *)malloc((size_t)n + 1);
    if (!out) return NULL;
    memcpy(out, s + start, (size_t)n);
    out[n] = '\0';
    return out;
}

/* 数 UTF-8 序列个数：遇到首字节 +1，跟随字节跳过。 */
int u8count(const char *s) {
    if (!s) return 0;
    int n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        n++;
        p++;
        while (*p && (*p & 0xC0) == 0x80) p++; /* 跟随字节 */
    }
    return n;
}

/* 取第 idx 个字符的 UTF-8 字节（含尾 NUL 的新缓冲）。 */
char *u8charat(const char *s, int idx) {
    if (!s) return NULL;
    const unsigned char *p = (const unsigned char *)s;
    int n = 0;
    while (*p) {
        const unsigned char *begin = p;
        p++;
        while (*p && (*p & 0xC0) == 0x80) p++;
        if (n == idx) {
            int len = (int)(p - begin);
            char *out = (char *)malloc((size_t)len + 1);
            if (!out) return NULL;
            memcpy(out, begin, (size_t)len);
            out[len] = '\0';
            return out;
        }
        n++;
    }
    return NULL;
}
