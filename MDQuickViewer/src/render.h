/* GDI 渲染：排版（layout）+ 自绘（paint）+ 双缓冲。
 *
 * 与 Go 版 render.go 的对应关系：
 *   layout()      → rd_layout()
 *   doText/doCode → rd_do_text() / rd_do_code()
 *   doTable       → rd_do_table()
 *   wrapUnits     → rd_wrap_units()
 *   paint()       → rd_paint()
 *
 * 【负尺寸防护】Go 版崩溃（0xC0000005）的根因是负宽度传给 MoveWindow 调整
 * comctl32 控件。这里把"绝不产生负值"写进类型系统：
 *   - 所有几何量用 int（有符号），但对外入口一律先过 clamp_nonneg()；
 *   - RECT 构造前四个分量全部夹到 >= 0；
 *   - avail（可用宽度）有下限 80，绝不为 0/负。
 */
#ifndef GOMD_RENDER_H
#define GOMD_RENDER_H

#include "model.h"

/* 排版常量。左边距与列表缩进步长必须与解析层一致，直接复用 model.h 里的
 * DOC_LM / DOC_LIST_INDENT_STEP，不要在这里另起一份。 */
#define RD_LM DOC_LM /* 左边距 */
#define RD_RM 48/* 右边距 */
#define RD_TM 30   /* 顶边距 */
#define RD_LEAD 4  /* 行距 */
#define RD_BODY_PX 15
#define RD_CODE_PX 14

#define RD_FONT_UI "Microsoft YaHei"
#define RD_FONT_CODE "Consolas"

/* ---------------------------------------------------------------- 文本选择
 * 选择位置用 (视觉行, 行内绘制单元, 单元内 rune 偏移) 三元组表示。
 * 定位全部基于已排版的 DrawUnit，因此与渲染结果严格一致。
 * 选择状态（anchor/head/on/drag）由 ui.c 持有；这里只提供纯计算。 */
typedef struct {
    int line; /* 视觉行索引 */
    int unit; /* 行内绘制单元索引 */
    int ch;   /* 单元内 rune 偏移 */
} SelPos;

int rd_sel_less(SelPos a, SelPos b);
int rd_sel_equal(SelPos a, SelPos b);

/* 内容坐标 (x,y) -> 选择位置。y 已经是内容坐标（调用方已加 scroll）。 */
SelPos rd_hit_pos(Doc *d, int x, int y);

/* 选区矩形（屏幕坐标，y 已减 scroll）。返回矩形个数，写入 out（最多 maxn）。 */
int rd_selection_rects(Doc *d, int scroll, SelPos a, SelPos b, RECT *out, int maxn);

/* 选中文本：传入规范化后的 [a,b]。返回 malloc 的 UTF-8 串，需调用方 free。 */
char *rd_selected_text(Doc *d, SelPos a, SelPos b);

/* 全选范围：写入 *a, *b。 */
void rd_select_all(Doc *d, SelPos *a, SelPos *b);

/* 全局缩放（Ctrl+滚轮调整），1.0 = 100% */
float rd_scale(void);
void rd_set_scale(float s);

/* 排版：给定客户区宽度，重算 lines / linkRects / contentHeight。 */
void rd_layout(Doc *d, int width);

/* 绘制：scroll 为已滚过的像素量。width/height 为客户区尺寸。 */
void rd_paint(Doc *d, HDC hdc, int scroll, int width, int height);

/* 双缓冲绘制：整帧画到内存位图再一次性贴到目标 DC。
 * 位图按尺寸缓存，resize 时自动重建；失败时自动降级为直接绘制。
 * a,b,selOn 为选区（来自 ui.c 的选择状态）；无选区传 {0,0,0},{0,0,0},0。 */
void rd_paint_buffered(Doc *d, HDC hdc, int scroll, int width, int height,
                       SelPos a, SelPos b, int selOn);

/* 释放双缓冲位图（窗口销毁/退出时调） */
void buf_release(void);

/* 命中测试：返回 (x,y) 处的链接目标（写入 buf，返回长度；无链接返回 0） */
int rd_hit_link(Doc *d, int x, int y, char *buf, int bufsz);

/* 字体缓存清理（程序退出时调） */
void rd_font_cleanup(void);

/* 供测试：把某段文本排版成单行并返回宽度 */
int rd_measure_text(const char *s, int len, const char *family, int px, int bold, int italic);
/* 供测试：把某段 UTF-8 文本按渲染规则拆成折行单元，dump 成可读串 */
char *rd_dump_layout(const char *src, const char *path, int width);

#endif
