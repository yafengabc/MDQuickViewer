/* MDQuickViewer 共用声明：Win32 头、comctl32、少量工具宏。
 *
 * 编译方式（MSYS2 UCRT64）：
 *   gcc -municode -mwindows ... -lcomctl32 -lgdi32 -luser32 -lshell32
 *
 * 统一用 W 版本的 API（宽字符），所有字符串都要过 utf8→utf16 转换，
 * 见 win32.c 里的 u8s()/u16free()。
 */
#ifndef GOMD_WIN32_H
#define GOMD_WIN32_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>

/* go 的 int32 → Windows 的 int */
typedef int i32;
typedef unsigned int u32;
typedef long long i64;
typedef unsigned long long u64;

/* COLORREF 是 0x00BBGGRR，而我们习惯写 0xRRGGBB（与 Go 版一致）。
 * COLR 负责把 0xRRGGBB 转成正确的 COLORREF（R/B 互换）。
 * 注意：早期这里误写成 ((COLORREF)(hex)) 空壳强转，
 * 等于把字节原样当成 COLORREF，所有颜色 R/B 通道对调（蓝变橙等）。 */
#define COLR(hex) RGB(((hex) >> 16) & 0xFF, ((hex) >> 8) & 0xFF, (hex) & 0xFF)
#define ARGB(a, r, g, b) \
    ((COLORREF)(((u32)(a) << 24) | ((u32)(r)) | ((u32)(g) << 8) | ((u32)(b) << 16)))

/* 宽字符串互转。u8s() 返回 LocalAlloc 出来的缓冲，调用方负责 u16free()。 */
wchar_t *u8s(const char *s);
void u16free(wchar_t *w);
/* UTF-8 字符串的字节长度（等价 Go 里的 len(src)，用于切段） */
int u8len(const char *s);
/* 取 UTF-8 字符串的 [start, stop) 字节子串，返回 LocalAlloc 缓冲 */
char *u8slice(const char *s, int start, int stop);
/* 字符数（Unicode 码点数），排版时按字符而非字节切分要用 */
int u8count(const char *s);
/* 取第 n 个 Unicode 字符（UTF-8 编码，可能多字节） */
char *u8charat(const char *s, int idx);

/* 字节安全的字符串复制（malloc）。**不要用 strdup/_strdup**：
 * MSVCRT 的 _strdup 走 ANSI 语义，会按当前代码页转换，中文 UTF-8 路径
 * 会被破坏（MinGW 的 strdup 恰好是字节安全的，所以问题只在 MSVC 侧暴露）。 */
char *u8dup(const char *s);

/* ASCII 范围内的大小写无关比较，返回 0 表示相等。**不要用 _stricmp**：
 * MSVCRT 会按本地代码页折叠字符，在中文代码页下比较含中文的路径不可靠。
 * 本项目只在比 ".md" 之类 ASCII 后缀、以及比较路径是否同一文件时用它。 */
int u8stricmp_ascii(const char *a, const char *b);

#endif /* GOMD_WIN32_H */
