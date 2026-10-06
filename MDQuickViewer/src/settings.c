/* 注册表读写实现：HKCU\Software\MDQuickViewer\LastFolder（REG_SZ）。
 * 走 advapi32，与 Go 版的 regGetString/regSetString 等价。 */
#include "settings.h"
#include "win32.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

#ifndef KEY_WOW64_64KEY
#define KEY_WOW64_64KEY 0x0100
#endif

/* 子键：默认 Software\MDQuickViewer，可被 MDQV_REG_KEY 重定向（测试用）。 */
static const char *reg_subkey(void) {
    const char *e = getenv("MDQV_REG_KEY");
    if (e && e[0]) return e;
    return "Software\\MDQuickViewer";
}

/* 目录是否真的存在且是目录 */
static int dir_exists(const char *dir) {
    if (!dir || !dir[0]) return 0;
    wchar_t *wd = u8s(dir);
    if (!wd) return 0;
    DWORD attr = GetFileAttributesW(wd);
    u16free(wd);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

const char *settings_load_last_folder(void) {
    static char buf[4096];
    buf[0] = '\0';
    const char *sk = reg_subkey();
    wchar_t *wsk = u8s(sk);
    if (!wsk) return "";
    HKEY hk = NULL;
    LONG r = RegOpenKeyExW(HKEY_CURRENT_USER, wsk, 0, KEY_READ | KEY_WOW64_64KEY, &hk);
    u16free(wsk);
    if (r != ERROR_SUCCESS) return "";

    wchar_t wbuf[4096];
    DWORD sz = sizeof(wbuf);
    DWORD type = 0;
    r = RegQueryValueExW(hk, L"LastFolder", NULL, &type, (BYTE *)wbuf, &sz);
    RegCloseKey(hk);
    if (r != ERROR_SUCCESS || type != REG_SZ) return "";

    /* sz 是字节数，含结尾 NUL；去掉可能的尾随 NUL */
    DWORD n = sz / sizeof(wchar_t);
    while (n > 0 && (wbuf[n - 1] == L'\0' || wbuf[n - 1] == L' ')) n--;
    wbuf[n] = L'\0';
    if (n == 0) return "";

    if (WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, buf, (int)sizeof(buf), NULL, NULL) == 0)
        return "";

    /* 记录可能被手动改坏 / 目录已被删除，校验一下 */
    if (!dir_exists(buf)) return "";
    return buf;
}

void settings_save_last_folder(const char *dir) {
    if (!dir_exists(dir)) return;
    const char *sk = reg_subkey();
    wchar_t *wsk = u8s(sk);
    if (!wsk) return;
    HKEY hk = NULL;
    DWORD disp = 0;
    LONG r = RegCreateKeyExW(HKEY_CURRENT_USER, wsk, 0, NULL, REG_OPTION_NON_VOLATILE,
                             KEY_WRITE | KEY_WOW64_64KEY, NULL, &hk, &disp);
    u16free(wsk);
    if (r != ERROR_SUCCESS) return;
    wchar_t *wdir = u8s(dir);
    if (wdir) {
        r = RegSetValueExW(hk, L"LastFolder", 0, REG_SZ, (const BYTE *)wdir,
                           (DWORD)((wcslen(wdir) + 1) * sizeof(wchar_t)));
        u16free(wdir);
    }
    RegCloseKey(hk);
    (void)r;
}

void settings_delete_last_folder(void) {
    const char *sk = reg_subkey();
    wchar_t *wsk = u8s(sk);
    if (!wsk) return;
    HKEY hk = NULL;
    LONG r = RegOpenKeyExW(HKEY_CURRENT_USER, wsk, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &hk);
    if (r == ERROR_SUCCESS) {
        RegDeleteValueW(hk, L"LastFolder");
        RegCloseKey(hk);
    }
    u16free(wsk);
}

const char *settings_initial_folder(const char *cur) {
    static char buf[4096];
    /* 1) 内存中的当前目录 */
    if (dir_exists(cur)) {
        strncpy(buf, cur, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        return buf;
    }
    /* 2) 注册表记录的目录 */
    const char *last = settings_load_last_folder();
    if (last && last[0]) {
        strncpy(buf, last, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        return buf;
    }
    /* 3) exe 所在目录 */
    wchar_t wexe[MAX_PATH];
    if (GetModuleFileNameW(NULL, wexe, MAX_PATH)) {
        wchar_t *bs = wcsrchr(wexe, L'\\');
        if (bs) *bs = L'\0';
        if (WideCharToMultiByte(CP_UTF8, 0, wexe, -1, buf, (int)sizeof(buf), NULL, NULL))
            return buf;
    }
    buf[0] = '.';
    buf[1] = '\0';
    return buf;
}
