/* MDQuickViewer 入口。
 *   - 自检模式（--test / --test-md / --test-hl / --test-render）：跑测试后退出。
 *     GUI 子系统的 stdout 不保证接到调用方管道，所以测试内部会写
 *     %TEMP%\MDQuickViewer-test.log，构建脚本从那里读结果。
 *   - 普通模式：调用 ui_main() 进入完整窗口应用。 */
#include "ui.h"
#include "markdown_test.h"
#include "highlight_test.h"
#include "render_test.h"
#include "cjk_path_test.h"
#include <windows.h>

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdline, int show) {
    (void)inst;
    (void)prev;
    (void)show;

    /* 自检模式：跑测试后退出。 */
    if (cmdline && (wcscmp(cmdline, L"--test-md") == 0 || wcscmp(cmdline, L"--test-hl") == 0 ||
                    wcscmp(cmdline, L"--test-render") == 0 ||
                    wcscmp(cmdline, L"--test-cjk") == 0 ||
                    wcscmp(cmdline, L"--test") == 0)) {
        int rc = 0;
        if (wcscmp(cmdline, L"--test-md") == 0) {
            rc |= md_tests_run();
        } else if (wcscmp(cmdline, L"--test-hl") == 0) {
            rc |= hl_tests_run();
        } else if (wcscmp(cmdline, L"--test-render") == 0) {
            rc |= rd_tests_run();
        } else if (wcscmp(cmdline, L"--test-cjk") == 0) {
            rc |= cjk_path_tests_run();
        } else {
            rc |= md_tests_run();
            rc |= hl_tests_run();
            rc |= rd_tests_run();
            rc |= cjk_path_tests_run();
        }
        return rc;
    }

    /* 普通模式：完整窗口应用。 */
    return ui_main();
}
