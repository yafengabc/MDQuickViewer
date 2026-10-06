/* Markdown 解析器：把源码转成扁平的块序列（Doc）。
 *
 * 与 Go 版（goldmark）行为对齐的要点：
 *  - 列表/引用在解析阶段就摊平成带 indent/marker 的块，渲染层无需递归容器；
 *  - 任务列表的 [ ] / [x] 提取成 ☑/☐ 标记，正文里不再残留；
 *  - 表格支持 :--- / :---: / ---: 三种对齐，缺列补空、多列截断；
 *  - 围栏代码块保留 lang 供语法高亮。
 *
 * 自己写而不用库的原因：Go 版 goldmark 的 AST 有几个反直觉行为（表头单元格直接
 * 挂在 TableHeader 下没有 Row 包装、任务复选框塞在段首），移植时踩过坑。
 *
 * 递归设计：parse_blocks 显式接收 (src, Lines) 上下文，因此列表项/引用块的
 * 子解析只需构造一份新的 Lines 视图（指向池内子串），无需全局状态。
 *
 * 内存：所有切片指向 P::pool，Doc 释放时统一回收。
 */
#include "model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ 字符串池 */
/* StrPool 类型本身在 model.h 里定义（Doc 内嵌了一份），这里只实现操作。
 *
 * 关键设计：绝不在添加内容时 realloc 已经发出的内存。
 * 早期实现用单个大 buf + realloc，结果 realloc 搬动内存后此前发出的所有
 * const char* 全部悬垂（表现为 kind 字段互相覆盖："h1Titleh2Sub"）。
 * 改成固定大小的块链表：每块独立 malloc，永不移动，块内指针天然稳定。
 */

#define POOL_CHUNK 8192

typedef struct PoolChunk {
    struct PoolChunk *next;
    int used, cap;
    char data[1]; /* 实际分配 cap 字节 */
} PoolChunk;

struct PoolChunkList {
    PoolChunk *head;
};

const char *pool_put(StrPool *p, const char *s, int len) {
    if (len < 0) len = 0;
    /* 块链表存在 p->buf 位置（首个块指针） */
    PoolChunk *head = (PoolChunk *)p->buf;
    if (!head || head->cap - head->used < len + 1) {
        int cap = POOL_CHUNK;
        if (len + 1 > cap) cap = len + 1;
        PoolChunk *nc = (PoolChunk *)malloc(sizeof(PoolChunk) + (size_t)cap);
        if (!nc) return NULL;
        nc->next = head;
        nc->used = 0;
        nc->cap = cap;
        p->buf = (char *)nc;
        head = nc;
    }
    char *r = head->data + head->used;
    if (len && s) memcpy(r, s, (size_t)len);
    r[len] = '\0';
    head->used += len + 1;
    p->len += len;
    return r;
}

void pool_free(StrPool *p) {
    PoolChunk *c = (PoolChunk *)p->buf;
    while (c) {
        PoolChunk *nx = c->next;
        free(c);
        c = nx;
    }
    p->buf = NULL;
    p->len = p->cap = 0;
}

/* ------------------------------------------------------------------ 动态数组 */

#define VEC(T, name)                                                  \
    typedef struct {                                                  \
        T *d;                                                         \
        int n, cap;                                                   \
    } name;                                                            \
    static void name##_push(name *v, T x) {                           \
        if (v->n >= v->cap) {                                         \
            v->cap = v->cap ? v->cap * 2 : 16;                        \
            T *nd = (T *)realloc(v->d, (size_t)v->cap * sizeof(T));   \
            if (!nd) return;                                          \
            v->d = nd;                                                \
        }                                                             \
        v->d[v->n++] = x;                                             \
    }

VEC(Block, BlockVec)
VEC(Span, SpanVec)
VEC(TableRow, RowVec)

/* ------------------------------------------------------------------ 常量 */

/* 版面常量统一在 model.h（DOC_LM / DOC_LIST_INDENT_STEP / DOC_QUOTE_INDENT），
 * 解析层与渲染层必须共用同一份，否则 marker_x 与正文 x0 落在两个坐标系里。 */
#define COL_QUOTE_TEXT RGB(0x4A, 0x4A, 0x4A)
#define MAX_TABLE_COLS 64

/* ------------------------------------------------------------------ 行视图 */

/* 一段源码按行切分后的视图。lstart[k] 是第 k 行在 src 中的字节偏移。 */
typedef struct {
    const char *src;
    int *lstart;
    int *llen;
    int nline;
    int owned; /* lstart/llen 是否需要 free */
} Lines;

static void lines_free(Lines *L) {
    if (L->owned) {
        free(L->lstart);
        free(L->llen);
    }
    L->lstart = NULL;
    L->llen = NULL;
    L->nline = 0;
}

/* 按 \n 切分构建 Lines（offset_fn 可为 NULL，此时 offset 即指针差） */
static void lines_split(Lines *L, const char *src, int len) {
    L->src = src;
    L->owned = 1;
    int cap = 64, n = 0;
    int *st = (int *)malloc((size_t)cap * sizeof(int));
    int *ln = (int *)malloc((size_t)cap * sizeof(int));
    int start = 0;
    for (int i = 0; i <= len; i++) {
        if (i == len || src[i] == '\n') {
            int e = i;
            if (e > start && src[e - 1] == '\r') e--; /* 去 CR */
            /* 末尾的空行（源串以 \n 结尾时最后一段）不计入，
             * 否则代码块会多出一行空白。 */
            if (i == len && e == start && start > 0) break;
            if (n == cap) {
                cap *= 2;
                st = (int *)realloc(st, (size_t)cap * sizeof(int));
                ln = (int *)realloc(ln, (size_t)cap * sizeof(int));
            }
            st[n] = start;
            ln[n] = e - start;
            n++;
            start = i + 1;
        }
    }
    L->lstart = st;
    L->llen = ln;
    L->nline = n;
}

/* 取第 i 行（越界返回空行） */
static void line_at(const Lines *L, int i, const char **s, int *n) {
    if (i < 0 || i >= L->nline) {
        *s = "";
        *n = 0;
        return;
    }
    *s = L->src + L->lstart[i];
    *n = L->llen[i];
}

/* ------------------------------------------------------------------ 解析器 */

typedef struct {
    struct StrPool pool;
    BlockVec out;
} P;

/* 行内样式栈 */
typedef struct {
    unsigned char bold, italic, strike;
    const char *link;
    int link_len;
} Style;

/* ---------------------------------------------------------------- 行内解析 */

static int find_char(const char *s, int start, int end, char t) {
    for (int i = start; i < end; i++)
        if (s[i] == t) return i;
    return -1;
}

static int find_seq(const char *s, int start, int end, const char *seq, int slen) {
    if (slen <= 0 || start + slen > end) return -1;
    for (int i = start; i <= end - slen; i++) {
        if (memcmp(s + i, seq, (size_t)slen) == 0) return i;
    }
    return -1;
}

static int run_len(const char *s, int start, int end, char c) {
    int k = 0;
    while (start + k < end && s[start + k] == c) k++;
    return k;
}

/* 找与开 run 等长的反引号串，start 是开 run 之后的位置。
 * n = 开 run 的长度（调用方从 i 处数）。 */
static int find_backtick_run_n(const char *s, int start, int end, int n) {
    if (n <= 0 || start >= end) return -1;
    int i = start;
    while (i < end) {
        if (s[i] == '`') {
            int m = run_len(s, i, end, '`');
            if (m == n) return i;
            i += m;
        } else i++;
    }
    return -1;
}

/* 单字符强调闭合位置：跳过转义、代码段、空白后的标记 */
static int find_emph_close(const char *s, int start, int end, char c) {
    int i = start;
    while (i < end) {
        if (s[i] == '\\') {
            i += 2;
            continue;
        }
        if (s[i] == '`') {
            int open_run = run_len(s, i, end, '`');
            int cl = find_backtick_run_n(s, i + open_run, end, open_run);
            if (cl >= 0) {
                i = cl + open_run;
                continue;
            }
            i += open_run;
            continue;
        }
        if (s[i] == c) {
            char prev = (i > 0) ? s[i - 1] : ' ';
            if (prev == ' ' || prev == '\t' || prev == '\n') {
                i++;
                continue;
            }
            return i;
        }
        i++;
    }
    return -1;
}

/* 解析 [text](url "title")，成功返回 1 */
static int parse_link_syntax(const char *s, int start, int end, int *text_off, int *text_len,
                             int *url_off, int *url_len, int *next) {
    if (start >= end || s[start] != '[') return 0;
    int depth = 0, close = -1;
    for (int i = start; i < end; i++) {
        if (s[i] == '\\') {
            i++;
            continue;
        }
        if (s[i] == '[') depth++;
        else if (s[i] == ']') {
            if (--depth == 0) {
                close = i;
                break;
            }
        }
    }
    if (close < 0 || close + 1 >= end || s[close + 1] != '(') return 0;

    int pd = 0, pclose = -1;
    for (int i = close + 1; i < end; i++) {
        if (s[i] == '(') pd++;
        else if (s[i] == ')') {
            if (--pd == 0) {
                pclose = i;
                break;
            }
        }
    }
    if (pclose < 0) return 0;

    *text_off = start + 1;
    *text_len = close - start - 1;
    *url_off = close + 2;
    int rawlen = pclose - close - 2;
    int ul = rawlen;
    for (int i = 0; i < rawlen; i++) {
        if (s[close + 2 + i] == ' ' || s[close + 2 + i] == '\t') {
            ul = i;
            break;
        }
    }
    *url_len = ul;
    *next = pclose + 1;
    return 1;
}

/* 追加一个 span（文本拷入池） */
static void emit_span(SpanVec *out, P *p, const char *t, int len, Style st) {
    if (len <= 0) return;
    Span sp;
    memset(&sp, 0, sizeof(sp));
    sp.text = pool_put(&p->pool, t, len);
    sp.len = len;
    sp.bold = st.bold;
    sp.italic = st.italic;
    sp.strike = st.strike;
    sp.link = st.link;
    sp.link_len = st.link_len;
    SpanVec_push(out, sp);
}

static void scan_inline(P *p, const char *s, int start, int end, Style st, SpanVec *out) {
    int plain = start;
    int i = start;

#define FLUSH()                                                  \
    do {                                                         \
        if (i > plain) emit_span(out, p, s + plain, i - plain, st); \
        plain = i;                                               \
    } while (0)

    while (i < end) {
        char c = s[i];

        if (c == '\\' && i + 1 < end) {
            i += 2;
            continue;
        }

        /* 行内代码：优先级最高，内部不再解析任何标记 */
        if (c == '`') {
            int open_run = run_len(s, i, end, '`'); /* 开 run 长度（1 或更多） */
            int close = find_backtick_run_n(s, i + open_run, end, open_run);
            if (close >= 0) {
                FLUSH();
                Span sp;
                memset(&sp, 0, sizeof(sp));
                sp.text = pool_put(&p->pool, s + i + open_run, close - i - open_run);
                sp.len = close - i - open_run;
                sp.code = 1;
                sp.link = st.link;
                sp.link_len = st.link_len;
                SpanVec_push(out, sp);
                i = close + open_run;
                plain = i;
                continue;
            }
        }

        /* 图片 ![alt](url) → "📷 alt" */
        if (c == '!' && i + 1 < end && s[i + 1] == '[') {
            int to, tl, uo, ul, nx;
            if (parse_link_syntax(s, i + 1, end, &to, &tl, &uo, &ul, &nx)) {
                FLUSH();
                static const char cam[] = "\xF0\x9F\x93\xB7 "; /* 📷 + 空格 */
                int pre = (int)sizeof(cam) - 1;
                char *tmp = (char *)malloc((size_t)(pre + tl) + 1);
                if (tmp) {
                    memcpy(tmp, cam, (size_t)pre);
                    memcpy(tmp + pre, s + to, (size_t)tl);
                    Span sp;
                    memset(&sp, 0, sizeof(sp));
                    sp.text = pool_put(&p->pool, tmp, pre + tl);
                    sp.len = pre + tl;
                    sp.link = pool_put(&p->pool, s + uo, ul);
                    sp.link_len = ul;
                    SpanVec_push(out, sp);
                    free(tmp);
                }
                i = nx;
                plain = i;
                continue;
            }
        }

        /* 链接 [text](url) */
        if (c == '[') {
            int to, tl, uo, ul, nx;
            if (parse_link_syntax(s, i, end, &to, &tl, &uo, &ul, &nx)) {
                FLUSH();
                Style ns = st;
                ns.link = pool_put(&p->pool, s + uo, ul);
                ns.link_len = ul;
                scan_inline(p, s, to, to + tl, ns, out);
                i = nx;
                plain = i;
                continue;
            }
        }

        /* 自动链接 <http://...> */
        if (c == '<') {
            int gt = find_char(s, i + 1, end, '>');
            if (gt >= 0) {
                int ilen = gt - i - 1, has = 0;
                for (int k = 0; k < ilen; k++)
                    if (s[i + 1 + k] == ':' || s[i + 1 + k] == '@') has = 1;
                if (has) {
                    FLUSH();
                    const char *u = pool_put(&p->pool, s + i + 1, ilen);
                    Span sp;
                    memset(&sp, 0, sizeof(sp));
                    sp.text = u;
                    sp.len = ilen;
                    sp.link = u;
                    sp.link_len = ilen;
                    SpanVec_push(out, sp);
                    i = gt + 1;
                    plain = i;
                    continue;
                }
            }
        }

        /* 删除线 ~~x~~ */
        if (c == '~' && i + 1 < end && s[i + 1] == '~') {
            int close = find_seq(s, i + 2, end, "~~", 2);
            if (close >= 0) {
                FLUSH();
                Style ns = st;
                ns.strike = 1;
                scan_inline(p, s, i + 2, close, ns, out);
                i = close + 2;
                plain = i;
                continue;
            }
        }

        /* 粗斜体 *** / ___（必须先于 ** 判断，否则会被 ** 吃掉前两个星号） */
        if ((c == '*' || c == '_') && i + 2 < end && s[i + 1] == c && s[i + 2] == c) {
            char seq[3] = {c, c, c};
            int close = find_seq(s, i + 3, end, seq, 3);
            if (close >= 0) {
                FLUSH();
                Style ns = st;
                ns.bold = 1;
                ns.italic = 1;
                scan_inline(p, s, i + 3, close, ns, out);
                i = close + 3;
                plain = i;
                continue;
            }
        }

        /* 粗体 ** / __ */
        if ((c == '*' || c == '_') && i + 1 < end && s[i + 1] == c) {
            char seq[2] = {c, c};
            int close = find_seq(s, i + 2, end, seq, 2);
            if (close >= 0) {
                FLUSH();
                Style ns = st;
                ns.bold = 1;
                scan_inline(p, s, i + 2, close, ns, out);
                i = close + 2;
                plain = i;
                continue;
            }
        }

        /* 斜体 * / _ */
        if (c == '*' || c == '_') {
            int close = find_emph_close(s, i + 1, end, c);
            if (close >= 0) {
                FLUSH();
                Style ns = st;
                ns.italic = 1;
                scan_inline(p, s, i + 1, close, ns, out);
                i = close + 1;
                plain = i;
                continue;
            }
        }

        i++;
    }
    FLUSH();
#undef FLUSH
}

/* 合并相邻同样式 span；文本以 \n 结尾说明是硬换行，不与后续合并 */
static void merge_spans(SpanVec *v, P *p) {
    if (v->n < 2) return;
    int w = 0;
    for (int k = 0; k < v->n; k++) {
        Span s = v->d[k];
        if (w > 0 && s.len > 0 && s.text[s.len - 1] != '\n') {
            Span *last = &v->d[w - 1];
            if (last->len > 0 && last->text[last->len - 1] != '\n' && last->bold == s.bold &&
                last->italic == s.italic && last->strike == s.strike && last->code == s.code &&
                last->link == s.link && last->color == s.color) {
                char *nt = (char *)malloc((size_t)(last->len + s.len) + 1);
                if (nt) {
                    memcpy(nt, last->text, (size_t)last->len);
                    memcpy(nt + last->len, s.text, (size_t)s.len);
                    nt[last->len + s.len] = '\0';
                    const char *merged = pool_put(&p->pool, nt, last->len + s.len);
                    if (merged) {
                        last->text = merged;
                        last->len += s.len;
                    }
                    free(nt);
                }
                continue;
            }
        }
        v->d[w++] = s;
    }
    v->n = w;
}

/* 解析行内文本填入 block */
static void add_inline(P *p, Block *b, const char *s, int len) {
    SpanVec v = {0};
    Style st = {0};
    scan_inline(p, s, 0, len, st, &v);
    merge_spans(&v, p);
    b->spans = v.d;
    b->nspan = v.n;
}

/* 供外部（测试）使用的行内解析入口 */
Span *md_parse_inline(const char *src, int len, int *out_n) {
    P p;
    memset(&p, 0, sizeof(p));
    pool_put(&p.pool, src, len);
    SpanVec v = {0};
    Style st = {0};
    scan_inline(&p, src, 0, len, st, &v);
    merge_spans(&v, &p);
    /* 文本指向 p.pool，调用方无法释放 → 复制出来自持 */
    Span *out = NULL;
    if (v.n > 0) {
        out = (Span *)calloc((size_t)v.n, sizeof(Span));
        for (int i = 0; i < v.n; i++) {
            out[i] = v.d[i];
            char *t = (char *)malloc((size_t)v.d[i].len + 1);
            memcpy(t, v.d[i].text, (size_t)v.d[i].len);
            t[v.d[i].len] = '\0';
            out[i].text = t;
            if (v.d[i].link) {
                char *l = (char *)malloc((size_t)v.d[i].link_len + 1);
                memcpy(l, v.d[i].link, (size_t)v.d[i].link_len);
                l[v.d[i].link_len] = '\0';
                out[i].link = l;
            }
        }
    }
    *out_n = v.n;
    free(v.d);
    pool_free(&p.pool);
    return out;
}

void md_spans_free(Span *s) {
    if (!s) return;
    /* 需知道 n 才能 free 每个 text；测试里统一用 md_spans_free_n */
    free(s);
}

void md_spans_free_n(Span *s, int n) {
    if (!s) return;
    for (int i = 0; i < n; i++) {
        free((void *)s[i].text);
        free((void *)s[i].link);
    }
    free(s);
}

/* ---------------------------------------------------------------- 块级判定 */

static int is_blank(const char *s, int n) {
    for (int i = 0; i < n; i++)
        if (s[i] != ' ' && s[i] != '\t') return 0;
    return 1;
}

static int indent_of(const char *s, int n) {
    int k = 0;
    for (int i = 0; i < n; i++) {
        if (s[i] == ' ') k += 1;
        else if (s[i] == '\t') k += 4;
        else break;
    }
    return k;
}

static void trim(const char **s, int *n) {
    while (*n > 0 && ((*s)[*n - 1] == ' ' || (*s)[*n - 1] == '\t' || (*s)[*n - 1] == '\r'))
        (*n)--;
    while (*n > 0 && ((*s)[0] == ' ' || (*s)[0] == '\t')) {
        (*s)++;
        (*n)--;
    }
}

static int is_hr(const char *t, int n) {
    int cnt = 0;
    char c0 = 0;
    for (int i = 0; i < n; i++) {
        char c = t[i];
        if (c == ' ' || c == '\t') continue;
        if (!c0) c0 = c;
        if (c != c0) return 0;
        if (c != '-' && c != '*' && c != '_') return 0;
        cnt++;
    }
    return c0 && cnt >= 3;
}

/* ATX 标题：返回级别（0 表示不是），正文偏移/长度写入参数 */
static int parse_atx(const char *t, int n, int *body_off, int *body_len) {
    if (n == 0 || t[0] != '#') return 0;
    int h = run_len(t, 0, n, '#');
    if (h > 6) return 0;
    int i = h;
    if (i < n && t[i] != ' ' && t[i] != '\t') return 0;
    while (i < n && (t[i] == ' ' || t[i] == '\t')) i++;
    int end = n;
    while (end > i && (t[end - 1] == ' ' || t[end - 1] == '\t')) end--;
    while (end > i && t[end - 1] == '#') end--;
    while (end > i && (t[end - 1] == ' ' || t[end - 1] == '\t')) end--;
    *body_off = i;
    *body_len = end - i;
    return h;
}

/* 围栏：返回围栏字符（0 表示不是） */
static char parse_fence(const char *t, int n) {
    if (n >= 3 && (t[0] == '`' || t[0] == '~') && t[0] == t[1] && t[1] == t[2]) return t[0];
    return 0;
}

/* 列表标记：返回 1 并输出正文偏移、有序标志、序号 */
static int parse_list_marker(const char *t, int n, int *rest, int *ordered, int *number) {
    if (n == 0) return 0;
    if (t[0] == '-' || t[0] == '*' || t[0] == '+') {
        if (n >= 2 && (t[1] == ' ' || t[1] == '\t')) {
            int i = 1;
            while (i < n && (t[i] == ' ' || t[i] == '\t')) i++;
            *rest = i;
            *ordered = 0;
            *number = 0;
            return 1;
        }
        return 0;
    }
    int i = 0;
    while (i < n && t[i] >= '0' && t[i] <= '9') i++;
    if (i > 0 && i < n && (t[i] == '.' || t[i] == ')') && i + 1 < n &&
        (t[i + 1] == ' ' || t[i + 1] == '\t')) {
        int num = 0;
        for (int k = 0; k < i; k++) num = num * 10 + (t[k] - '0');
        int j = i + 1;
        while (j < n && (t[j] == ' ' || t[j] == '\t')) j++;
        *rest = j;
        *ordered = 1;
        *number = num ? num : 1;
        return 1;
    }
    return 0;
}

/* 表格行拆分（处理 \| 转义） */
static int split_table_row(const char *s, int n, int *offs, int *lens, int maxc) {
    int i = 0, end = n;
    if (i < end && s[i] == '|') i++;
    while (end > i && (s[end - 1] == ' ' || s[end - 1] == '\t')) end--;
    if (end > i && s[end - 1] == '|') end--;

    int c = 0, start = i;
    for (int k = i; k <= end; k++) {
        if (k == end) {
            if (c < maxc) {
                int cs = start, ce = k;
                while (cs < ce && (s[cs] == ' ' || s[cs] == '\t')) cs++;
                while (ce > cs && (s[ce - 1] == ' ' || s[ce - 1] == '\t')) ce--;
                offs[c] = cs;
                lens[c] = ce - cs;
                c++;
            }
            break;
        }
        if (s[k] == '|' && (k == 0 || s[k - 1] != '\\')) {
            if (c < maxc) {
                int cs = start, ce = k;
                while (cs < ce && (s[cs] == ' ' || s[cs] == '\t')) cs++;
                while (ce > cs && (s[ce - 1] == ' ' || s[ce - 1] == '\t')) ce--;
                offs[c] = cs;
                lens[c] = ce - cs;
                c++;
            }
            start = k + 1;
        }
    }
    return c;
}

/* 分隔行：成功返回列数（对齐写入 aligns），失败返回 0 */
static int parse_table_align(const char *s, int n, int *aligns) {
    int offs[MAX_TABLE_COLS], lens[MAX_TABLE_COLS];
    int nc = split_table_row(s, n, offs, lens, MAX_TABLE_COLS);
    if (nc == 0) return 0;
    for (int i = 0; i < nc; i++) {
        int cs = offs[i], cn = lens[i];
        if (cn == 0) return 0;
        int hasdash = 0;
        int left = (s[cs] == ':'), right = (s[cs + cn - 1] == ':');
        for (int k = 0; k < cn; k++) {
            if (s[cs + k] == '-') hasdash = 1;
            else if (s[cs + k] != ':') return 0;
        }
        if (!hasdash) return 0;
        aligns[i] = (left && right) ? ALIGN_CENTER : (right ? ALIGN_RIGHT : ALIGN_LEFT);
    }
    return nc;
}

/* 语言标注归一化：{lang} / {.lang} → 纯语言名。
 * info string 的首 word 才是语言名（CommonMark），所以还要在逗号处截断：
 * ```rust,ignore → "rust" */
static void normalize_lang(const char *info, int n, char *out, int outsz) {
    int i = 0, j = n;
    while (i < j && (info[i] == ' ' || info[i] == '\t')) i++;
    while (j > i && (info[j - 1] == ' ' || info[j - 1] == '\t')) j--;
    if (i < j && info[i] == '{') {
        i++;
        if (i < j && info[i] == '.') i++;
    }
    if (j > i && info[j - 1] == '}') j--;
    if (i < j && info[i] == '.') i++;
    int k = i;
    while (k < j && info[k] != ' ' && info[k] != '\t' && info[k] != ',') k++;
    int len = k - i;
    if (len >= outsz) len = outsz - 1;
    if (len < 0) len = 0;
    memcpy(out, info + i, (size_t)len);
    out[len] = '\0';
}

/* ---------------------------------------------------------------- 块解析 */

static void parse_blocks(P *p, Lines *L, int depth, int from, int to);

/* 新建块（kind 拷入池） */
static Block new_block(P *p, const char *kind) {
    Block b;
    memset(&b, 0, sizeof(b));
    b.kind = pool_put(&p->pool, kind, (int)strlen(kind));
    return b;
}

static void apply_quote(Block *b) {
    b->quote = 1;
    b->indent += DOC_QUOTE_INDENT;
    if (b->color == 0) b->color = COL_QUOTE_TEXT;
}

/* 判断某行是否是块级起始（用于打断段落） */
static int starts_block(const char *t, int n) {
    if (parse_fence(t, n)) return 1;
    if (is_hr(t, n)) return 1;
    int dummy;
    if (parse_atx(t, n, &dummy, &dummy)) return 1;
    if (n > 0 && t[0] == '>') return 1;
    if (n > 0 && t[0] == '|') return 1;
    int a, b, c;
    if (parse_list_marker(t, n, &a, &b, &c)) return 1;
    return 0;
}

static void parse_paragraph(P *p, Lines *L, int depth, int *idx, int to) {
    /* 收集直到空行或下一个块级起始 */
    int start = *idx;
    int end = start;
    while (end < to) {
        const char *s;
        int n;
        line_at(L, end, &s, &n);
        if (is_blank(s, n)) break;
        const char *t = s;
        int tn = n;
        trim(&t, &tn);
        if (end > start && starts_block(t, tn)) break;
        end++;
    }
    if (end == start) {
        *idx = start + 1;
        return;
    }
    /* 逐行拼进池（用 \n 连接），再统一做行内解析 */
    int total = 0;
    for (int k = start; k < end; k++) {
        const char *s;
        int n;
        line_at(L, k, &s, &n);
        const char *t = s;
        int tn = n;
        trim(&t, &tn);
        total += tn + 1;
    }
    char *buf = (char *)malloc((size_t)total + 1);
    int bi = 0;
    for (int k = start; k < end; k++) {
        const char *s;
        int n;
        line_at(L, k, &s, &n);
        const char *t = s;
        int tn = n;
        trim(&t, &tn);
        memcpy(buf + bi, t, (size_t)tn);
        bi += tn;
        if (k + 1 < end) buf[bi++] = '\n';
    }
    buf[bi] = '\0';

    Block b = new_block(p, "p");
    add_inline(p, &b, buf, bi);
    free(buf);
    BlockVec_push(&p->out, b);
    *idx = end;
}

static void parse_fenced_code(P *p, Lines *L, int *idx, int to) {
    const char *s;
    int n;
    int cur = *idx;
    line_at(L, cur, &s, &n);
    const char *t = s;
    int tn = n;
    trim(&t, &tn);
    char fc = parse_fence(t, tn);
    char lang[32] = {0};
    int info_off = 3;
    while (info_off < tn && (t[info_off] == ' ' || t[info_off] == '\t')) info_off++;
    normalize_lang(t + info_off, tn - info_off, lang, (int)sizeof(lang));

    cur++;
    /* 先数行 */
    int cstart = cur;
    while (cur < to) {
        line_at(L, cur, &s, &n);
        const char *tt = s;
        int ttn = n;
        trim(&tt, &ttn);
        if (parse_fence(tt, ttn) == fc) break;
        cur++;
    }
    int cend = cur;
    if (cur < to) cur++; /* 吃掉收尾围栏 */

    Block b = new_block(p, "code");
    int nlines = cend - cstart;
    if (nlines > 0) {
        b.lines = (const char **)calloc((size_t)nlines, sizeof(char *));
        b.line_lens = (int *)calloc((size_t)nlines, sizeof(int));
        for (int k = 0; k < nlines; k++) {
            line_at(L, cstart + k, &s, &n);
            const char *tt = s;
            int ttn = n;
            trim(&tt, &ttn);
            b.lines[k] = pool_put(&p->pool, tt, ttn);
            b.line_lens[k] = ttn;
        }
        b.nline = nlines;
    }
    if (lang[0]) {
        b.lang = (char *)malloc(strlen(lang) + 1);
        strcpy(b.lang, lang);
    }
    BlockVec_push(&p->out, b);
    *idx = cur;
}

static void parse_indented_code(P *p, Lines *L, int *idx, int to) {
    int cur = *idx;
    int cstart = cur;
    while (cur < to) {
        const char *s;
        int n;
        line_at(L, cur, &s, &n);
        if (is_blank(s, n)) {
            /* 后面若仍是缩进代码则空行属于代码块 */
            int j = cur + 1;
            while (j < to) {
                const char *s2;
                int n2;
                line_at(L, j, &s2, &n2);
                if (!is_blank(s2, n2)) break;
                j++;
            }
            if (j < to) {
                const char *s3;
                int n3;
                line_at(L, j, &s3, &n3);
                if (indent_of(s3, n3) >= 4) {
                    cur = j;
                    continue;
                }
            }
            break;
        }
        if (indent_of(s, n) < 4) break;
        cur++;
    }
    int cend = cur;
    if (cend <= cstart) {
        *idx = cstart + 1;
        return;
    }
    Block b = new_block(p, "code");
    int nlines = cend - cstart;
    b.lines = (const char **)calloc((size_t)nlines, sizeof(char *));
    b.line_lens = (int *)calloc((size_t)nlines, sizeof(int));
    for (int k = 0; k < nlines; k++) {
        const char *s;
        int n;
        line_at(L, cstart + k, &s, &n);
        int ind = indent_of(s, n);
        b.lines[k] = pool_put(&p->pool, s + ind, n - ind);
        b.line_lens[k] = n - ind;
    }
    b.nline = nlines;
    BlockVec_push(&p->out, b);
    *idx = cend;
}

static void parse_quote(P *p, Lines *L, int *idx, int to) {
    int cur = *idx;
    /* 抽取连续的 > 行到临时缓冲 */
    char *buf = NULL;
    int blen = 0, bcap = 0;
    while (cur < to) {
        const char *s;
        int n;
        line_at(L, cur, &s, &n);
        const char *t = s;
        int tn = n;
        trim(&t, &tn);
        if (tn > 0 && t[0] == '>') {
            const char *rest = t + 1;
            int rlen = tn - 1;
            if (rlen > 0 && rest[0] == ' ') {
                rest++;
                rlen--;
            }
            if (blen + rlen + 2 > bcap) {
                bcap = (bcap ? bcap * 2 : 1024) + rlen + 2;
                buf = (char *)realloc(buf, (size_t)bcap);
            }
            memcpy(buf + blen, rest, (size_t)rlen);
            blen += rlen;
            buf[blen++] = '\n';
            cur++;
        } else if (is_blank(s, n)) {
            break;
        } else {
            /* 懒续行 */
            if (blen + tn + 2 > bcap) {
                bcap = (bcap ? bcap * 2 : 1024) + tn + 2;
                buf = (char *)realloc(buf, (size_t)bcap);
            }
            memcpy(buf + blen, t, (size_t)tn);
            blen += tn;
            buf[blen++] = '\n';
            cur++;
        }
    }
    *idx = cur;
    if (!buf || blen == 0) {
        free(buf);
        return;
    }
    /* 子解析 */
    Lines sub;
    lines_split(&sub, buf, blen);
    int before = p->out.n;
    parse_blocks(p, &sub, 0, 0, sub.nline);
    lines_free(&sub);
    /* 给新产生的块打引用标记 */
    for (int k = before; k < p->out.n; k++) apply_quote(&p->out.d[k]);
    free(buf);
}

static void parse_table(P *p, Lines *L, int *idx, int to) {
    int cur = *idx;
    const char *s;
    int n;
    line_at(L, cur, &s, &n);
    const char *hr = s;
    int hrn = n;
    trim(&hr, &hrn);
    if (hrn == 0 || hr[0] != '|') {
        parse_paragraph(p, L, 0, idx, to);
        return;
    }
    int aligns[MAX_TABLE_COLS];
    const char *sep;
    int sepn;
    line_at(L, cur + 1, &sep, &sepn);
    const char *sept = sep;
    int septn = sepn;
    trim(&sept, &septn);
    int ncol = parse_table_align(sept, septn, aligns);
    if (ncol == 0) {
        parse_paragraph(p, L, 0, idx, to);
        return;
    }

    /* 表头 */
    RowVec rows = {0};
    int hoff[MAX_TABLE_COLS], hlen[MAX_TABLE_COLS];
    int hn = split_table_row(hr, hrn, hoff, hlen, ncol);
    TableRow row;
    memset(&row, 0, sizeof(row));
    row.header = 1;
    row.cells = (TableCell *)calloc((size_t)ncol, sizeof(TableCell));
    row.ncell = ncol;
    for (int c = 0; c < ncol; c++) {
        int off = (c < hn) ? hoff[c] : 0;
        int ln = (c < hn) ? hlen[c] : 0;
        row.cells[c].align = aligns[c];
        /* 表头加粗 */
        SpanVec v = {0};
        Style st = {0};
        st.bold = 1;
        scan_inline(p, hr + off, 0, ln, st, &v);
        merge_spans(&v, p);
        row.cells[c].spans = v.d;
        row.cells[c].nspan = v.n;
    }
    RowVec_push(&rows, row);
    cur += 2;

    /* 表体 */
    while (cur < to) {
        line_at(L, cur, &s, &n);
        const char *t = s;
        int tn = n;
        trim(&t, &tn);
        if (tn == 0 || t[0] != '|') break;
        int off[MAX_TABLE_COLS], len[MAX_TABLE_COLS];
        int nc = split_table_row(t, tn, off, len, ncol);
        TableRow r2;
        memset(&r2, 0, sizeof(r2));
        r2.cells = (TableCell *)calloc((size_t)ncol, sizeof(TableCell));
        r2.ncell = ncol;
        for (int c = 0; c < ncol; c++) {
            int o = (c < nc) ? off[c] : 0;
            int l = (c < nc) ? len[c] : 0;
            r2.cells[c].align = aligns[c];
            SpanVec v = {0};
            Style st = {0};
            scan_inline(p, t + o, 0, l, st, &v);
            merge_spans(&v, p);
            r2.cells[c].spans = v.d;
            r2.cells[c].nspan = v.n;
        }
        RowVec_push(&rows, r2);
        cur++;
    }
    *idx = cur;

    TableData *td = (TableData *)calloc(1, sizeof(TableData));
    td->cols = ncol;
    td->col_x = (i32 *)calloc((size_t)ncol, sizeof(i32));
    td->col_w = (i32 *)calloc((size_t)ncol, sizeof(i32));
    td->rows = rows.d;
    td->nrow = rows.n;

    Block b = new_block(p, "table");
    b.table = td;
    BlockVec_push(&p->out, b);
}

static void parse_list(P *p, Lines *L, int depth, int *idx, int to) {
    int cur = *idx;
    int number = 0;
    int have_number = 0;

    while (cur < to) {
        const char *s;
        int n;
        line_at(L, cur, &s, &n);
        if (is_blank(s, n)) {
            cur++;
            continue;
        }
        const char *t = s;
        int tn = n;
        trim(&t, &tn);
        int rest, ordered, num;
        if (!parse_list_marker(t, tn, &rest, &ordered, &num)) break;

        if (ordered) {
            number = have_number ? number + 1 : num;
            have_number = 1;
        }

        /* 收集本项的所有行：首行正文 + 缩进续行
         * iptr 存指针（不是偏移）——子解析时 L->src 可能指向池内任意块，
         * 偏移没有统一基准。 */
        int marker_w = 2; /* 标记 + 空格 */
        const char **iptr = NULL;
        int *ilen = NULL, icap = 0, icount = 0;
        const char *ifirst = t + rest;
        int ifirstlen = tn - rest;
        cur++;

        int pending_blank = 0;
        while (cur < to) {
            line_at(L, cur, &s, &n);
            if (is_blank(s, n)) {
                pending_blank++;
                cur++;
                continue;
            }
            int ind = indent_of(s, n);
            if (ind >= marker_w) {
                for (int b = 0; b < pending_blank; b++) {
                    if (icount == icap) {
                        icap = icap ? icap * 2 : 8;
                        iptr = (const char **)realloc((void *)iptr, (size_t)icap * sizeof(char *));
                        ilen = (int *)realloc(ilen, (size_t)icap * sizeof(int));
                    }
                    iptr[icount] = NULL;
                    ilen[icount] = 0;
                    icount++;
                }
                pending_blank = 0;
                int skip = marker_w < ind ? marker_w : ind;
                if (icount == icap) {
                    icap = icap ? icap * 2 : 8;
                    iptr = (const char **)realloc((void *)iptr, (size_t)icap * sizeof(char *));
                    ilen = (int *)realloc(ilen, (size_t)icap * sizeof(int));
                }
                iptr[icount] = s + skip;
                ilen[icount] = n - skip;
                icount++;
                cur++;
            } else {
                break;
            }
        }
        cur -= pending_blank;

        /* 构造子 Lines：首行内容单独放进池（因为要剥掉任务标记） */
        char *firstbuf = (char *)malloc((size_t)ifirstlen + 1);
        memcpy(firstbuf, ifirst, (size_t)ifirstlen);
        firstbuf[ifirstlen] = '\0';
        int firstlen = ifirstlen;
        int first_is_blank = is_blank(ifirst, ifirstlen);

        /* 任务列表检测 */
        int task = 0, checked = 0;
        if (firstlen >= 3 && firstbuf[0] == '[' && firstbuf[2] == ']') {
            char c1 = firstbuf[1];
            if (c1 == ' ' || c1 == 'x' || c1 == 'X') {
                task = 1;
                checked = (c1 != ' ');
                int k = 3;
                while (k < firstlen && (firstbuf[k] == ' ' || firstbuf[k] == '\t')) k++;
                memmove(firstbuf, firstbuf + k, (size_t)(firstlen - k));
                firstlen -= k;
                firstbuf[firstlen] = '\0';
            }
        }

        /* 标记文本 */
        char marker_buf[16];
        const char *marker;
        int marker_len;
        if (task) {
            marker = checked ? "\xE2\x98\x91" : "\xE2\x98\x90"; /* ☑ / ☐ */
            marker_len = 3;
        } else if (ordered) {
            _snprintf_s(marker_buf, sizeof(marker_buf), _TRUNCATE, "%d.", number);
            marker = marker_buf;
            marker_len = (int)strlen(marker_buf);
        } else if (depth >= 2) {
            marker = "\xE2\x96\xAA"; /* ▪ */
            marker_len = 3;
        } else if (depth >= 1) {
            marker = "\xE2\x97\xA6"; /* ◦ */
            marker_len = 3;
        } else {
            marker = "\xE2\x80\xA2"; /* • */
            marker_len = 3;
        }

        i32 base_indent = depth * DOC_LIST_INDENT_STEP + DOC_LIST_INDENT_STEP;
        i32 marker_x = DOC_LM + depth * DOC_LIST_INDENT_STEP + 4;

        /* 组装子源码为一段连续缓冲：首行（已剥任务标记）+ 续行 */
        int subn = 0;
        int subcap = icount + 2;
        const char **sublines = (const char **)calloc((size_t)subcap, sizeof(char *));
        int *sublens = (int *)calloc((size_t)subcap, sizeof(int));
        if (!first_is_blank || firstlen > 0) {
            sublines[subn] = firstbuf;
            sublens[subn] = firstlen;
            subn++;
        } else {
            free(firstbuf);
        }
        for (int k = 0; k < icount; k++) {
            sublines[subn] = iptr[k]; /* NULL 表示空行 */
            sublens[subn] = ilen[k];
            subn++;
        }

        int total = 0;
        for (int k = 0; k < subn; k++) total += sublens[k] + 1;
        char *cbuf = (char *)malloc((size_t)total + 1);
        int ci = 0;
        for (int k = 0; k < subn; k++) {
            if (sublines[k]) memcpy(cbuf + ci, sublines[k], (size_t)sublens[k]);
            cbuf[ci + sublens[k]] = '\n';
            ci += sublens[k] + 1;
        }
        cbuf[ci] = '\0';
        free((void *)sublines);
        free(sublens);
        free((void *)iptr);
        free(ilen);

        Lines sub;
        lines_split(&sub, cbuf, ci);

        int before = p->out.n;
        parse_blocks(p, &sub, depth + 1, 0, sub.nline);
        lines_free(&sub);
        free(cbuf);

        if (p->out.n == before) {
            /* 空项也要有一行，否则标记不显示 */
            Block b = new_block(p, "p");
            b.marker = pool_put(&p->pool, marker, marker_len);
            b.marker_len = marker_len;
            b.marker_x = marker_x;
            b.indent = base_indent;
            BlockVec_push(&p->out, b);
        } else {
            /* 标记只加在首块 */
            Block *b0 = &p->out.d[before];
            b0->marker = pool_put(&p->pool, marker, marker_len);
            b0->marker_len = marker_len;
            b0->marker_x = marker_x;
            b0->indent = base_indent;
            /* 后续兄弟块继承缩进（子列表已在自己的 depth 处理，这里只补齐） */
            for (int k = before; k < p->out.n; k++) {
                if (p->out.d[k].indent == 0) p->out.d[k].indent = base_indent;
            }
        }
    }
    *idx = cur;
}

static void parse_blocks(P *p, Lines *L, int depth, int from, int to) {
    int i = from;
    while (i < to) {
        const char *s;
        int n;
        line_at(L, i, &s, &n);
        if (is_blank(s, n)) {
            i++;
            continue;
        }
        const char *t = s;
        int tn = n;
        trim(&t, &tn);

        /* 缩进代码块（顶层或列表项内） */
        if (indent_of(s, n) >= 4 && !parse_fence(t, tn)) {
            parse_indented_code(p, L, &i, to);
            continue;
        }

        /* 围栏代码块 */
        if (parse_fence(t, tn)) {
            parse_fenced_code(p, L, &i, to);
            continue;
        }

        /* 分隔线 */
        if (is_hr(t, tn)) {
            Block b = new_block(p, "hr");
            BlockVec_push(&p->out, b);
            i++;
            continue;
        }

        /* 引用 */
        if (tn > 0 && t[0] == '>') {
            parse_quote(p, L, &i, to);
            continue;
        }

        /* 列表 */
        {
            int rest, ordered, num;
            if (parse_list_marker(t, tn, &rest, &ordered, &num)) {
                parse_list(p, L, depth, &i, to);
                continue;
            }
        }

        /* 表格 */
        if (tn > 0 && t[0] == '|') {
            parse_table(p, L, &i, to);
            continue;
        }

        /* ATX 标题 */
        {
            int bo, bl;
            int h = parse_atx(t, tn, &bo, &bl);
            if (h) {
                char kind[4] = {'h', (char)('0' + h), 0, 0};
                Block b = new_block(p, kind);
                add_inline(p, &b, t + bo, bl);
                BlockVec_push(&p->out, b);
                i++;
                continue;
            }
        }

        /* Setext 标题：下一行是 === / --- */
        if (i + 1 < to) {
            const char *ns;
            int nn;
            line_at(L, i + 1, &ns, &nn);
            const char *nt = ns;
            int ntn = nn;
            trim(&nt, &ntn);
            int level = 0;
            if (ntn > 0) {
                if (nt[0] == '=') {
                    int k = 0;
                    while (k < ntn && nt[k] == '=') k++;
                    if (k == ntn) level = 1;
                } else if (ntn >= 2 && nt[0] == '-') {
                    int k = 0;
                    while (k < ntn && nt[k] == '-') k++;
                    if (k == ntn) level = 2;
                }
            }
            if (level) {
                char kind[4] = {'h', (char)('0' + level), 0, 0};
                Block b = new_block(p, kind);
                add_inline(p, &b, t, tn);
                BlockVec_push(&p->out, b);
                i += 2;
                continue;
            }
        }

        /* 段落 */
        parse_paragraph(p, L, depth, &i, to);
    }
}

/* ------------------------------------------------------------------ 入口/释放 */

Doc *md_parse(const char *src, const char *path) {
    P p;
    memset(&p, 0, sizeof(p));

    /* 归一化换行：CRLF/CR → LF */
    int slen = u8len(src);
    char *norm = (char *)malloc((size_t)slen + 2);
    int ni = 0;
    for (int i = 0; i < slen; i++) {
        if (src[i] == '\r') {
            if (i + 1 < slen && src[i + 1] == '\n') continue;
            norm[ni++] = '\n';
        } else {
            norm[ni++] = src[i];
        }
    }
    norm[ni] = '\0';

    const char *pooled = pool_put(&p.pool, norm, ni);
    free(norm);

    Lines L;
    lines_split(&L, pooled, ni);
    parse_blocks(&p, &L, 0, 0, L.nline);
    lines_free(&L);

    Doc *d = (Doc *)calloc(1, sizeof(Doc));
    d->src = pooled;
    d->src_len = ni;
    /* 用字节安全的 u8dup 而不是 _strdup：MSVCRT 的 _strdup 走 ANSI 语义，
     * 中文路径（UTF-8）会被按 CP936 转换而损坏。 */
    d->path = u8dup(path ? path : "");
    d->blocks = p.out.d;
    d->nblock = p.out.n;
    /* 布局阶段字段留空，等 render 填充 */
    d->__pool = p.pool; /* pool 所有权移交给 Doc，md_doc_free 统一回收 */
    return d;
}

void md_doc_free(Doc *d) {
    if (!d) return;
    for (int i = 0; i < d->nblock; i++) {
        Block *b = &d->blocks[i];
        free(b->spans);
        if (b->lines) {
            for (int k = 0; k < b->nline; k++) {
                /* lines 指向池，不单独 free */
            }
            free(b->lines);
            free(b->line_lens);
        }
        free(b->lang);
        if (b->table) {
            for (int r = 0; r < b->table->nrow; r++) {
                for (int c = 0; c < b->table->rows[r].ncell; c++)
                    free(b->table->rows[r].cells[c].spans);
                free(b->table->rows[r].cells);
            }
            free(b->table->rows);
            free(b->table->col_x);
            free(b->table->col_w);
            free(b->table);
        }
    }
    free(d->blocks);
    free(d->lines);
    free(d->link_rects);
    free(d->path);
    pool_free(&d->__pool);
    pool_free(&d->__lpool);
    free(d);
}
