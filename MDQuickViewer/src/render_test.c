/* GDI 渲染层测试。
 *
 * 用法：MDQuickViewer.exe --test-render
 *
 * 三类检查：
 *   1. 排版几何：行数、y 单调递增、x 单调递增、行高与内容高度；
 *   2. 负宽度防护：width<=0 时不能崩、不能出负几何；
 *   3. 离屏位图自检：CreateDIBSection 上画一遍，把 BMP 落盘转 PNG 看图。
 */
#include "render.h"
#include "highlight.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int g_pass = 0, g_fail = 0;
static const char *g_group = "";
static FILE *g_out = NULL;

static void tout(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (g_out) {
        vfprintf(g_out, fmt, ap);
        fflush(g_out);
    }
    vfprintf(stdout, fmt, ap);
    fflush(stdout);
    va_end(ap);
}

static void fail(const char *file, int line, const char *fmt, ...) {
    g_fail++;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    if (g_out) {
        fprintf(g_out, "  FAIL [%s] %s:%d: %s\n", g_group, file, line, buf);
        fflush(g_out);
    }
    printf("  FAIL [%s] %s:%d: %s\n", g_group, file, line, buf);
    fflush(stdout);
}

#define CHECK(cond, ...)                          \
    do {                                          \
        if (cond) {                               \
            g_pass++;                             \
        } else {                                  \
            fail(__FILE__, __LINE__, __VA_ARGS__);\
        }                                         \
    } while (0)

/* ------------------------------------------------------------------ 工具 */

/* 把某视觉行的所有单元文本拼起来 */
static void line_text(VisualLine *vl, char *out, int outsz) {
    int bi = 0;
    out[0] = '\0';
    if (!vl) return;
    for (int i = 0; i < vl->nunit && bi < outsz - 1; i++) {
        int n = vl->units[i].text_len;
        if (bi + n >= outsz) n = outsz - 1 - bi;
        memcpy(out + bi, vl->units[i].text, (size_t)n);
        bi += n;
    }
    out[bi] = '\0';
}

/* 找第一行包含 needle 的视觉行，返回下标，找不到返回 -1 */
static int find_line(Doc *d, const char *needle) {
    char buf[1024];
    for (int i = 0; i < d->nline; i++) {
        line_text(&d->lines[i], buf, (int)sizeof(buf));
        if (strstr(buf, needle)) return i;
    }
    return -1;
}

/* 文档里所有行的完整文本 */
static void all_text(Doc *d, char *out, int outsz) {
    int bi = 0;
    char buf[1024];
    out[0] = '\0';
    for (int i = 0; i < d->nline; i++) {
        line_text(&d->lines[i], buf, (int)sizeof(buf));
        int n = (int)strlen(buf);
        if (bi + n + 2 >= outsz) break;
        memcpy(out + bi, buf, (size_t)n);
        bi += n;
        out[bi++] = '\n';
    }
    out[bi] = '\0';
}

/* ------------------------------------------------------------------ 基础几何 */

static void test_basic_layout(void) {
    g_group = "layout/basic";
    Doc *d = md_parse("# Hello\n\nWorld paragraph.\n", "t.md");
    rd_layout(d, 800);
    CHECK(d->nblock == 2, "期望 2 块，实际 %d", d->nblock);
    CHECK(d->nline == 2, "期望 2 行，实际 %d", d->nline);
    CHECK(d->content_height > 0, "content_height 应为正，实际 %d", (int)d->content_height);

    /* y 必须严格递增且为正 */
    for (int i = 1; i < d->nline; i++) {
        CHECK(d->lines[i].y > d->lines[i - 1].y, "第 %d 行 y 未递增：%d vs %d", i,
              (int)d->lines[i].y, (int)d->lines[i - 1].y);
    }
    /* h1 的字号行高应大于段落 */
    CHECK(d->lines[0].height > d->lines[1].height, "h1 行高 %d 应大于段落行高 %d",
          (int)d->lines[0].height, (int)d->lines[1].height);

    /* 行内 x 单调递增，首行左边界 = LM */
    char buf[256];
    line_text(&d->lines[0], buf, (int)sizeof(buf));
    CHECK(strcmp(buf, "Hello") == 0, "首行文本应为 Hello，实际 '%s'", buf);
    CHECK(d->lines[0].units[0].x == RD_LM, "首行应从 x=%d 起，实际 %d", RD_LM,
          (int)d->lines[0].units[0].x);
    CHECK(d->lines[0].units[0].item.px > d->lines[1].units[0].item.px, "h1 字号应大于正文字号");

    md_doc_free(d);
}

static void test_x_monotonic(void) {
    g_group = "layout/x-monotonic";
    Doc *d = md_parse(
        "The quick brown fox jumps over the lazy dog and keeps running far away.\n", "t.md");
    rd_layout(d, 400);
    for (int i = 0; i < d->nline; i++) {
        VisualLine *vl = &d->lines[i];
        for (int k = 1; k < vl->nunit; k++) {
            CHECK(vl->units[k].x > vl->units[k - 1].x, "第 %d 行第 %d 单元 x 未递增：%d vs %d", i, k,
                  (int)vl->units[k].x, (int)vl->units[k - 1].x);
        }
    }
    /* 折行后每行末尾不能超过可用宽度太多（允许 2px 舍入余量） */
    for (int i = 0; i < d->nline; i++) {
        VisualLine *vl = &d->lines[i];
        if (vl->nunit == 0) continue;
        DrawUnit *last = &vl->units[vl->nunit - 1];
        int w = rd_measure_text(last->text, last->text_len, last->item.family, last->item.px,
                                last->item.bold, last->item.italic);
        int right = (int)last->x + w;
        int limit = 400 - RD_RM;
        CHECK(right <= limit + 4, "第 %d 行右边界 %d 超出 %d", i, right, limit);
    }
    md_doc_free(d);
}

static void test_wrap_count(void) {
    g_group = "layout/wrap";
    const char *src =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    Doc *d = md_parse(src, "t.md");
    rd_layout(d, 400);
    CHECK(d->nline >= 2, "80 字符在 400px 宽应折成多行，实际 %d 行", d->nline);
    /* 首行 wrap=false，后续为 true */
    CHECK(d->lines[0].wrap == 0, "首行不应标记为折行续行");
    for (int i = 1; i < d->nline; i++) {
        CHECK(d->lines[i].wrap == 1, "第 %d 行应为折行续行", i);
    }
    char buf[4096];
    all_text(d, buf, (int)sizeof(buf));
    /* 去掉换行后应与原文一致（无字符丢失/重复） */
    char joined[4096];
    int ji = 0;
    for (int i = 0; buf[i]; i++) {
        if (buf[i] == '\n') continue;
        joined[ji++] = buf[i];
    }
    joined[ji] = '\0';
    CHECK(strcmp(joined, src) == 0, "折行后文本应与原文一致\n  期望: %.60s\n  实际: %.60s", src,
          joined);
    md_doc_free(d);
}

static void test_cjk_wrap(void) {
    g_group = "layout/cjk";
    const char *src = "中文测试文本需要能够正确折行显示在窄窗口里面而不越界溢出才对";
    Doc *d = md_parse(src, "t.md");
    rd_layout(d, 260);
    CHECK(d->nline >= 3, "38 个汉字在 260px 宽应折成 3 行以上，实际 %d", d->nline);
    for (int i = 0; i < d->nline; i++) {
        VisualLine *vl = &d->lines[i];
        for (int k = 0; k < vl->nunit; k++) {
            /* CJK 逐字成单元，不该出现多字节被切断的半个字符 */
            unsigned char c = (unsigned char)vl->units[k].text[0];
            CHECK((c & 0xC0) != 0x80, "第 %d 行第 %d 单元以 UTF-8 续字节开头", i, k);
        }
    }
    char buf[1024];
    all_text(d, buf, (int)sizeof(buf));
    char joined[1024];
    int ji = 0;
    for (int i = 0; buf[i]; i++) {
        if (buf[i] == '\n') continue;
        joined[ji++] = buf[i];
    }
    joined[ji] = '\0';
    CHECK(strcmp(joined, src) == 0, "CJK 折行后文本应一致：%.40s vs %.40s", src, joined);
    md_doc_free(d);
}

/* ------------------------------------------------------------------ 负宽度防护 */

static void test_negative_width(void) {
    g_group = "guard/negative";
    const char *src = "# Title\n\nSome paragraph text here.\n\n| a | b |\n|---|---|\n| 1 | 2 |\n";
    int widths[] = {0, -1, -100, 1, 2, 5};
    for (int i = 0; i < (int)(sizeof(widths) / sizeof(widths[0])); i++) {
        Doc *d = md_parse(src, "t.md");
        rd_layout(d, widths[i]);
        CHECK(d->content_height >= 0, "width=%d 时 content_height 为负：%d", widths[i],
              (int)d->content_height);
        for (int k = 0; k < d->nline; k++) {
            VisualLine *vl = &d->lines[k];
            CHECK(vl->y >= 0, "width=%d 时第 %d 行 y 为负：%d", widths[i], k, (int)vl->y);
            CHECK(vl->height >= 0, "width=%d 时第 %d 行 height 为负：%d", widths[i], k,
                  (int)vl->height);
            for (int u = 0; u < vl->nunit; u++) {
                CHECK(vl->units[u].x >= 0, "width=%d 时第 %d 行第 %d 单元 x 为负：%d", widths[i], k,
                      u, (int)vl->units[u].x);
            }
        }
        for (int k = 0; k < d->nlink; k++) {
            LinkRect *lr = &d->link_rects[k];
            CHECK(lr->w >= 0 && lr->h >= 0, "width=%d 时 linkRects[%d] 为负：%dx%d", widths[i], k,
                  (int)lr->w, (int)lr->h);
        }
        md_doc_free(d);
    }
    /* 反复用极窄宽度重排，确认没有内存损坏（UAF/越界都会被这一串打出来） */
    Doc *d = md_parse(src, "t.md");
    for (int i = 0; i < 200; i++) {
        rd_layout(d, (i % 2) ? 1 : 300);
    }
    CHECK(d->nline > 0, "反复重排后仍应有行");
    md_doc_free(d);

    /* 反复正常宽度重排（模拟 resize），布局池必须被正确回收 */
    d = md_parse(src, "t.md");
    for (int i = 100; i < 1400; i += 13) {
        rd_layout(d, i);
    }
    CHECK(d->content_height > 0, "反复重排后 content_height 应为正");
    md_doc_free(d);
}

static void test_paint_degenerate(void) {
    g_group = "guard/paint";
    Doc *d = md_parse("# T\n\nbody\n", "t.md");
    rd_layout(d, 600);
    /* 0 / 负 客户区尺寸必须直接返回，不能碰 GDI */
    HDC screen = GetDC(NULL);
    rd_paint(d, NULL, 0, 600, 400);
    rd_paint(d, screen, 0, 0, 0);
    rd_paint(d, screen, 0, -10, -10);
    rd_paint(d, screen, 0, 1, 1);
    rd_paint(d, screen, -50, 600, 400); /* 负滚动 */
    rd_paint(NULL, screen, 0, 600, 400);
    ReleaseDC(NULL, screen);
    CHECK(1, "退化尺寸绘制未崩溃");
    md_doc_free(d);
}

/* ------------------------------------------------------------------ 链接 */

static void test_links(void) {
    g_group = "link/hit";
    Doc *d = md_parse("Visit [gomd repo](https://github.com/yafengabc/gomd) now.\n", "t.md");
    rd_layout(d, 800);
    CHECK(d->nlink >= 1, "应有至少 1 个链接热区，实际 %d", d->nlink);

    char buf[256];
    int ln = find_line(d, "gomd repo");
    CHECK(ln >= 0, "应找到含链接文本的行");
    if (ln >= 0) {
        VisualLine *vl = &d->lines[ln];
        int hit = 0;
        for (int k = 0; k < d->nlink; k++) {
            LinkRect *lr = &d->link_rects[k];
            int cx = (int)lr->x + (int)lr->w / 2;
            int cy = (int)lr->y + (int)lr->h / 2;
            if (rd_hit_link(d, cx, cy, buf, (int)sizeof(buf)) > 0) {
                hit = 1;
                CHECK(strcmp(buf, "https://github.com/yafengabc/gomd") == 0,
                      "链接目标应为仓库地址，实际 '%s'", buf);
                break;
            }
        }
        CHECK(hit, "链接中心点应命中");
        /* 链接单元自身的左端内 1px 也应命中（units[0] 可能是 "Visit " 这样的前缀） */
        int left = -1, ly = (int)vl->y + (int)vl->height / 2;
        for (int k = 0; k < vl->nunit; k++) {
            if (vl->units[k].item.link) {
                left = (int)vl->units[k].x + 1;
                break;
            }
        }
        CHECK(left >= 0, "应存在带 link 的绘制单元");
        if (left >= 0) CHECK(rd_hit_link(d, left, ly, buf, (int)sizeof(buf)) > 0, "链接左端应命中");
    }
    /* 空白处不命中 */
    CHECK(rd_hit_link(d, 5, 2, buf, (int)sizeof(buf)) == 0, "空白处不应命中");

    /* 代码块内的内容不产生链接热区 */
    Doc *d2 = md_parse("```\n[a](http://x)\n```\n", "t.md");
    rd_layout(d2, 800);
    CHECK(d2->nlink == 0, "代码块内不应产生链接热区，实际 %d", d2->nlink);
    md_doc_free(d2);
    md_doc_free(d);
}

/* ------------------------------------------------------------------ 表格 */

static void test_table(void) {
    g_group = "table/layout";
    const char *src =
        "| Name | Age | City |\n"
        "|------|-----|------|\n"
        "| Alice | 30 | Beijing |\n"
        "| Bob | 4 | Xi'an |\n";
    Doc *d = md_parse(src, "t.md");
    rd_layout(d, 800);
    CHECK(d->nblock == 1, "表格应是 1 个块，实际 %d", d->nblock);
    TableData *td = d->blocks[0].table;
    CHECK(td != NULL, "表格块应带 table 数据");
    if (td) {
        CHECK(td->cols == 3, "期望 3 列，实际 %d", td->cols);
        CHECK(td->nrow == 3, "期望 3 行（含表头），实际 %d", td->nrow);
        CHECK(td->col_w != NULL && td->col_x != NULL, "列几何应已回填");
        CHECK(td->x1 > td->x0, "表格宽度应为正：%d..%d", (int)td->x0, (int)td->x1);
        for (int i = 1; i < td->cols; i++) {
            CHECK(td->col_x[i] > td->col_x[i - 1], "第 %d 列左边界未递增", i);
            CHECK(td->col_x[i] == td->col_x[i - 1] + td->col_w[i - 1], "第 %d 列左边界不连续", i);
        }
        for (int r = 1; r < td->nrow; r++) {
            CHECK(td->rows[r].y0 > td->rows[r - 1].y0, "第 %d 行 y 未递增", r);
        }
        CHECK(td->rows[0].header == 1, "首行应为表头");
    }
    char buf[1024];
    all_text(d, buf, (int)sizeof(buf));
    CHECK(strstr(buf, "Alice") != NULL, "应包含单元格内容 Alice");
    CHECK(strstr(buf, "Xi'an") != NULL, "应包含单元格内容 Xi'an");
    md_doc_free(d);

    /* 窄窗口：列宽必须仍为正，且列不重叠 */
    Doc *n = md_parse(src, "t.md");
    rd_layout(n, 210);
    TableData *nt = n->blocks[0].table;
    CHECK(nt && nt->col_w != NULL, "窄窗口下也应算出列宽");
    if (nt) {
        for (int i = 0; i < nt->cols; i++) {
            CHECK(nt->col_w[i] > 0, "窄窗口下第 %d 列宽应为正，实际 %d", i, (int)nt->col_w[i]);
        }
    }
    md_doc_free(n);
}

static void test_table_align(void) {
    g_group = "table/align";
    const char *src = "| L | C | R |\n| :-- | :-: | --: |\n| aaa | bbb | ccc |\n";
    Doc *d = md_parse(src, "t.md");
    CHECK(d->nblock == 1 && d->blocks[0].table, "应解析出表格");
    TableData *td = d->blocks[0].table;
    CHECK(td && td->nrow == 2, "期望 2 行");
    if (td && td->nrow == 2) {
        CHECK(td->rows[1].cells[0].align == ALIGN_LEFT, "第 0 列应左对齐");
        CHECK(td->rows[1].cells[1].align == ALIGN_CENTER, "第 1 列应居中");
        CHECK(td->rows[1].cells[2].align == ALIGN_RIGHT, "第 2 列应右对齐");
    }
    rd_layout(d, 800);
    /* 右对齐列的内容右边缘应贴近列右边界 */
    if (td && td->nrow == 2) {
        int col = 2;
        int cell_right = td->col_x[col] + td->col_w[col] - 8;
        int found = 0, best = -100000;
        for (int i = 0; i < d->nline; i++) {
            VisualLine *vl = &d->lines[i];
            if (vl->nunit == 0) continue;
            if (vl->units[0].x < td->col_x[col] - 1) continue;
            DrawUnit *last = &vl->units[vl->nunit - 1];
            int w = rd_measure_text(last->text, last->text_len, last->item.family, last->item.px,
                                    last->item.bold, last->item.italic);
            int right = (int)last->x + w;
            if (right - cell_right > best) best = right - cell_right;
            found = 1;
        }
        CHECK(found, "应能在右对齐列找到内容行");
        CHECK(best <= 4, "右对齐内容右缘应贴近列边界，实际超出 %d px", best);
    }
    md_doc_free(d);
}

/* ------------------------------------------------------------------ 代码块 */

static void test_code_block(void) {
    g_group = "code/block";
    const char *src = "```c\nint main(void) {\n    return 0;\n}\n```\n";
    Doc *d = md_parse(src, "t.md");
    CHECK(d->nblock == 1 && strcmp(d->blocks[0].kind, "code") == 0, "应为单个代码块");
    CHECK(d->blocks[0].lang && strcmp(d->blocks[0].lang, "c") == 0, "语言标注应为 c");
    rd_layout(d, 700);
    CHECK(d->nline == 3, "3 行源码应产生 3 个视觉行，实际 %d", d->nline);

    /* 固定行高：所有代码行行高一致 */
    if (d->nline == 3) {
        CHECK(d->lines[0].height == d->lines[1].height && d->lines[1].height == d->lines[2].height,
              "代码行高应一致：%d/%d/%d", (int)d->lines[0].height, (int)d->lines[1].height,
              (int)d->lines[2].height);
    }
    /* 关键字应被着色（与普通文本不同色） */
    char buf[256];
    line_text(&d->lines[0], buf, (int)sizeof(buf));
    CHECK(strstr(buf, "int") != NULL, "首行应含 int：%s", buf);
    int colored = 0;
    for (int i = 0; i < d->nline; i++) {
        for (int k = 0; k < d->lines[i].nunit; k++) {
            if (d->lines[i].units[k].item.color != hl_color(CC_PLAIN)) colored++;
        }
    }
    CHECK(colored > 0, "C 代码应产生非 plain 着色");

    /* 窄窗口下长行折行，但行高仍一致，且源码行不丢 */
    Doc *n = md_parse("```c\nint averyveryverylongvariablename = anotherveryverylongfunctioncall(a, b, c, d);\n```\n",
                      "t.md");
    rd_layout(n, 200);
    CHECK(n->nline >= 2, "窄窗口下长代码行应折行，实际 %d 行", n->nline);
    for (int i = 1; i < n->nline; i++) {
        CHECK(n->lines[i].height == n->lines[0].height, "折行后行高仍应一致");
        CHECK(n->lines[i].wrap == 1, "代码折行行应标记 wrap");
    }
    md_doc_free(n);
    md_doc_free(d);
}

/* ------------------------------------------------------------------ 列表/引用 */

static void test_list_quote(void) {
    g_group = "block/list-quote";
    Doc *d = md_parse("- alpha\n- beta\n\n> quoted line\n", "t.md");
    rd_layout(d, 700);
    CHECK(d->nblock == 3, "期望 3 块，实际 %d", d->nblock);
    /* 列表项与引用块首行应带 marker */
    int has_marker = 0, quote_block = 0;
    for (int i = 0; i < d->nblock; i++) {
        if (d->blocks[i].marker_len > 0) has_marker = 1;
        if (d->blocks[i].quote) quote_block = 1;
    }
    CHECK(has_marker, "列表块应有 marker");
    CHECK(quote_block, "应有引用块");
    for (int i = 0; i < d->nline; i++) {
        CHECK(d->lines[i].marker_len == 0 || d->lines[i].marker != NULL,
              "有 marker_len 就必须有 marker 指针");
        /* marker 只应出现在块的首行 */
        if (d->lines[i].marker_len > 0) {
            int bl = d->lines[i].block;
            CHECK(bl >= 0 && bl < d->nblock, "marker 行的块下标越界：%d", bl);
        }
    }
    md_doc_free(d);

    /* 嵌套列表 marker_x 应递增 */
    Doc *n = md_parse("- a\n  - b\n    - c\n", "t.md");
    rd_layout(n, 700);
    CHECK(n->nblock >= 2, "嵌套列表应产生多块，实际 %d", n->nblock);
    if (n->nblock >= 2) {
        CHECK(n->blocks[1].marker_x > n->blocks[0].marker_x, "嵌套 marker_x 应更深：%d vs %d",
              (int)n->blocks[1].marker_x, (int)n->blocks[0].marker_x);
    }
    md_doc_free(n);
}

static void test_inline_styles(void) {
    g_group = "inline/styles";
    Doc *d = md_parse(
        "plain **bold** _it_ ~~del~~ `code` [l](http://a)\n", "t.md");
    rd_layout(d, 900);
    CHECK(d->nblock == 1, "应为 1 段");
    Block *b = &d->blocks[0];
    CHECK(b->nspan >= 6, "应有多个 span，实际 %d", b->nspan);

    int f_bold = 0, f_ital = 0, f_strike = 0, f_code = 0, f_link = 0;
    for (int i = 0; i < b->nspan; i++) {
        if (b->spans[i].bold) f_bold = 1;
        if (b->spans[i].italic) f_ital = 1;
        if (b->spans[i].strike) f_strike = 1;
        if (b->spans[i].code) f_code = 1;
        if (b->spans[i].link) f_link = 1;
    }
    CHECK(f_bold, "应有粗体 span");
    CHECK(f_ital, "应有斜体 span");
    CHECK(f_strike, "应有删除线 span");
    CHECK(f_code, "应有行内代码 span");
    CHECK(f_link, "应有链接 span");

    /* 行内代码应使用等宽字体并带浅灰底 */
    rd_layout(d, 900);
    int found = 0;
    for (int i = 0; i < d->nline && !found; i++) {
        for (int k = 0; k < d->lines[i].nunit; k++) {
            DrawUnit *du = &d->lines[i].units[k];
            if (du->item.code && du->item.bg) {
                found = 1;
                CHECK(strcmp(du->item.family, RD_FONT_CODE) == 0, "行内代码应等宽字体");
            }
        }
    }
    CHECK(found, "应找到带背景的行内代码单元");
    md_doc_free(d);
}

/* ------------------------------------------------------------------ 双缓冲 */

/* 从任意 DC 的当前位图读回墨迹比例（用于验证双缓冲结果） */
static double ink_ratio_dc(HDC dc) {
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 0;
    bi.bmiHeader.biHeight = 0;
    /* 先问出当前位图尺寸 */
    HBITMAP cur = (HBITMAP)GetCurrentObject(dc, OBJ_BITMAP);
    BITMAP bmpinfo;
    memset(&bmpinfo, 0, sizeof(bmpinfo));
    if (!GetObject(cur, sizeof(bmpinfo), &bmpinfo)) return -1.0;
    int w = (int)bmpinfo.bmWidth, h = (int)abs((int)bmpinfo.bmHeight);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    int stride = w * 4;
    unsigned char *bits = (unsigned char *)malloc((size_t)stride * (size_t)h);
    if (!bits) return -1.0;
    double r = -1.0;
    if (GetDIBits(dc, cur, 0, (UINT)h, bits, &bi, DIB_RGB_COLORS)) {
        long ink = 0, total = 0;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                const unsigned char *p = bits + (size_t)y * (size_t)stride + (size_t)x * 4;
                total++;
                if (p[0] < 235 || p[1] < 235 || p[2] < 235) ink++;
            }
        }
        r = total ? (double)ink / (double)total : -1.0;
    }
    free(bits);
    return r;
}

static void test_double_buffer(void) {
    g_group = "paint/buffer";
    HDC screen = GetDC(NULL);
    if (!screen) {
        CHECK(0, "取不到屏幕 DC");
        return;
    }
    HDC dc = CreateCompatibleDC(screen);
    if (!dc) {
        CHECK(0, "CreateCompatibleDC 失败");
        ReleaseDC(NULL, screen);
        return;
    }
    Doc *d = md_parse("# Title\n\nsome **body** text with a [link](http://x)\n", "t.md");

    /* 同尺寸重复画：位图应复用，不重建 */
    rd_set_scale(1.0f);
    rd_layout(d, 400);
    rd_paint_buffered(d, dc, 0, 400, 300, (SelPos){0,0,0}, (SelPos){0,0,0}, 0);
    double a = ink_ratio_dc(dc);
    rd_paint_buffered(d, dc, 0, 400, 300, (SelPos){0,0,0}, (SelPos){0,0,0}, 0);
    double b = ink_ratio_dc(dc);
    CHECK(a > 0.005 && b > 0.005, "两次绘制都应有墨迹：%.4f / %.4f", a, b);
    CHECK(fabs(a - b) < 0.001, "同参数重复绘制结果应一致：%.4f vs %.4f", a, b);

    /* 改尺寸：必须能重建位图，且还能画 */
    rd_paint_buffered(d, dc, 0, 700, 500, (SelPos){0,0,0}, (SelPos){0,0,0}, 0);
    rd_layout(d, 700);
    rd_paint_buffered(d, dc, 0, 700, 500, (SelPos){0,0,0}, (SelPos){0,0,0}, 0);
    CHECK(ink_ratio_dc(dc) > 0.005, "变尺寸后仍应能绘制");

    /* 极小与负尺寸：走降级路径也不能崩 */
    rd_paint_buffered(d, dc, 0, 0, 0, (SelPos){0,0,0}, (SelPos){0,0,0}, 0);
    rd_paint_buffered(d, dc, 0, -5, -5, (SelPos){0,0,0}, (SelPos){0,0,0}, 0);
    rd_paint_buffered(d, dc, 0, 1, 1, (SelPos){0,0,0}, (SelPos){0,0,0}, 0);
    rd_paint_buffered(NULL, dc, 0, 400, 300, (SelPos){0,0,0}, (SelPos){0,0,0}, 0);
    CHECK(1, "退化尺寸下双缓冲未崩溃");

    buf_release();
    /* 释放后再画：应重新建位图 */
    rd_layout(d, 400);
    rd_paint_buffered(d, dc, 0, 400, 300, (SelPos){0,0,0}, (SelPos){0,0,0}, 0);
    CHECK(ink_ratio_dc(dc) > 0.005, "buf_release 后应能重建位图");
    buf_release();

    DeleteDC(dc);
    ReleaseDC(NULL, screen);
    md_doc_free(d);
}

/* ------------------------------------------------------------------ 缩放 */

static void test_scale(void) {
    g_group = "scale";
    Doc *d = md_parse("# Title\n\nbody text\n", "t.md");
    rd_set_scale(1.0f);
    rd_layout(d, 800);
    i32 h1 = d->content_height;
    int px1 = d->lines[0].units[0].item.px;

    rd_set_scale(2.0f);
    CHECK(rd_scale() > 1.9f, "缩放应生效：%f", (double)rd_scale());
    rd_layout(d, 800);
    CHECK(d->lines[0].units[0].item.px > px1, "2x 缩放字号应变大：%d vs %d",
          d->lines[0].units[0].item.px, px1);
    CHECK(d->content_height > h1, "2x 缩放内容高度应变大：%d vs %d", (int)d->content_height,
          (int)h1);

    /* 越界应被夹住而不是照单全收 */
    rd_set_scale(0.01f);
    CHECK(rd_scale() >= 0.5f, "缩放下限应为 0.5，实际 %f", (double)rd_scale());
    rd_set_scale(99.0f);
    CHECK(rd_scale() <= 3.0f, "缩放上限应为 3.0，实际 %f", (double)rd_scale());

    rd_set_scale(1.0f);
    md_doc_free(d);
}

/* ------------------------------------------------------------------ 离屏渲染 */

/* 从 CreateDIBSection 的 DIB 像素区导出 BMP 文件。
 * 直接memcpy，不用 GetDIBits —— 后者会按请求的格式重新量化，
 * 在非 32bpp 兼容位图上会把颜色压成黑白。 */
static int dump_bmp(const unsigned char *bits, int w, int h, const char *path) {
    int stride = w * 4;
    /* BMP 文件头 14 字节 + BITMAPINFOHEADER 40 字节 + 像素（32bpp 无调色板）。
     * 落盘高度必须为正：BMP 规范只认正高度 + 自底向上像素。 */
    int total = 14 + 40 + stride * h;
    unsigned char *buf = (unsigned char *)calloc(1, (size_t)total);
    if (!buf) return 0;
    buf[0] = 'B';
    buf[1] = 'M';
    buf[2] = (unsigned char)(total & 0xFF);
    buf[3] = (unsigned char)((total >> 8) & 0xFF);
    buf[4] = (unsigned char)((total >> 16) & 0xFF);
    buf[5] = (unsigned char)((total >> 24) & 0xFF);
    buf[10] = 54; /* 像素偏移 */
    BITMAPINFOHEADER bh;
    memset(&bh, 0, sizeof(bh));
    bh.biSize = sizeof(BITMAPINFOHEADER);
    bh.biWidth = w;
    bh.biHeight = h; /* 正= 自底向上 */
    bh.biPlanes = 1;
    bh.biBitCount = 32;
    bh.biCompression = BI_RGB;
    bh.biSizeImage = stride * h;
    memcpy(buf + 14, &bh, 40);
    /* DIB 是自顶向下，BMP 要自底向上，逐行倒序拷 */
    for (int y = 0; y < h; y++) {
        memcpy(buf + 54 + (size_t)(h - 1 - y) * (size_t)stride, bits + (size_t)y * (size_t)stride,
               (size_t)stride);
    }
    int ok = 0;
    FILE *f = NULL;
    if (fopen_s(&f, path, "wb") == 0 && f) {
        fwrite(buf, 1, (size_t)total, f);
        ok = (int)fclose(f) == 0;
    }
    free(buf);
    return ok;
}

/* 统计非背景像素比例，确认真的画了东西 */
static double ink_ratio(const unsigned char *bits, int w, int h) {
    int stride = w * 4;
    long ink = 0, total = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const unsigned char *p = bits + (size_t)y * (size_t)stride + (size_t)x * 4;
            total++;
            /* 白底（255,255,255）；容忍抗锯齿造成的浅灰 */
            if (p[0] < 235 || p[1] < 235 || p[2] < 235) ink++;
        }
    }
    return total ? (double)ink / (double)total : -1.0;
}

static void test_offscreen(void) {
    g_group = "paint/offscreen";
    HDC screen = GetDC(NULL);
    if (!screen) {
        CHECK(0, "取不到屏幕 DC");
        return;
    }
    HDC dc = CreateCompatibleDC(screen);
    CHECK(dc != NULL, "CreateCompatibleDC 失败");
    if (!dc) {
        ReleaseDC(NULL, screen);
        return;
    }

    int W = 900, H = 700;
    /* 用 CreateDIBSection 而不是 CreateCompatibleBitmap：
     * 后者的像素格式由当前显示设备决定（可能是 16bpp 或带调色板），
     * GetDIBits 按 32bpp BI_RGB 读回来就只剩黑白，颜色全丢。
     * DIB 的 bpp/baseline 布局在这里完全自己说了算，读回零成本。 */
    BITMAPINFO dib;
    memset(&dib, 0, sizeof(dib));
    dib.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    dib.bmiHeader.biWidth = W;
    dib.bmiHeader.biHeight = -H; /* 自顶向下，与下面逐行写入一致 */
    dib.bmiHeader.biPlanes = 1;
    dib.bmiHeader.biBitCount = 32;
    dib.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HBITMAP bmp = CreateDIBSection(dc, &dib, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp || !bits) {
        CHECK(0, "CreateDIBSection 失败");
        if (bmp) DeleteObject(bmp);
        DeleteDC(dc);
        ReleaseDC(NULL, screen);
        return;
    }
    HGDIOBJ old = SelectObject(dc, bmp);

    const char *src =
        "# gomd\n\n"
        "A **native** Win32 Markdown viewer written in _pure C_ with ~~no~~ WebView.\n\n"
        "## Features\n\n"
        "- GDI self-rendered preview\n"
        "- Fenced code with syntax highlight\n"
        "- GFM tables and task lists\n\n"
        "> Rendering happens on the UI thread, with double buffering.\n\n"
        "| Feature | Status | Notes |\n"
        "|---------|--------|-------|\n"
        "| Parser | done | 96 tests |\n"
        "| Highlight | done | 62 tests |\n\n"
        "```c\n"
        "int main(void) {\n"
        "    /* draw it yourself */\n"
        "    return paint(hdc);\n"
        "}\n"
        "```\n\n"
        "---\n\n"
        "See [the repository](https://github.com/yafengabc/gomd) for details.\n";

    Doc *d = md_parse(src, "sample.md");
    rd_set_scale(1.0f);
    rd_layout(d, W);

    /* 先画一遍到内存位图 */
    RECT all = {0, 0, W, H};
    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(dc, &all, white);
    DeleteObject(white);
    rd_paint(d, dc, 0, W, H);

    double r1 = ink_ratio((const unsigned char *)bits, W, H);
    CHECK(r1 > 0.01, "全量绘制应有可见墨迹，比例 %.4f", r1);
    /* 灰底必须真的出现：纯白说明 fill 走了别的路径或坐标算错 */
    {
        int gray = 0;
        for (int y = 0; y < H; y++) {
            const unsigned char *row = (const unsigned char *)bits + (size_t)y * W * 4;
            for (int x = 0; x < W; x++) {
                const unsigned char *p = row + (size_t)x * 4;
                if (p[0] > 235 && p[0] < 250 && p[0] == p[1] && p[1] == p[2]) gray++;
            }
        }
        CHECK(gray > 1000, "应有明显的浅灰底（代码块/表头），实际 %d 像素", gray);
    }
    /* 着色必须真的上色：统计非灰阶像素 */
    {
        int colored = 0;
        for (int y = 0; y < H; y++) {
            const unsigned char *row = (const unsigned char *)bits + (size_t)y * W * 4;
            for (int x = 0; x < W; x++) {
                const unsigned char *p = row + (size_t)x * 4;
                if (p[0] > 235) continue;
                int mx = p[0] > p[1] ? (p[0] > p[2] ? p[0] : p[2]) : (p[1] > p[2] ? p[1] : p[2]);
                int mn = p[0] < p[1] ? (p[0] < p[2] ? p[0] : p[2]) : (p[1] < p[2] ? p[1] : p[2]);
                if (mx - mn > 30) colored++; /* 明显有色相的像素 */
            }
        }
        CHECK(colored > 200, "语法着色/链接色应产生彩色像素，实际 %d", colored);
    }

    /* 滚动绘制：滚到中间，再滚到底 */
    int mid = (int)d->content_height / 2;
    rd_paint(d, dc, mid, W, H);
    CHECK(ink_ratio((const unsigned char *)bits, W, H) > -1.0, "滚动绘制不应失败");

    rd_paint(d, dc, (int)d->content_height, W, H);
    CHECK(ink_ratio((const unsigned char *)bits, W, H) > -1.0, "滚到底绘制不应失败");

    /* 回到顶部，导出 BMP 供肉眼检查 */
    rd_paint(d, dc, 0, W, H);
    wchar_t wpath[MAX_PATH];
    if (GetTempPathW(MAX_PATH, wpath)) {
        wcscat_s(wpath, MAX_PATH, L"gomd-render-check.bmp");
        char ansi[MAX_PATH * 3];
        int n = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, ansi, (int)sizeof(ansi), NULL, NULL);
        if (n > 0) {
            CHECK(dump_bmp((const unsigned char *)bits, W, H, ansi), "导出 BMP 失败");
            if (g_out) {
                fprintf(g_out, "  (截图: %s)\n", ansi);
                fflush(g_out);
            }
        }
    }

    /* 窄窗口重排 + 绘制：不能崩，墨迹仍需存在 */
    for (int w = 120; w <= 900; w += 40) {
        int hh = 400;
        rd_layout(d, w);
        rd_paint(d, dc, 0, w < W ? w : W, hh);
    }
    CHECK(1, "多宽度重排绘制未崩溃");

    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    ReleaseDC(NULL, screen);
    md_doc_free(d);
}

/* ------------------------------------------------------------------ 文本选择 */
static void test_selection(void) {
    g_group = "selection";
    const char *src = "# Hello World\n\nThis is a paragraph of text.\n\n"
                      "- item one\n- item two\n\n```c\nint x = 1;\n```\n";
    Doc *d = md_parse(src, "sel.md");
    CHECK(d != NULL, "selection: 解析失败");
    if (!d) return;
    rd_layout(d, 400);
    CHECK(d->nline > 0, "selection: 排版后应有视觉行");

    /* A: 顺序比较 */
    CHECK(rd_sel_equal((SelPos){0, 0, 0}, (SelPos){0, 0, 0}), "sel_equal 自身");
    CHECK(!rd_sel_equal((SelPos){0, 0, 0}, (SelPos){0, 0, 1}), "sel_equal 不同 ch");
    CHECK(rd_sel_less((SelPos){0, 0, 0}, (SelPos){0, 0, 1}), "sel_less ch");
    CHECK(!rd_sel_less((SelPos){0, 0, 1}, (SelPos){0, 0, 0}), "sel_less 反向");
    CHECK(rd_sel_less((SelPos){1, 0, 0}, (SelPos){2, 0, 0}), "sel_less line");

    /* B: 命中测试返回在范围内（覆盖整个文档高度 × 宽度网格） */
    int bad = 0;
    for (int y = 0; y <= (int)d->content_height; y += 7)
        for (int x = 0; x <= 400; x += 11) {
            SelPos p = rd_hit_pos(d, x, y);
            if (p.line < 0 || p.line > d->nline) bad++;
        }
    CHECK(bad == 0, "selection: 命中位置 line 越界 %d 处", bad);

    /* 首行内部应命中第 0 行 */
    int y0 = (int)d->lines[0].y + 5;
    SelPos top = rd_hit_pos(d, RD_LM + 5, y0);
    CHECK(top.line == 0, "selection: 首行内部应命中第 0 行，实际 %d", top.line);

    /* C: 空选区间应返回空串 */
    char *empty = rd_selected_text(d, (SelPos){0, 0, 0}, (SelPos){0, 0, 0});
    CHECK(empty != NULL && empty[0] == '\0', "selection: 空选应返回空串");
    free(empty);

    /* D: 全选 */
    SelPos a, b;
    rd_select_all(d, &a, &b);
    if (rd_sel_less(b, a)) { SelPos t = a; a = b; b = t; }
    CHECK(!rd_sel_equal(a, b), "selection: 全选起止应不同");
    char *all = rd_selected_text(d, a, b);
    CHECK(all != NULL && all[0] != '\0', "selection: 全选文本非空");
    if (all) {
        CHECK(strstr(all, "Hello") != NULL, "selection: 全选应包含 Hello");
        CHECK(strstr(all, "item two") != NULL, "selection: 全选应包含 item two");
        free(all);
    }

    /* E: 选区矩形非负、数量合理 */
    if (d->nline > 1) {
        SelPos p0 = rd_hit_pos(d, RD_LM + 5, (int)d->lines[0].y + 5);
        SelPos p1 = rd_hit_pos(d, 400, (int)d->lines[d->nline - 1].y + 5);
        if (rd_sel_less(p1, p0)) { SelPos t = p0; p0 = p1; p1 = t; }
        RECT rects[256];
        int n = rd_selection_rects(d, 0, p0, p1, rects, 256);
        CHECK(n >= 1, "selection: 选区矩形数应 >=1，实际 %d", n);
        int badr = 0;
        for (int i = 0; i < n; i++)
            if (rects[i].right < rects[i].left || rects[i].bottom < rects[i].top) badr++;
        CHECK(badr == 0, "selection: 选区矩形出现负尺寸 %d 个", badr);
    }

    md_doc_free(d);
}

/* ------------------------------------------------------------------ 着色回归 */

/* 离屏渲染一份含标题/链接/行内代码/带色代码块的文档，扫描像素，
 * 断言出现多种"有色相"颜色（max-min 通道差 > 25）。
 * 这直接守住两个历史 bug：
 *   1) 双缓冲用 CreateCompatibleBitmap 在内存 DC 上创建了 1 位单色位图，
 *      所有颜色被量化成黑/白 —— 整页文字纯黑、毫无着色；
 *   2) COLR / hl_color 把 0xRRGGBB 当 COLORREF 原样使用，R/B 通道对调，
 *      链接蓝变橙、关键字红变蓝。
 * 无显示（GetDC 失败）时跳过，不影响 CI。 */
static void test_color_output(void) {
    tout("--- 着色回归（离屏渲染，断言多种颜色）---\n");
    const char *md =
        "# 标题\n\n"
        "[链接](https://example.com) 与 `inline code` 行内代码。\n\n"
        "```c\nint x = 1; // 注释\nchar *s = \"hi\";\n```\n";
    Doc *d = md_parse(md, NULL);
    if (!d) { tout("  skip: md_parse 失败\n"); return; }
    rd_layout(d, 400);

    HDC s = GetDC(NULL);
    if (!s) { tout("  skip: 无显示设备\n"); md_doc_free(d); rd_font_cleanup(); return; }
    HDC dc = CreateCompatibleDC(s);
    ReleaseDC(NULL, s);
    if (!dc) { md_doc_free(d); rd_font_cleanup(); return; }

    int W = 400, H = 320;
    BITMAPINFOHEADER bi;
    memset(&bi, 0, sizeof(bi));
    bi.biSize = sizeof(bi);
    bi.biWidth = W;
    bi.biHeight = -H;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;
    RGBQUAD *bits = NULL;
    HBITMAP bmp = CreateDIBSection(dc, (BITMAPINFO *)&bi, DIB_RGB_COLORS,
                                   (void **)&bits, NULL, 0);
    if (!bmp || !bits) { DeleteDC(dc); md_doc_free(d); rd_font_cleanup(); return; }
    SelectObject(dc, bmp);

    rd_paint_buffered(d, dc, 0, W, H, (SelPos){0, 0, 0}, (SelPos){0, 0, 0}, 0);

    unsigned long *uniq = NULL;
    int nuniq = 0, cap = 0;
    for (int i = 0; i < W * H; i++) {
        unsigned char r = bits[i].rgbRed, g = bits[i].rgbGreen, b = bits[i].rgbBlue;
        if (r > 240 && g > 240 && b > 240) continue; /* 背景白 */
        int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
        int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
        if (mx - mn <= 25) continue; /* 灰阶/近灰跳过，只数有色相的 */
        unsigned long k = ((unsigned long)r << 16) | ((unsigned long)g << 8) | b;
        int found = 0;
        for (int j = 0; j < nuniq; j++) if (uniq[j] == k) { found = 1; break; }
        if (!found) {
            if (nuniq >= cap) {
                cap = cap ? cap * 2 : 32;
                uniq = (unsigned long *)realloc(uniq, (size_t)cap * sizeof(unsigned long));
            }
            uniq[nuniq++] = k;
        }
    }
    if (uniq) free(uniq);
    DeleteObject(bmp);
    DeleteDC(dc);
    md_doc_free(d);
    rd_font_cleanup();

    CHECK(nuniq >= 3, "着色: 离屏渲染应出现 >=3 种有色相颜色，实际 %d", nuniq);
    if (nuniq >= 3) tout("  PASS 着色生效，检测到 %d 种有色相颜色\n", nuniq);
}

/* ------------------------------------------------------------------ 入口 */

/* 日志文件与 markdown/highlight 测试共用同一份（追加写） */
static FILE *open_log(void) {
    wchar_t logpath[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, logpath)) return NULL;
    wcscat_s(logpath, MAX_PATH, L"MDQuickViewer-test.log");
    return _wfopen(logpath, L"a");
}

int rd_tests_run(void) {
    g_out = open_log();
    tout("=== 渲染层测试 ===\n");
    test_basic_layout();
    test_x_monotonic();
    test_wrap_count();
    test_cjk_wrap();
    test_negative_width();
    test_paint_degenerate();
    test_links();
    test_table();
    test_table_align();
    test_code_block();
    test_list_quote();
    test_inline_styles();
    test_scale();
    test_offscreen();
    test_double_buffer();
    test_selection();
    test_color_output();
    tout("=== 渲染层结果: %d 通过, %d 失败 ===\n", g_pass, g_fail);
    /* tout() 已经同时写日志和 stdout，这里只关文件，不要再打一遍 */
    if (g_out) {
        fclose(g_out);
        g_out = NULL;
    }
    return g_fail == 0 ? 0 : 1;
}