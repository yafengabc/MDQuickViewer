/* 布局压测：直接调用 ui.c 里真实的 split_list_preview，模拟窗口被拖到极小 /
 * 负尺寸 / 疯狂 resize 时，列表与预览宽度是否始终保持非负、且两者之和不超过
 * 客户区宽度。这正是 Go 版 MoveWindow 收到负宽触发 0xC0000005 的根因防护。
 *
 * 编译（控制台子系统，避免拉起窗口）：
 *   gcc -O2 -std=c11 -D_WIN32_WINNT=0x0601 -D_WIN32_IE=0x0600 \
 *       src/layout_stress.c src/ui.c src/render.c src/markdown.c \
 *       src/win32.c src/dlg.c src/settings.c \
 *       -mconsole -lcomctl32 -lgdi32 -luser32 -lshell32 -lshlwapi \
 *       -lole32 -luuid -o /tmp/layout_stress.exe
 */
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

static int failures = 0;

static void check(int cw, int panelW, int showList) {
    int listW = -1, prevW = -1;
    split_list_preview(cw, panelW, showList, &listW, &prevW);

    if (listW < 0 || prevW < 0) {
        fprintf(stderr, "FAIL 负值: cw=%d panelW=%d show=%d -> listW=%d prevW=%d\n",
                cw, panelW, showList, listW, prevW);
        failures++;
        return;
    }
    int sum = listW + prevW;
    int eff = cw < 0 ? 0 : cw;
    if (sum != eff) {
        fprintf(stderr, "FAIL 和不符: cw=%d panelW=%d show=%d -> listW=%d prevW=%d sum=%d eff=%d\n",
                cw, panelW, showList, listW, prevW, sum, eff);
        failures++;
    }
    if (listW > eff || prevW > eff) {
        fprintf(stderr, "FAIL 超界: cw=%d panelW=%d show=%d -> listW=%d prevW=%d eff=%d\n",
                cw, panelW, showList, listW, prevW, eff);
        failures++;
    }
}

int main(void) {
    /* 1) 系统性边界：负数、0、极小、正常、巨大 */
    int cws[] = {
        INT_MIN, -100000, -1000, -100, -1, 0, 1, 2, 3, 5, 10, 50,
        99, 100, 101, 280, 500, 1000, 100000, INT_MAX
    };
    int panels[] = {0, 1, 50, 280, 10000};
    for (int i = 0; i < (int)(sizeof(cws) / sizeof(cws[0])); i++)
        for (int p = 0; p < (int)(sizeof(panels) / sizeof(panels[0])); p++)
            for (int s = 0; s < 2; s++)
                check(cws[i], panels[p], s);

    /* 2) 随机压测 100 万次，模拟疯狂 resize */
    unsigned int seed = 12345u;
    for (int n = 0; n < 1000000; n++) {
        seed = seed * 1103515245u + 12345u;
        int cw = (int)(seed & 0x1FFFF);          /* 0..131071 */
        if (n % 7 == 0) cw = -(int)(seed & 0x3FF); /* 偶尔负 */
        if (n % 13 == 0) cw = (seed & 0x80000000u) ? INT_MIN : INT_MAX;
        seed = seed * 1103515245u + 12345u;
        int panel = (int)(seed & 0x7FFF);
        int show = (int)((seed >> 8) & 1);
        check(cw, panel, show);
    }

    if (failures == 0) {
        printf("LAYOUT STRESS OK: 所有输入下 listW/prevW 均 >=0 且和不超过 cw\n");
        return 0;
    }
    fprintf(stderr, "\nLAYOUT STRESS 失败 %d 例\n", failures);
    return 1;
}
