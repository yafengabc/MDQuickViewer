/* Markdown 解析器测试。
 *
 * 用法：MDQuickViewer.exe --test-md
 * 之所以做成可执行程序而不是独立测试二进制，是因为整个工程就是一个 GUI
 * 可执行文件，链接 libcomctl32 的 GUI 程序跑 printf 断言不方便。
 * 这里改成在 GUI 程序里跑自检，结果写到 stdout 与日志文件。
 */
#include "model.h"
#include "markdown_test.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

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

#define CHECK(cond, ...)                     \
    do {                                     \
        if (cond) {                          \
            g_pass++;                        \
        } else {                             \
            fail(__FILE__, __LINE__, __VA_ARGS__); \
        }                                    \
    } while (0)

/* 取出文档里第 n 个块 */
static Block *blk(Doc *d, int n) { return (n < d->nblock) ? &d->blocks[n] : NULL; }

/* 把块的 span 拼成字符串（写入调用者缓冲） */
static void spans_text(Block *b, char *out, int outsz) {
    int bi = 0;
    out[0] = '\0';
    if (!b) return;
    for (int i = 0; i < b->nspan && bi < outsz - 1; i++) {
        int n = b->spans[i].len;
        if (bi + n >= outsz) n = outsz - 1 - bi;
        memcpy(out + bi, b->spans[i].text, (size_t)n);
        bi += n;
    }
    out[bi] = '\0';
}

#define SPANS_TEXT(b, buf) spans_text((b), (buf), (int)sizeof(buf))

/* ------------------------------------------------------------------ 块级 */

static void test_headings(void) {
    g_group = "heading";
    Doc *d = md_parse("# h1\n## h2\n### h3\n#### h4\n##### h5\n###### h6\n", "t.md");
    CHECK(d->nblock == 6, "期望 6 个块，实际 %d", d->nblock);
    const char *want[6] = {"h1", "h2", "h3", "h4", "h5", "h6"};
    for (int i = 0; i < 6; i++) {
        Block *b = blk(d, i);
        CHECK(b && b->kind && strcmp(b->kind, want[i]) == 0, "第 %d 块应为 %s", i, want[i]);
    }
    md_doc_free(d);

    /* 7 个井号不是标题 */
    d = md_parse("####### x\n", "t.md");
    CHECK(d->nblock >= 1 && strcmp(d->blocks[0].kind, "p") == 0, "7 井号应降级为段落");
    md_doc_free(d);

    /* 闭合井号被剥掉 */
    d = md_parse("## Title ##\n", "t.md");
    char buf[64];
    SPANS_TEXT(blk(d, 0), buf);
    CHECK(strcmp(buf, "Title") == 0, "闭合井号应剥掉，实际 \"%s\"", buf);
    md_doc_free(d);
}

static void test_setext(void) {
    g_group = "setext";
    Doc *d = md_parse("Title\n=====\n\nSub\n---\n", "t.md");
    CHECK(d->nblock == 2, "期望 2 块，实际 %d", d->nblock);
    char buf[64];
    if (d->nblock == 2) {
        SPANS_TEXT(blk(d, 0), buf);
        CHECK(strcmp(blk(d, 0)->kind, "h1") == 0 && strcmp(buf, "Title") == 0,
              "Setext h1 应为 Title，实际 %s/\"%s\"", blk(d, 0)->kind, buf);
        SPANS_TEXT(blk(d, 1), buf);
        CHECK(strcmp(blk(d, 1)->kind, "h2") == 0 && strcmp(buf, "Sub") == 0,
              "Setext h2 应为 Sub，实际 %s/\"%s\"", blk(d, 1)->kind, buf);
    }
    md_doc_free(d);
}

static void test_paragraph(void) {
    g_group = "paragraph";
    Doc *d = md_parse("one\ntwo\nthree\n", "t.md");
    CHECK(d->nblock == 1, "3 行应合成 1 个段落，实际 %d 块", d->nblock);
    char buf[64];
    SPANS_TEXT(blk(d, 0), buf);
    CHECK(strcmp(buf, "one\ntwo\nthree") == 0, "段落内容 \"%s\"", buf);
    md_doc_free(d);
}

static void test_hr(void) {
    g_group = "hr";
    const char *cases[3] = {"---\n", "***\n", "___\n"};
    for (int i = 0; i < 3; i++) {
        Doc *d = md_parse(cases[i], "t.md");
        CHECK(d->nblock == 1 && strcmp(d->blocks[0].kind, "hr") == 0, "%s 应为 hr", cases[i]);
        md_doc_free(d);
    }
}

static void test_fenced_code(void) {
    g_group = "fenced";
    Doc *d = md_parse("```go\nfunc main() {}\n```\n", "t.md");
    Block *b = blk(d, 0);
    CHECK(b && b->kind && strcmp(b->kind, "code") == 0, "应为 code 块");
    if (b) {
        CHECK(b->lang && strcmp(b->lang, "go") == 0, "lang 应为 go，实际 %s",
              b->lang ? b->lang : "(null)");
        CHECK(b->nline == 1 && b->line_lens[0] == 14, "代码行数/长度 %d/%d", b->nline,
              b->nline ? b->line_lens[0] : -1);
    }
    md_doc_free(d);

    /* 语言标注归一化 */
    struct { const char *src, *want; } langs[4] = {
        {"```{.rs}\nx\n```\n", "rs"},
        {"```{rs}\nx\n```\n", "rs"},
        {"```rust,ignore\nx\n```\n", "rust"},
        {"```\nx\n```\n", ""},
    };
    for (int i = 0; i < 4; i++) {
        d = md_parse(langs[i].src, "t.md");
        Block *bb = blk(d, 0);
        const char *got = (bb && bb->lang) ? bb->lang : "";
        CHECK(strcmp(got, langs[i].want) == 0, "lang 归一化 %s → 期望 \"%s\" 实际 \"%s\"",
              langs[i].src, langs[i].want, got);
        md_doc_free(d);
    }

    /* 波浪号围栏 */
    d = md_parse("~~~\nplain\n~~~\n", "t.md");
    CHECK(d->nblock == 1 && d->blocks[0].nline == 1, "~~~ 围栏应被识别");
    md_doc_free(d);

    /* 未闭合围栏不应崩溃 */
    d = md_parse("```py\nx = 1\ny = 2\n", "t.md");
    CHECK(d->nblock == 1 && d->blocks[0].nline == 2, "未闭合围栏应吃掉剩余内容");
    md_doc_free(d);
}

static void test_indented_code(void) {
    g_group = "indented";
    Doc *d = md_parse("text\n\n    code one\n    code two\n", "t.md");
    CHECK(d->nblock == 2, "期望 2 块，实际 %d", d->nblock);
    if (d->nblock == 2) {
        Block *b = blk(d, 1);
        CHECK(strcmp(b->kind, "code") == 0, "第 2 块应为 code");
        CHECK(b->nline == 2, "代码 2 行，实际 %d", b->nline);
        if (b->nline == 2) {
            CHECK(b->line_lens[0] == 8 && memcmp(b->lines[0], "code one", 8) == 0,
                  "第 1 行 \"%.*s\"", b->line_lens[0], b->lines[0]);
        }
    }
    md_doc_free(d);
}

static void test_list(void) {
    g_group = "list";

    Doc *d = md_parse("- a\n- b\n", "t.md");
    CHECK(d->nblock == 2, "2 个列表项 → 2 块，实际 %d", d->nblock);
    if (d->nblock == 2) {
        CHECK(blk(d, 0)->marker && strcmp(blk(d, 0)->marker, "\xE2\x80\xA2") == 0,
              "无序标记应为 •");
        CHECK(blk(d, 0)->indent > 0, "列表项应有缩进");
        char buf[32];
        SPANS_TEXT(blk(d, 0), buf);
        CHECK(strcmp(buf, "a") == 0, "首项内容 \"%s\"", buf);
    }
    md_doc_free(d);

    /* 有序列表 */
    d = md_parse("1. a\n2. b\n3. c\n", "t.md");
    CHECK(d->nblock == 3, "3 个有序项，实际 %d", d->nblock);
    if (d->nblock == 3) {
        CHECK(strcmp(blk(d, 0)->marker, "1.") == 0, "第 1 项标记 1.");
        CHECK(strcmp(blk(d, 1)->marker, "2.") == 0, "第 2 项标记 2.");
        CHECK(strcmp(blk(d, 2)->marker, "3.") == 0, "第 3 项标记 3.");
    }
    md_doc_free(d);

    /* 自定义起始序号 */
    d = md_parse("5. five\n6. six\n", "t.md");
    CHECK(d->nblock == 2 && strcmp(blk(d, 0)->marker, "5.") == 0, "有序列表应从 5 开始");
    md_doc_free(d);

    /* 嵌套列表缩进递进 */
    d = md_parse("- a\n  - b\n- c\n", "t.md");
    int top = -1, nest = -1, c2 = -1;
    for (int i = 0; i < d->nblock; i++) {
        char b[32];
        SPANS_TEXT(blk(d, i), b);
        if (strcmp(b, "a") == 0 && top < 0) top = blk(d, i)->indent;
        if (strcmp(b, "b") == 0 && nest < 0) nest = blk(d, i)->indent;
        if (strcmp(b, "c") == 0 && c2 < 0) c2 = blk(d, i)->indent;
    }
    CHECK(top >= 0 && nest > top, "嵌套项缩进应更大 top=%d nest=%d", top, nest);
    CHECK(c2 == top, "回到顶层后缩进应复原 top=%d c=%d", top, c2);
    md_doc_free(d);
}

static void test_task_list(void) {
    g_group = "task";
    Doc *d = md_parse("- [ ] todo\n- [x] done\n", "t.md");
    CHECK(d->nblock == 2, "期望 2 块，实际 %d", d->nblock);
    if (d->nblock == 2) {
        CHECK(strcmp(blk(d, 0)->marker, "\xE2\x98\x90") == 0, "未勾选应为 ☐");
        CHECK(strcmp(blk(d, 1)->marker, "\xE2\x98\x91") == 0, "已勾选应为 ☑");
        char buf[32];
        SPANS_TEXT(blk(d, 0), buf);
        CHECK(strcmp(buf, "todo") == 0, "标记后不应残留 [ ]，实际 \"%s\"", buf);
    }
    md_doc_free(d);

    /* 大写 X 也算勾选 */
    d = md_parse("- [X] done\n", "t.md");
    CHECK(d->nblock == 1 && strcmp(blk(d, 0)->marker, "\xE2\x98\x91") == 0,
          "[X] 应识别为已勾选");
    md_doc_free(d);
}

static void test_quote(void) {
    g_group = "quote";
    Doc *d = md_parse("> quoted text\n", "t.md");
    CHECK(d->nblock == 1, "期望 1 块，实际 %d", d->nblock);
    if (d->nblock == 1) {
        CHECK(blk(d, 0)->quote == 1, "应打引用标记");
        CHECK(blk(d, 0)->indent > 0, "引用块应有缩进");
        CHECK(blk(d, 0)->color != 0, "引用块应有专属颜色");
    }
    md_doc_free(d);

    /* 引用内标题也要带标记 */
    d = md_parse("> # Title\n", "t.md");
    CHECK(d->nblock == 1 && strcmp(blk(d, 0)->kind, "h1") == 0 && blk(d, 0)->quote,
          "引用内标题应保留 h1 且带引用标记");
    md_doc_free(d);
}

static void test_table(void) {
    g_group = "table";
    Doc *d = md_parse("| L | C | R |\n|:--|:-:|--:|\n| a | b | c |\n", "t.md");
    CHECK(d->nblock == 1, "期望 1 块，实际 %d", d->nblock);
    if (d->nblock == 1 && blk(d, 0)->table) {
        TableData *t = blk(d, 0)->table;
        CHECK(strcmp(blk(d, 0)->kind, "table") == 0, "应为 table");
        CHECK(t->cols == 3, "3 列，实际 %d", t->cols);
        CHECK(t->nrow == 2, "2 行（表头+1），实际 %d", t->nrow);
        CHECK(t->rows[0].header == 1 && t->rows[1].header == 0, "首行应为表头");
        CHECK(t->rows[0].cells[0].align == ALIGN_LEFT, "第 1 列左对齐");
        CHECK(t->rows[0].cells[1].align == ALIGN_CENTER, "第 2 列居中");
        CHECK(t->rows[0].cells[2].align == ALIGN_RIGHT, "第 3 列右对齐");
    }
    md_doc_free(d);

    /* 表头加粗 */
    d = md_parse("| A | B |\n|---|---|\n| 1 | 2 |\n", "t.md");
    if (d->nblock == 1 && blk(d, 0)->table) {
        TableData *t = blk(d, 0)->table;
        CHECK(t->rows[0].cells[0].nspan > 0 && t->rows[0].cells[0].spans[0].bold,
              "表头应加粗");
        CHECK(t->rows[1].cells[0].nspan > 0 && !t->rows[1].cells[0].spans[0].bold,
              "表体不应加粗");
    }
    md_doc_free(d);

    /* 缺列补空 */
    d = md_parse("| A | B |\n|---|---|\n| 1 |\n", "t.md");
    if (d->nblock == 1 && blk(d, 0)->table) {
        TableData *t = blk(d, 0)->table;
        CHECK(t->rows[1].ncell == 2, "缺列应补空，实际 %d", t->rows[1].ncell);
        CHECK(t->rows[1].cells[1].nspan == 0, "补出的单元格应为空");
    }
    md_doc_free(d);

    /* 多列截断 */
    d = md_parse("| A |\n|---|\n| 1 | 2 | 3 |\n", "t.md");
    if (d->nblock == 1 && blk(d, 0)->table)
        CHECK(blk(d, 0)->table->rows[1].ncell == 1, "多余列应截断");
    md_doc_free(d);

    /* 无分隔行 → 降级为段落 */
    d = md_parse("| a | b |\n| c | d |\n", "t.md");
    int allp = d->nblock > 0;
    for (int i = 0; i < d->nblock; i++)
        if (strcmp(d->blocks[i].kind, "p") != 0) allp = 0;
    CHECK(allp, "无分隔行应全部降级为段落");
    md_doc_free(d);
}

/* ---------------------------------------------------------------- 行内 */

/* 拼接字面量取长度：md_parse_inline 需要显式长度，用宏避免手数出错 */
#define LIT(s) (s), (int)(sizeof(s) - 1)

static void test_inline(void) {
    g_group = "inline";
    int n = 0;
    Span *s;

    /* 行内代码不解析内部标记 */
    s = md_parse_inline(LIT("a `*not bold*` b"), &n);
    CHECK(n == 3, "期望 3 段，实际 %d", n);
    if (n == 3) {
        CHECK(s[1].code == 1 && !s[1].bold, "代码段内不应解析加粗");
        CHECK(s[1].len == 10 && memcmp(s[1].text, "*not bold*", 10) == 0, "代码内容 \"%.*s\"",
              s[1].len, s[1].text);
    }
    md_spans_free_n(s, n);

    /* 反引号转义 */
    s = md_parse_inline(LIT("``a`b``"), &n);
    CHECK(n == 1 && s[0].len == 3 && memcmp(s[0].text, "a`b", 3) == 0, "``a`b`` 应解析为 a`b");
    md_spans_free_n(s, n);

    /* 粗体/斜体 */
    s = md_parse_inline(LIT("**b** and *i*"), &n);
    int hasb = 0, hasi = 0;
    for (int i = 0; i < n; i++) {
        if (s[i].bold && s[i].len == 1 && s[i].text[0] == 'b') hasb = 1;
        if (s[i].italic && s[i].len == 1 && s[i].text[0] == 'i') hasi = 1;
    }
    CHECK(hasb, "应识别 **b**");
    CHECK(hasi, "应识别 *i*");
    md_spans_free_n(s, n);

    /* 下划线粗体 */
    s = md_parse_inline(LIT("__bold__"), &n);
    CHECK(n >= 1 && s[0].bold && s[0].len == 4, "__bold__ 应为加粗");
    md_spans_free_n(s, n);

    /* 斜体+粗体 */
    s = md_parse_inline(LIT("***both***"), &n);
    int both = 0;
    for (int i = 0; i < n; i++)
        if (s[i].bold && s[i].italic) both = 1;
    CHECK(both, "***both*** 应同时加粗+斜体");
    md_spans_free_n(s, n);

    /* 删除线 */
    s = md_parse_inline(LIT("~~gone~~"), &n);
    CHECK(n >= 1 && s[0].strike && s[0].len == 4, "~~gone~~ 应为删除线");
    md_spans_free_n(s, n);

    /* 链接 */
    s = md_parse_inline(LIT("see [docs](https://ex.com) now"), &n);
    int nl = 0;
    for (int i = 0; i < n; i++)
        if (s[i].link) nl++;
    CHECK(nl >= 1, "应有 1 个链接片段，实际 %d", nl);
    md_spans_free_n(s, n);

    /* 链接带 title：URL 只取空格前 */
    s = md_parse_inline(LIT("[t](https://a.com \"Title\")"), &n);
    CHECK(n >= 1 && s[0].link && strcmp(s[0].link, "https://a.com") == 0,
          "带 title 的链接 URL 应为 https://a.com，实际 %s", s[0].link ? s[0].link : "(null)");
    md_spans_free_n(s, n);

    /* 自动链接 */
    s = md_parse_inline(LIT("<https://ex.com/x>"), &n);
    CHECK(n >= 1 && s[0].link && strcmp(s[0].link, "https://ex.com/x") == 0, "尖括号自动链接");
    md_spans_free_n(s, n);

    /* 图片 */
    s = md_parse_inline(LIT("![alt](a.png)"), &n);
    CHECK(n >= 1 && s[0].link && strcmp(s[0].link, "a.png") == 0, "图片链接");
    md_spans_free_n(s, n);

    /* 嵌套：加粗内含斜体 */
    s = md_parse_inline(LIT("**bold with *inner* end**"), &n);
    int inner = 0;
    for (int i = 0; i < n; i++)
        if (s[i].len == 5 && memcmp(s[i].text, "inner", 5) == 0 && s[i].bold && s[i].italic)
            inner = 1;
    CHECK(inner, "**bold with *inner* end** 内层应同时 bold+italic");
    md_spans_free_n(s, n);

    /* 链接嵌在加粗内 */
    s = md_parse_inline(LIT("**[L](u)**"), &n);
    CHECK(n >= 1 && s[0].bold && s[0].link, "加粗内的链接应同时有 bold 与 link");
    md_spans_free_n(s, n);

    /* 孤立星号不当强调 */
    s = md_parse_inline(LIT("2 * 3 * 4"), &n);
    int has2 = 0, has4 = 0;
    for (int i = 0; i < n; i++) {
        if (strchr(s[i].text, '2')) has2 = 1;
        if (strchr(s[i].text, '4')) has4 = 1;
    }
    CHECK(has2 && has4, "孤立 * 不应吞掉内容");
    md_spans_free_n(s, n);

    /* 未闭合括号不 hang */
    s = md_parse_inline(LIT("[unclosed]( and [also"), &n);
    CHECK(n >= 0, "未闭合括号不应崩溃");
    md_spans_free_n(s, n);
}

static void test_edge(void) {
    g_group = "edge";

    /* 空输入 */
    Doc *d = md_parse("", "t.md");
    CHECK(d->nblock == 0, "空输入应无块");
    md_doc_free(d);

    d = md_parse("\n\n\n", "t.md");
    CHECK(d->nblock == 0, "纯换行应无块");
    md_doc_free(d);

    d = md_parse("   \n\t\n  \n", "t.md");
    CHECK(d->nblock == 0, "空白行应无块");
    md_doc_free(d);

    /* CRLF 归一化 */
    d = md_parse("# T\r\n\r\nbody\r\n", "t.md");
    CHECK(d->nblock == 2, "CRLF 文档应有 2 块，实际 %d", d->nblock);
    if (d->nblock >= 1 && blk(d, 0)->nspan > 0)
        CHECK(strchr(blk(d, 0)->spans[0].text, '\r') == NULL, "不应残留 CR");
    md_doc_free(d);

    /* 深层嵌套不 hang */
    d = md_parse("- a\n  - b\n    - c\n      - d\n        - e\n", "t.md");
    CHECK(d->nblock > 0, "深层嵌套应产出块");
    md_doc_free(d);

    /* 列表中多空行不 hang */
    d = md_parse("- a\n\n\n\n- b\n\n\n", "t.md");
    CHECK(d->nblock > 0, "多空行列表应终止");
    md_doc_free(d);

    /* 综合用例不 panic */
    const char *mix = "# T\n\nintro *text*\n\n- a\n- [x] b\n\n> q\n\n```rust\nfn x(){}\n```\n\n"
                      "| A | B |\n|---|---|\n| 1 | 2 |\n\n---\n\nend\n";
    d = md_parse(mix, "t.md");
    int has_h = 0, has_code = 0, has_table = 0, has_hr = 0;
    for (int i = 0; i < d->nblock; i++) {
        const char *k = d->blocks[i].kind;
        if (k[0] == 'h') has_h = 1;
        if (strcmp(k, "code") == 0) has_code = 1;
        if (strcmp(k, "table") == 0) has_table = 1;
        if (strcmp(k, "hr") == 0) has_hr = 1;
    }
    CHECK(has_h, "综合用例应含标题");
    CHECK(has_code, "综合用例应含代码块");
    CHECK(has_table, "综合用例应含表格");
    CHECK(has_hr, "综合用例应含分隔线");
    md_doc_free(d);

    /* UTF-8 中文：首段是 "这是"（2 字 = 6 字节） */
    d = md_parse("# \xE6\xA0\x87\xE9\xA2\x98\n\n\xE8\xBF\x99\xE6\x98\xAF**\xE4\xB8\xAD\xE6\x96\x87**"
                 "\xE6\xB5\x8B\xE8\xAF\x95\xE3\x80\x82\n",
                 "t.md");
    CHECK(d->nblock == 2, "中文文档应有 2 块，实际 %d", d->nblock);
    if (d->nblock >= 2 && blk(d, 1)->nspan > 0) {
        CHECK(blk(d, 1)->spans[0].len == 6, "中文 span 长度应按字节计（\"这是\"=6），实际 %d",
              blk(d, 1)->spans[0].len);
        CHECK(blk(d, 1)->spans[0].len == 6 && !blk(d, 1)->spans[0].bold,
              "加粗前的中文不应带 bold");
    }
    md_doc_free(d);
}

int md_tests_run(void) {
    g_pass = g_fail = 0;
    /* GUI 子系统下 stdout 可能接不到调用方的管道，同时写一份日志文件 */
    wchar_t logpath[MAX_PATH];
    if (GetTempPathW(MAX_PATH, logpath)) {
        wcscat_s(logpath, MAX_PATH, L"MDQuickViewer-test.log");
        g_out = _wfopen(logpath, L"w");
    }
    tout("=== Markdown 解析器测试 ===\n");
    test_headings();
    test_setext();
    test_paragraph();
    test_hr();
    test_fenced_code();
    test_indented_code();
    test_list();
    test_task_list();
    test_quote();
    test_table();
    test_inline();
    test_edge();
    /* tout() 已经同时写日志和 stdout，这里只关文件，不要再打一遍 */
    tout("=== 结果: %d 通过, %d 失败 ===\n", g_pass, g_fail);
    if (g_out) {
        fclose(g_out);
        g_out = NULL;
    }
    return g_fail == 0 ? 0 : 1;
}
