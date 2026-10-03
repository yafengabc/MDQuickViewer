/* 文档中间模型：与 Go 版 render.go 的 Span/Block/TableData 同构。
 *
 * 内存策略：所有 Span 的 text 字段都指向 md_doc 的字符串池（不单独 malloc），
 * Block 数组一次性分配。释放时统一 md_doc_free()，避免各处 free 遗漏。
 * 这比 Go 版多了一层考虑：C 没有 GC，必须自己管。
 */
#ifndef GOMD_MODEL_H
#define GOMD_MODEL_H

#include "win32.h"

/* 对齐方式 */
enum { ALIGN_LEFT = 0, ALIGN_CENTER = 1, ALIGN_RIGHT = 2 };

/* 版面常量。解析层算 marker_x/indent、渲染层算 x0 都依赖它们，
 * 必须是同一份定义 —— 早先 markdown.c 里写LM=20 而 render.h 写 RD_LM=48，
 * 两个坐标系不一致，列表圆点会跑到左边距外侧。 */
#define DOC_LM 48              /* 左边距 */
#define DOC_LIST_INDENT_STEP 22 /* 每层列表缩进 */
#define DOC_QUOTE_INDENT 20     /* 引用块额外缩进 */

/* 行内片段：渲染层最小着色单位 */
typedef struct {
    const char *text; /* 指向字符串池 */
    int len;          /* 字节长度 */
    unsigned char bold, italic, code, strike;
    const char *link; /* 非 NULL 表示链接，指向字符串池 */
    int link_len;
    COLORREF color; /* 0 = 默认前景色 */
} Span;

/* 块类型（kind 用字符串表示，与 Go 版一致便于对照） */
typedef struct TableData TableData;

typedef struct {
    const char *text;
    int len;
    int align;
    Span *spans; /* 行内片段，指向 Doc 内部池 */
    int nspan;
} TableCell;

typedef struct {
    TableCell *cells;
    int ncell;
    int header;
    i32 y0, y1; /* 布局阶段回填 */
} TableRow;

struct TableData {
    int cols;
    i32 *col_x, *col_w; /* 布局阶段回填 */
    TableRow *rows;
    int nrow;
    i32 x0, x1;
};

/* 字符串池：解析时所有切片都指向这里，释放时一次性回收。
 * 必须先于 Doc 定义（Doc 内嵌了一份按值）。 */
typedef struct StrPool {
    char *buf;
    int len, cap;
} StrPool;
const char *pool_put(StrPool *p, const char *s, int len);
void pool_free(StrPool *p);

/* 语法高亮的语言定义表（highlight.c 里实现） */
typedef struct {
    const char *line_comment[2];  /* 行注释前缀，最多两个 */
    const char *block_open;        /* 块注释起始，例如 C 的 slash-star */
    const char *block_close;       /* 块注释结束，例如 C 的 star-slash */
    const char *quotes;            /* 字符串引号集合 */
    unsigned char has_preproc;     /* C 家族的 #include/#define */
    unsigned char has_dollar;      /* shell 的 $VAR */
    const char *const *extra_words; /* 语言专有词表（NULL 结尾的字符串数组） */
    unsigned char extra_class;     /* extra_words 的类别 */
} LangDef;

/* 块级元素。kind 取值：h1..h6 / p / code / hr / table */
typedef struct {
    const char *kind;

    /* 文本类（标题与段落）*/
    Span *spans;
    int nspan;

    /* 代码块 */
    const char **lines; /* 每行指向字符串池 */
    int *line_lens;
    int nline;
    char *lang; /* 围栏语言标注，NULL = 无 */

    /* 排版 */
    i32 indent;
    char quote;
    const char *marker; /* 列表标记；非 NULL 且本行为首行时绘制 */
    int marker_len;
    i32 marker_x;
    COLORREF color;

    /* 布局阶段回填 */
    i32 y0, y1;
    i32 first_ln;

    TableData *table;
} Block;

/* 带样式的文本片段：排版与测量的输入 */
typedef struct {
    const char *text;
    int len;
    const char *family;
    int px;
    unsigned char bold, italic, code, strike;
    COLORREF color, bg;
    const char *link;
    int link_len;
    unsigned char underline;
} RunItem;

/* 一个可独立绘制的单元。item.text 是整段，text 才是本单元实际画的字。 */
typedef struct {
    i32 x;
    RunItem item;
    const char *text;
    int text_len;
} DrawUnit;
/* 排版后的一个视觉行（可能来自折行） */
typedef struct {
    int block;
    i32 y, height;
    DrawUnit *units;
    int nunit;
    const char *marker;
    int marker_len;
    i32 marker_x;
    char wrap; /* true = 折行续行，复制时行尾不插换行 */
} VisualLine;

typedef struct {
    i32 x, y, w, h;
    const char *dest;
    int dest_len;
} LinkRect;

/* 解析后的整篇文档 */
typedef struct {
    const char *src; /* 字符串池（归一化换行后的完整文本） */
    int src_len;
    char *path;

    Block *blocks;
    int nblock;

    /* 布局阶段回填 */
    VisualLine *lines;
    int nline;
    LinkRect *link_rects;
    int nlink;
    i32 content_height;

    /* 内部：字符串池所有权，md_doc_free 回收 */
    StrPool __pool;

    /* 内部：布局期字符串池。折行时需要拼接同样式相邻片段，
     * 拼出来的新串挂在 DrawUnit.text 上，必须活过整个绘制阶段。
     * 每次 rd_layout 开头由 layout_clear 清空。 */
    StrPool __lpool;
} Doc;

/* 解析入口。内部会归一化 CRLF。返回的 Doc 需要用 md_doc_free 释放。 */
Doc *md_parse(const char *src, const char *path);
void md_doc_free(Doc *d);

/* 独立解析一段行内文本（供测试用）。返回的 Span 文本自持，
 * 需用 md_spans_free_n(s, n) 释放。 */
Span *md_parse_inline(const char *src, int len, int *out_n);
void md_spans_free_n(Span *s, int n);

#endif /* GOMD_MODEL_H */
