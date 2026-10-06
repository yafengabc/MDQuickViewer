/* GDI 渲染实现。排版 + 自绘 + 双缓冲。 */
#include "render.h"
#include "highlight.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ 常量 */

#define COL_TEXT COLR(0x222222)
#define COL_HEAD COLR(0x111111)
#define COL_LINK COLR(0x0B57D0)
#define COL_CODE COLR(0x333333)
#define COL_CODE_BG COLR(0xF4F4F4)
#define COL_QUOTE COLR(0x666666)
#define COL_QUOTE_BAR COLR(0xC8C8C8)
#define COL_HR COLR(0xDDDDDD)
#define COL_SEL COLR(0xEAF2FB)
#define COL_BG COLR(0xFFFFFF)
#define COL_TBL_HEAD_BG COLR(0xF2F4F7)

#define COL_CODE_INLINE COLR(0xA40E26)
#define COL_CODE_INLINE_BG COLR(0xF2F3F5)
#define COL_HEAD_ACCENT COLR(0x0B4F9E)
#define COL_TASK_DONE COLR(0x1A7F37)
#define COL_TASK_TODO COLR(0x8C8C8C)
#define COL_TABLE_HEAD_TEXT COLR(0x1F2328)

/* ------------------------------------------------------------------ 负值防护 */

/* 夹到非负。这是全文件唯一的"负数入口"，所有几何计算出口都过这里。 */
static int nonneg(int v) { return v < 0 ? 0 : v; }

/* 安全矩形：构造前把四个分量都夹到非负。 */
static RECT safe_rect(int l, int t, int r, int b) {
    RECT rc;
    rc.left = nonneg(l);
    rc.top = nonneg(t);
    rc.right = nonneg(r);
    rc.bottom = nonneg(b);
    return rc;
}

/* ------------------------------------------------------------------ 缩放 */

static float g_scale = 1.0f;
float rd_scale(void) { return g_scale; }
void rd_set_scale(float s) {
    if (s < 0.5f) s = 0.5f;
    if (s > 3.0f) s = 3.0f;
    g_scale = s;
}
static int eff_px(int base) { return (int)((float)base * g_scale + 0.5f); }

/* ------------------------------------------------------------------ 字体缓存 */

typedef struct {
    wchar_t family[64];
    int px;
    int bold, italic;
    HFONT font;
} FontKey;

#define FONT_CACHE_MAX 128
static FontKey g_fonts[FONT_CACHE_MAX];
static int g_font_n = 0;

static HFONT get_font(const char *family, int px, int bold, int italic) {
    wchar_t wf[64];
    MultiByteToWideChar(CP_UTF8, 0, family, -1, wf, 64);
    for (int i = 0; i < g_font_n; i++) {
        if (g_fonts[i].px == px && g_fonts[i].bold == bold && g_fonts[i].italic == italic &&
            wcscmp(g_fonts[i].family, wf) == 0)
            return g_fonts[i].font;
    }
    if (g_font_n >= FONT_CACHE_MAX) {
        /* 缓存满：清掉最旧的一条再新建（实际用不到这么多） */
        DeleteObject(g_fonts[0].font);
        memmove(&g_fonts[0], &g_fonts[1], sizeof(FontKey) * (FONT_CACHE_MAX - 1));
        g_font_n--;
    }
    LOGFONTW lf;
    ZeroMemory(&lf, sizeof(lf));
    lf.lfHeight = -px; /* 负值 = 字符高度 */
    lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
    lf.lfItalic = italic ? TRUE : FALSE;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, 64, wf);
    HFONT f = CreateFontIndirectW(&lf);
    if (!f) return (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    FontKey *k = &g_fonts[g_font_n++];
    wcscpy_s(k->family, 64, wf);
    k->px = px;
    k->bold = bold;
    k->italic = italic;
    k->font = f;
    return f;
}

void rd_font_cleanup(void) {
    for (int i = 0; i < g_font_n; i++) {
        if (g_fonts[i].font) DeleteObject(g_fonts[i].font);
    }
    g_font_n = 0;
}

/* ------------------------------------------------------------------ 测量 */

static HDC g_meas_dc = NULL;

static HDC meas_dc(void) {
    if (!g_meas_dc) g_meas_dc = CreateCompatibleDC(NULL);
    return g_meas_dc;
}

/* 文本宽度（像素）。
 * 注意：必须用 -1 长度（不带结尾 NUL），否则 NUL 的字形宽会算进去。 */
int rd_measure_text(const char *s, int len, const char *family, int px, int bold, int italic) {
    if (!s || len <= 0) return 0;
    HDC dc = meas_dc();
    if (!dc) return 0;
    wchar_t *w = (wchar_t *)LocalAlloc(LMEM_FIXED, ((size_t)len + 1) * sizeof(wchar_t));
    if (!w) return 0;
    int wl = MultiByteToWideChar(CP_UTF8, 0, s, len, w, len + 1);
    if (wl <= 0) {
        LocalFree(w);
        return 0;
    }
    w[wl] = 0;
    HFONT f = get_font(family, px, bold, italic);
    HGDIOBJ old = SelectObject(dc, f);
    SIZE sz;
    GetTextExtentPoint32W(dc, w, wl, &sz);
    SelectObject(dc, old);
    LocalFree(w);
    return (int)sz.cx;
}

static int measure_item(const RunItem *it, const char *text, int len) {
    return rd_measure_text(text, len, it->family, it->px, it->bold, it->italic);
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

VEC(DrawUnit, DUVec)
VEC(VisualLine, LnVec)
VEC(LinkRect, LrVec)

/* ------------------------------------------------------------------ 排版 */

static void layout_clear(Doc *d) {
    for (int i = 0; i < d->nline; i++) free(d->lines[i].units);
    free(d->lines);
    d->lines = NULL;
    d->nline = 0;
    free(d->link_rects);
    d->link_rects = NULL;
    d->nlink = 0;
    d->content_height = 0;
    /* 合并片段的文本挂在布局池上，池必须与 lines 同寿命 */
    pool_free(&d->__lpool);
}

static int block_gap(const char *kind) {
    if (!kind) return 10;
    if (strcmp(kind, "h1") == 0) return 20;
    if (strcmp(kind, "h2") == 0) return 16;
    if (strcmp(kind, "h3") == 0) return 14;
    if (strcmp(kind, "h4") == 0 || strcmp(kind, "h5") == 0 || strcmp(kind, "h6") == 0) return 12;
    if (strcmp(kind, "hr") == 0) return 14;
    if (strcmp(kind, "code") == 0) return 16;
    return 10;
}

static int heading_px(const char *kind) {
    if (!kind) return eff_px(RD_BODY_PX);
    if (strcmp(kind, "h1") == 0) return eff_px(26);
    if (strcmp(kind, "h2") == 0) return eff_px(22);
    if (strcmp(kind, "h3") == 0) return eff_px(18);
    if (strcmp(kind, "h4") == 0) return eff_px(16);
    if (strcmp(kind, "h5") == 0) return eff_px(15);
    if (strcmp(kind, "h6") == 0) return eff_px(14);
    return eff_px(RD_BODY_PX);
}

/* span → RunItem（带块级样式：标题加粗、引用斜体等） */
typedef struct {
    RunItem *d;
    int n, cap;
} RIVec;

static void ri_push(RIVec *v, RunItem x) {
    if (v->n >= v->cap) {
        v->cap = v->cap ? v->cap * 2 : 16;
        RunItem *nd = (RunItem *)realloc(v->d, (size_t)v->cap * sizeof(RunItem));
        if (!nd) return;
        v->d = nd;
    }
    v->d[v->n++] = x;
}

static void spans_to_items(Block *b, RIVec *out) {
    int px = heading_px(b->kind);
    int is_p = (strcmp(b->kind, "p") == 0);
    for (int i = 0; i < b->nspan; i++) {
        Span *s = &b->spans[i];
        RunItem it;
        memset(&it, 0, sizeof(it));
        it.text = s->text;
        it.len = s->len;
        it.family = RD_FONT_UI;
        it.px = px;
        it.bold = s->bold;
        it.italic = s->italic;
        it.strike = s->strike;
        it.link = s->link;
        it.link_len = s->link_len;
        it.color = s->color;
        it.underline = (s->link != NULL);

        if (!is_p) it.bold = 1;
        if (strcmp(b->kind, "h6") == 0) it.italic = 1;
        if (strcmp(b->kind, "h1") == 0 || strcmp(b->kind, "h2") == 0) it.color = COL_HEAD_ACCENT;
        if (b->quote) {
            it.italic = 1;
            if (it.color == 0) it.color = COL_QUOTE;
        }
        if (s->code) {
            it.family = RD_FONT_CODE;
            it.px = eff_px(RD_CODE_PX);
            it.code = 1;
            it.bold = 0;
            it.color = COL_CODE_INLINE;
            it.bg = COL_CODE_INLINE_BG;
        }
        if (s->link) {
            it.color = COL_LINK;
            it.underline = 1;
        }
        if (s->color) it.color = s->color;
        ri_push(out, it);
    }
}

/* 表格单元格的 RunItem（表头强制加粗） */
static void cell_items(TableCell *c, int header, RIVec *out) {
    int px = eff_px(RD_BODY_PX);
    for (int i = 0; i < c->nspan; i++) {
        Span *s = &c->spans[i];
        RunItem it;
        memset(&it, 0, sizeof(it));
        it.text = s->text;
        it.len = s->len;
        it.family = s->code ? RD_FONT_CODE : RD_FONT_UI;
        it.px = s->code ? eff_px(RD_CODE_PX) : px;
        it.bold = header ? 1 : s->bold;
        it.italic = s->italic;
        it.strike = s->strike;
        it.code = s->code;
        it.color = s->color ? s->color : (header ? COL_TABLE_HEAD_TEXT : COL_TEXT);
        it.link = s->link;
        it.link_len = s->link_len;
        it.underline = (s->link != NULL);
        it.bg = s->code ? COL_CODE_INLINE_BG : 0;
        ri_push(out, it);
    }
}

/* 是否应作为一段连续绘制（保持字距，避免逐字取整漂移） */
static int same_item(RunItem *a, RunItem *b) {
    return strcmp(a->family, b->family) == 0 && a->px == b->px && a->bold == b->bold &&
           a->italic == b->italic && a->code == b->code && a->strike == b->strike &&
           a->color == b->color && a->bg == b->bg && a->link == b->link &&
           a->underline == b->underline;
}

/* ------------------------------------------------------------------ 折行单元 */

/* 一个折行最小单元（CJK 逐字、拉丁按词、空格单独） */
typedef struct {
    const char *text;
    int len;
    RunItem item;
} Unit;

typedef struct {
    Unit *d;
    int n, cap;
} UVec;

static void u_push(UVec *v, const char *t, int len, RunItem *it) {
    if (v->n >= v->cap) {
        v->cap = v->cap ? v->cap * 2 : 32;
        Unit *nd = (Unit *)realloc(v->d, (size_t)v->cap * sizeof(Unit));
        if (!nd) return;
        v->d = nd;
    }
    v->d[v->n].text = t;
    v->d[v->n].len = len;
    v->d[v->n].item = *it;
    v->n++;
}

/* CJK 判定（与 Go 版 isCJK 同一组区间） */
static int is_cjk(unsigned int cp) {
    return (cp >= 0x3000 && cp <= 0x303F) || (cp >= 0x3040 && cp <= 0x309F) ||
           (cp >= 0x30A0 && cp <= 0x30FF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
           (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xAC00 && cp <= 0xD7AF) ||
           (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFF00 && cp <= 0xFFEF);
}

/* 从 UTF-8 取下一个码点，返回字节数（*cp 存码点）。
 * 掩码必须按序列长度取：直接 *cp = c 会把首字节的高位一起带进结果，
 * 三个字节的中文会算出 0x39xxx 这种垃圾码点，is_cjk 全部判false，
 * 于是所有汉字被当成一个长 Latin 单词，折行和 CJK 判定一起失效。 */
static int utf8_next(const char *s, int len, int i, unsigned int *cp) {
    unsigned char c = (unsigned char)s[i];
    if (c < 0x80) {
        *cp = c;
        return 1;
    }
    int n;
    unsigned int m;
    if ((c & 0xE0) == 0xC0) {
        n = 2;
        m = 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        n = 3;
        m = 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        n = 4;
        m = 0x07;
    } else {
        *cp = c; /* 非法首字节，当作单字节处理，不吞掉后面的内容 */
        return 1;
    }
    if (i + n > len) {
        *cp = c;
        return 1;
    }
    unsigned int v = c & m;
    for (int k = 1; k < n; k++) {
        unsigned char cc = (unsigned char)s[i + k];
        if ((cc & 0xC0) != 0x80) { /* 续字节缺失，截断处理 */
            *cp = c;
            return 1;
        }
        v = (v << 6) | (cc & 0x3F);
    }
    *cp = v;
    return n;
}

/* 空白串 / 非空白串两分，与 Go 版 codeTokens 一致。
 * 不做标点级细分：窄窗口下整段折行即可，逐符号断开反而更难读。 */
static void code_tokens(const char *s, int len, UVec *out, RunItem *it) {
    int i = 0;
    while (i < len) {
        if (s[i] == ' ' || s[i] == '\t') {
            int j = i;
            while (j < len && (s[j] == ' ' || s[j] == '\t')) j++;
            u_push(out, s + i, j - i, it);
            i = j;
            continue;
        }
        int j = i;
        while (j < len && s[j] != ' ' && s[j] != '\t') j++;
        u_push(out, s + i, j - i, it);
        i = j;
    }
}

/* 把一个 RunItem 拆成折行单元 */
static void split_units(RunItem *it, UVec *out) {
    if (it->code) {
        code_tokens(it->text, it->len, out, it);
        return;
    }
    int start = 0;
    int i = 0;
    while (i < it->len) {
        unsigned int cp;
        int n = utf8_next(it->text, it->len, i, &cp);
        if (cp == '\n') {
            if (i > start) u_push(out, it->text + start, i - start, it);
            u_push(out, "\n", 1, it);
            i += n;
            start = i;
            continue;
        }
        if (cp == ' ' || cp == '\t') {
            if (i > start) u_push(out, it->text + start, i - start, it);
            u_push(out, it->text + i, n, it);
            i += n;
            start = i;
            continue;
        }
        if (is_cjk(cp)) {
            if (i > start) u_push(out, it->text + start, i - start, it);
            u_push(out, it->text + i, n, it);
            i += n;
            start = i;
            continue;
        }
        i += n;
    }
    if (i > start) u_push(out, it->text + start, i - start, it);
}

/* ------------------------------------------------------------------ 折行主流程 */

typedef struct {
    Doc *d;
    LnVec lines;
    LrVec links;
} Lay;

/* 合并同类单元的文本需要新分配；这里统一在 wrap 内做 */
/* 拼两段文本成一段连续绘制用的字符串。
 * 结果放进 Doc 的布局池（__lpool），而不是 malloc 后再 free ——
 * DrawUnit.text 会一直指向它直到下一次 layout，局部 malloc 活不下来。 */
static const char *join_text(Doc *d, const char *a, int alen, const char *b, int blen) {
    if (alen <= 0) return b;
    if (blen <= 0) return a;
    char *tmp = (char *)malloc((size_t)(alen + blen) + 1);
    if (!tmp) return a;
    memcpy(tmp, a, (size_t)alen);
    memcpy(tmp + alen, b, (size_t)blen);
    tmp[alen + blen] = '\0';
    const char *r = pool_put(&d->__lpool, tmp, alen + blen);
    free(tmp);
    return r;
}

/* 把单元流折行写入 Lay，返回结束 y。
 * record_links=0 用于表格单元格与代码块（不需要链接热区）。
 * force_lh>0 时用固定行高（代码块行距均匀，不像正文按字号浮动）。 */
static int wrap_units(Lay *L, UVec *uv, int block_idx, Block *b, int x0, int avail, int y0,
                      int record_links, int force_lh) {
    int y = y0;
    int cur_x = x0;
    int line_h = 0;
    int first_idx = -1;
    /* cont 表示"当前正在攒的这一行是上一行的折行续行"。
     * 它与 flush 生命周期解耦：FLUSH 不会清它，只有遇到源码换行才清 0。
     * 早先用 is_wrap 随FLUSH 一起清零，结果硬断出来的第 2..n 段全被当成
     * 新行（复制时吞换行），行高也退化成 0。 */
    int cont = 0;
    int pending_lh = 0; /* 本次 flush 的固定行高；0 = 用 force_lh/line_h */
    DUVec cur = {0};

#define FLUSH()                                                                    \
    do {                                                                           \
        if (cur.n > 0) {                                                           \
            int lh = pending_lh > 0 ? pending_lh                                   \
                                    : (force_lh > 0 ? force_lh : line_h);          \
            VisualLine vl;                                                         \
            memset(&vl, 0, sizeof(vl));                                            \
            vl.block = block_idx;                                                  \
            vl.y = y;                                                              \
            vl.height = nonneg(lh);                                                \
            vl.units = cur.d;                                                       \
            vl.nunit = cur.n;                                                      \
            vl.wrap = (unsigned char)cont;                                         \
            if (first_idx < 0) {                                                    \
                first_idx = L->lines.n;                                            \
                vl.marker = b ? b->marker : NULL;                                  \
                vl.marker_len = b ? b->marker_len : 0;                             \
                vl.marker_x = b ? b->marker_x : 0;                                 \
            }                                                                      \
            LnVec_push(&L->lines, vl);                                             \
            if (record_links) {                                                    \
                for (int k = 0; k < cur.n; k++) {                                  \
                    if (cur.d[k].item.link) {                                       \
                        LinkRect lr;                                               \
                        lr.x = cur.d[k].x;                                          \
                        lr.y = y;                                                  \
                        lr.w = measure_item(&cur.d[k].item, cur.d[k].text,          \
                                            cur.d[k].text_len);                     \
                        lr.h = nonneg(lh);                                         \
                        lr.dest = cur.d[k].item.link;                               \
                        lr.dest_len = cur.d[k].item.link_len;                       \
                        LrVec_push(&L->links, lr);                                  \
                    }                                                              \
                }                                                                  \
            }                                                                      \
            /* 所有权转移：缓冲区交给 vl，工作区必须重新从零开始，          \
             * 否则下一行的 push 会覆盖掉刚转交出去的数组。 */                    \
            cur.d = NULL;                                                          \
            cur.n = 0;                                                             \
            cur.cap = 0;                                                           \
            y += nonneg(lh) + RD_LEAD;                                             \
            cur_x = x0;                                                            \
            line_h = 0;                                                            \
            pending_lh = 0;                                                        \
        }                                                                          \
    } while (0)

    for (int k = 0; k < uv->n; k++) {
        Unit *u = &uv->d[k];
        if (u->len == 1 && u->text[0] == '\n') {
            FLUSH(); /* 源码里的换行：新行，不是折行续行 */
            cont = 0;
            continue;
        }
        int w = measure_item(&u->item, u->text, u->len);
        if (w > avail && avail > 0) {
            /* 超宽单元（超长 URL、无空格长串）：折行判定救不了它——
             * cur_x==x0 时条件不成立，会一路溢出视口。按字符硬断开。 */
            int bx = cur_x;
            int rem = u->len;
            const char *bt = u->text;
            /* 行高在硬断前算好，每段都用它 */
            int hlb = force_lh > 0 ? force_lh : (int)((long)u->item.px * 3 / 2 + 2);
            if (hlb > line_h) line_h = hlb;
            while (rem > 0) {
                /* 二分找能塞进本行的最长字节前缀 */
                int lo = 1, hi = rem, bestn = 0;
                while (lo <= hi) {
                    int mid = lo + (hi - lo) / 2;
                    if (measure_item(&u->item, bt, mid) <= avail) {
                        bestn = mid;
                        lo = mid + 1;
                    } else {
                        hi = mid - 1;
                    }
                }
                if (bestn == 0) bestn = rem; /* 单字符都塞不下，原样放置避免死循环 */
                DrawUnit du;
                memset(&du, 0, sizeof(du));
                du.x = bx;
                du.item = u->item;
                du.text = bt;
                du.text_len = bestn;
                DUVec_push(&cur, du);
                int adv = measure_item(&u->item, bt, bestn);
                bx += adv;
                bt += bestn;
                rem -= bestn;
                if (rem > 0) {
                    /* 已产出过行才叫续行；第一段是逻辑行的开头 */
                    cont = (first_idx >= 0);
                    pending_lh = hlb;
                    FLUSH();
                    bx = x0;
                }
            }
            cur_x = bx;
            cont = (first_idx >= 0);
            pending_lh = hlb;
            continue;
        }
        /* 常规折行判定：超出可用宽度且当前行已有内容。
         * first_idx<0 表示本次调用还没产出过视觉行，也就是正处在源码行开头 ——
         * 这时溢出的那一行不是"续行"，否则复制文本时会在行首吞掉换行。 */
        if (cur_x + w > x0 + avail && cur_x > x0) {
            cont = (first_idx >= 0);
            FLUSH();
        }
        if (cur.n > 0 && same_item(&cur.d[cur.n - 1].item, &u->item)) {
            /* 同样式相邻单元合并成一段绘制。新串进布局池，随下一次 layout 释放。 */
            cur.d[cur.n - 1].text =
                join_text(L->d, cur.d[cur.n - 1].text, cur.d[cur.n - 1].text_len, u->text, u->len);
            cur.d[cur.n - 1].text_len += u->len;
        } else {
            DrawUnit du;
            memset(&du, 0, sizeof(du));
            du.x = cur_x;
            du.item = u->item;
            du.text = u->text;
            du.text_len = u->len;
            DUVec_push(&cur, du);
        }
        cur_x += w;
        int lh = (int)((long)u->item.px * 3 / 2 + 2);
        if (lh > line_h) line_h = lh;
    }
    FLUSH();
#undef FLUSH

    if (cur.d) free(cur.d);
    if (b) b->first_ln = first_idx;
    return y;
}

/* ------------------------------------------------------------------ 表格 */

/* 单元格内容折行（写入 Lay，同时记录 linkRects），返回结束 y */
static int wrap_cell(Lay *L, RIVec *items, int x0, int avail, int y0) {
    UVec uv = {0};
    for (int i = 0; i < items->n; i++) split_units(&items->d[i], &uv);
    int y = wrap_units(L, &uv, -1, NULL, x0, nonneg(avail), y0, 1, 0);
    free(uv.d);
    return y;
}

static int do_table(Lay *L, Block *b, int width, int y0) {
    TableData *td = b->table;
    /* 列数来自解析，必须夹住：负数会让 realloc 的 size 变成天文数字。
     * MAX_TABLE_COLS 与解析器的上限一致，正常路径永远碰不到这个分支。 */
    if (!td || td->cols <= 0 || td->cols > 512) return y0;
    int x0 = RD_LM + b->indent;
    int avail = width - RD_LM - RD_RM - b->indent;
    if (avail < 200) avail = 200; /* 表格低于 200px 会挤成一团 */

    int pad = 8;
    int ncol = td->cols;
    if (td->nrow < 0 || td->nrow > 100000) return y0;
    int *nat = (int *)calloc((size_t)ncol, sizeof(int));
    if (!nat) return y0;
    for (int i = 0; i < ncol; i++) nat[i] = 40; /* 列宽下限 */

    /* 测量每列的自然宽度 */
    for (int r = 0; r < td->nrow; r++) {
        TableRow *row = &td->rows[r];
        for (int ci = 0; ci < row->ncell && ci < ncol; ci++) {
            RIVec items = {0};
            cell_items(&row->cells[ci], row->header, &items);
            int w = 0;
            for (int i = 0; i < items.n; i++) w += measure_item(&items.d[i], items.d[i].text, items.d[i].len);
            free(items.d);
            w += 2 * pad;
            if (w > nat[ci]) nat[ci] = w;
        }
    }

    int total = 0;
    for (int i = 0; i < ncol; i++) total += nat[i];

    int *colw = (int *)calloc((size_t)ncol, sizeof(int));
    if (!colw) {
        free(nat);
        return y0;
    }
    if (total <= avail || total <= 0) {
        for (int i = 0; i < ncol; i++) colw[i] = nat[i];
    } else {
        /* 按比例压缩，但不低于 40 */
        for (int i = 0; i < ncol; i++) {
            int cw = (int)((float)nat[i] * (float)avail / (float)total + 0.5f);
            if (cw < 40) cw = 40;
            colw[i] = cw;
        }
    }
    free(nat);

    /* realloc 失败时保留旧指针（后续写入要判空），不做"失败即崩"的假设 */
    i32 *nw = (i32 *)realloc(td->col_w, (size_t)ncol * sizeof(i32));
    if (nw) td->col_w = nw;
    nw = (i32 *)realloc(td->col_x, (size_t)ncol * sizeof(i32));
    if (nw) td->col_x = nw;
    if (!td->col_w || !td->col_x) {
        free(colw);
        return y0;
    }
    int cx = x0;
    for (int i = 0; i < ncol; i++) {
        td->col_w[i] = colw[i];
        td->col_x[i] = cx;
        cx += colw[i];
    }
    td->x0 = x0;
    td->x1 = cx;
    free(colw);

    int y = y0;
    for (int r = 0; r < td->nrow; r++) {
        TableRow *row = &td->rows[r];
        row->y0 = y;
        int max_h = 0;
        for (int ci = 0; ci < row->ncell && ci < ncol; ci++) {
            TableCell *cell = &row->cells[ci];
            int cw = td->col_w[ci];
            int content_w = cw - 2 * pad;
            if (content_w < 20) content_w = 20;
            int content_x = td->col_x[ci] + pad;
            int cy = y + pad;

            RIVec items = {0};
            cell_items(cell, row->header, &items);
            int lr0 = L->links.n;
            int first_idx = L->lines.n;
            int by = wrap_cell(L, &items, content_x, content_w, cy);
            free(items.d);

            /* 居中/右对齐：按每个视觉行单独偏移。
             * Go 版用 map[y]off，本地用并行数组记录本单元格产生的偏移，
             * 因为同一 y 不会跨单元格，够用且无需哈希表。 */
            int nlines_here = L->lines.n - first_idx;
            int *voff = (int *)calloc((size_t)(nlines_here > 0 ? nlines_here : 1), sizeof(int));
            for (int i = 0; i < nlines_here; i++) {
                VisualLine *vl = &L->lines.d[first_idx + i];
                if (vl->nunit == 0) continue;
                int first_x = vl->units[0].x;
                DrawUnit *last_u = &vl->units[vl->nunit - 1];
                int last_w = measure_item(&last_u->item, last_u->text, last_u->text_len);
                int line_w = (last_u->x + last_w) - first_x;
                int off = 0;
                if (cell->align == ALIGN_CENTER) off = (content_w - line_w) / 2;
                else if (cell->align == ALIGN_RIGHT) off = content_w - line_w;
                voff[i] = off;
                if (off != 0) {
                    for (int j = 0; j < vl->nunit; j++) vl->units[j].x += off;
                }
            }
            /* 链接热区跟着对应行的偏移一起挪 */
            for (int k = lr0; k < L->links.n; k++) {
                for (int i = 0; i < nlines_here; i++) {
                    if (L->links.d[k].y == L->lines.d[first_idx + i].y) {
                        L->links.d[k].x += voff[i];
                        break;
                    }
                }
            }
            free(voff);

            int h = (by - cy) + 2 * pad;
            if (h > max_h) max_h = h;
        }
        row->y1 = y + nonneg(max_h);
        y = row->y1;
    }
    return y;
}

/* ------------------------------------------------------------------ 代码块 */

/* 代码块。Go 版是"每个源码行单独跑一遍折行"，行距固定 effPx(codePx)*3/2。
 * 这里保持一致：逐行 tokenize + 折行，行与行之间不靠 "\n" 单元分隔，
 * 而是每行结束就 flush，天然不会串行。 */
static int do_code(Lay *L, Block *b, int block_idx, int width, int y0) {
    int x0 = RD_LM + b->indent;
    int avail = width - RD_LM - RD_RM - b->indent;
    if (avail < 80) avail = 80;

    const LangDef *def = hl_lang_def(b->lang);
    HLState hst = {0};
    int y = y0;
    int line_h = eff_px(RD_CODE_PX) * 3 / 2;

    for (int li = 0; li < b->nline; li++) {
        TokVec tv = {0};
        hl_tokenize(b->lines[li], b->line_lens[li], def, &hst, &tv);
        UVec uv = {0};
        for (int ti = 0; ti < tv.n; ti++) {
            RunItem it;
            memset(&it, 0, sizeof(it));
            it.family = RD_FONT_CODE;
            it.px = eff_px(RD_CODE_PX);
            it.code = 1;
            it.color = hl_color(tv.d[ti].cls);
            it.text = tv.d[ti].text;
            it.len = tv.d[ti].len;
            /* 代码按 token 边界成段；token 内不再细切，
             * 窄窗口下靠 token 整体折行，可读性优于逐字符断开。 */
            u_push(&uv, it.text, it.len, &it);
        }
        y = wrap_units(L, &uv, block_idx, b, x0, avail, y, 0, line_h);
        free(uv.d);
        hl_tokvec_free(&tv);
    }
    return y;
}

static int do_text(Lay *L, Block *b, int block_idx, int width, int y0) {
    RIVec items = {0};
    spans_to_items(b, &items);
    int x0 = RD_LM + b->indent;
    int avail = width - RD_LM - RD_RM - b->indent;
    if (avail < 80) avail = 80;

    UVec uv = {0};
    for (int i = 0; i < items.n; i++) split_units(&items.d[i], &uv);
    int y = wrap_units(L, &uv, block_idx, b, x0, avail, y0, 1, 0);
    free(uv.d);
    free(items.d);
    return y;
}

/* ------------------------------------------------------------------ layout */

void rd_layout(Doc *d, int width) {
    if (!d) return;
    layout_clear(d);
    if (width <= 0) width = 1; /* 负/零宽度直接归一，后面到处都会再夹 */

    Lay L;
    memset(&L, 0, sizeof(L));
    L.d = d;

    int y = RD_TM;
    for (int i = 0; i < d->nblock; i++) {
        Block *b = &d->blocks[i];
        b->y0 = y;
        const char *k = b->kind;
        if (strcmp(k, "hr") == 0) {
            y += 16;
        } else if (strcmp(k, "code") == 0) {
            y = do_code(&L, b, i, width, y);
        } else if (strcmp(k, "table") == 0) {
            y = do_table(&L, b, width, y);
        } else {
            y = do_text(&L, b, i, width, y);
        }
        b->y1 = y;
        y += block_gap(k);
    }
    d->content_height = nonneg(y + 24);
    d->lines = L.lines.d;
    d->nline = L.lines.n;
    d->link_rects = L.links.d;
    d->nlink = L.links.n;
}

/* ------------------------------------------------------------------ 绘制 */

static void fill(HDC hdc, RECT *r, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    if (b) {
        FillRect(hdc, r, b);
        DeleteObject(b);
    }
}

/* 只画一段文字（不含背景/下划线/删除线，那些由调用方按整行几何画） */
static void draw_text(HDC hdc, int x, int y, const char *s, int len, RunItem *it, COLORREF dflt) {
    if (!s || len <= 0) return;
    wchar_t *w = (wchar_t *)LocalAlloc(LMEM_FIXED, ((size_t)len + 1) * sizeof(wchar_t));
    if (!w) return;
    int wl = MultiByteToWideChar(CP_UTF8, 0, s, len, w, len + 1);
    if (wl <= 0) {
        LocalFree(w);
        return;
    }
    w[wl] = 0;
    HFONT f = get_font(it->family, it->px, it->bold, it->italic);
    HGDIOBJ old = SelectObject(hdc, f);
    COLORREF col = it->color ? it->color : dflt;
    if (it->link) col = COL_LINK;
    SetTextColor(hdc, col);
    TextOutW(hdc, x, y, w, wl);
    SelectObject(hdc, old);
    LocalFree(w);
}

/* 画一条 1px 水平线（pen 颜色由调用方给） */
static void hline(HDC hdc, int x1, int x2, int y, COLORREF c) {
    if (x2 <= x1) return;
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    if (!pen) return;
    HGDIOBJ old = SelectObject(hdc, pen);
    MoveToEx(hdc, x1, y, NULL);
    LineTo(hdc, x2, y);
    SelectObject(hdc, old);
    DeleteObject(pen);
}

static void vline(HDC hdc, int x, int y1, int y2, COLORREF c) {
    if (y2 <= y1) return;
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    if (!pen) return;
    HGDIOBJ old = SelectObject(hdc, pen);
    MoveToEx(hdc, x, y1, NULL);
    LineTo(hdc, x, y2);
    SelectObject(hdc, old);
    DeleteObject(pen);
}

static void rd_paint_to(Doc *d, HDC hdc, int scroll, int width, int height,
                        SelPos a, SelPos b, int selOn) {
    if (!d || !hdc) return;
    /* 客户区被压得极窄/极矮时可能算出 0 或负值；用负尺寸构造 RECT 会让
     * FillRect/BitBlt 越界，直接跳过绘制。这是 Go 版崩溃修复的同一道闸。 */
    if (width <= 0 || height <= 0) return;

    RECT full = safe_rect(0, 0, width, height);
    fill(hdc, &full, COL_BG);

    /* 块级背景：代码块底 + 引用竖条 + 表头底 */
    for (int i = 0; i < d->nblock; i++) {
        Block *b = &d->blocks[i];
        if (b->y1 < scroll || b->y0 > scroll + height) continue;
        if (strcmp(b->kind, "code") == 0) {
            RECT r = safe_rect(RD_LM - 12, b->y0 - 8, width - RD_RM + 12, b->y1 + 8);
            fill(hdc, &r, COL_CODE_BG);
            HPEN pen = CreatePen(PS_SOLID, 1, COL_QUOTE_BAR);
            if (pen) {
                HGDIOBJ old = SelectObject(hdc, pen);
                MoveToEx(hdc, RD_LM - 12, b->y0 - 8, NULL);
                LineTo(hdc, RD_LM - 12, b->y1 + 8);
                SelectObject(hdc, old);
                DeleteObject(pen);
            }
        }
        if (b->quote) {
            RECT r = safe_rect(RD_LM + 4, b->y0 - 2, RD_LM + 9, b->y1 + 2);
            fill(hdc, &r, COL_QUOTE_BAR);
        }
        if (strcmp(b->kind, "table") == 0 && b->table && b->table->nrow > 0 &&
            b->table->rows[0].header && b->table->col_x) {
            TableRow *hdr = &b->table->rows[0];
            if (!(hdr->y1 < scroll || hdr->y0 > scroll + height)) {
                RECT r = safe_rect(b->table->x0, hdr->y0 - scroll, b->table->x1,
                                   hdr->y1 - scroll);
                fill(hdc, &r, COL_TBL_HEAD_BG);
            }
        }
    }

    /* 选区高亮：在块背景之上、文字之下（与 Go 版一致）。
     * 不透明浅蓝即可，因为随后文字画在其上。 */
    if (selOn && d->nline > 0) {
        RECT srects[512];
        int sn = rd_selection_rects(d, scroll, a, b, srects, 512);
        if (sn > 0) {
            HBRUSH sbr = CreateSolidBrush(COL_SEL);
            if (sbr) {
                for (int si = 0; si < sn; si++) FillRect(hdc, &srects[si], sbr);
                DeleteObject(sbr);
            }
        }
    }

    SetBkMode(hdc, TRANSPARENT);

    /* 文本 */
    for (int i = 0; i < d->nline; i++) {
        VisualLine *vl = &d->lines[i];
        int y_top = vl->y - scroll;
        if (y_top > height || y_top + vl->height < 0) continue;

        if (vl->marker && vl->marker_len > 0) {
            RunItem mi;
            memset(&mi, 0, sizeof(mi));
            mi.family = RD_FONT_UI;
            mi.px = eff_px(RD_BODY_PX);
            mi.bold = 1;
            COLORREF mc = COL_HEAD;
            if (vl->marker_len == 3) {
                if (memcmp(vl->marker, "\xE2\x98\x91", 3) == 0) mc = COL_TASK_DONE;       /* ☑ */
                else if (memcmp(vl->marker, "\xE2\x98\x90", 3) == 0) mc = COL_TASK_TODO;  /* ☐ */
                else if (memcmp(vl->marker, "\xE2\x97\xA6", 3) == 0) mc = COL_LINK;       /* ◦ */
                else if (memcmp(vl->marker, "\xE2\x96\xAA", 3) == 0) mc = COL_LINK;       /* ▪ */
            }
            mi.color = mc;
            draw_text(hdc, vl->marker_x, y_top, vl->marker, vl->marker_len, &mi, COL_HEAD);
        }

        for (int k = 0; k < vl->nunit; k++) {
            DrawUnit *du = &vl->units[k];
            int w = du->item.bg ? measure_item(&du->item, du->text, du->text_len) : 0;
            if (du->item.bg) {
                RECT br = safe_rect(du->x - 2, y_top + 1, du->x + w + 2, y_top + vl->height - 1);
                fill(hdc, &br, du->item.bg);
            }
            draw_text(hdc, du->x, y_top, du->text, du->text_len, &du->item, COL_TEXT);
            /* 删除线画在文字中线；链接下划线画在行底往上 3px（与 Go 版一致） */
            if (du->item.strike) {
                int ww = w ? w : measure_item(&du->item, du->text, du->text_len);
                COLORREF sc = du->item.color ? du->item.color : COL_TEXT;
                hline(hdc, du->x, du->x + ww, y_top + vl->height / 2, sc);
            }
            if (du->item.underline && du->item.link) {
                int ww = w ? w : measure_item(&du->item, du->text, du->text_len);
                hline(hdc, du->x, du->x + ww, y_top + vl->height - 3, COL_LINK);
            }
        }
    }

/* 分隔线画在文本之后（hr 块没有行，只画线） */
for (int i = 0; i < d->nblock; i++) {
        Block *b = &d->blocks[i];
        if (strcmp(b->kind, "hr") != 0) continue;
        int y = b->y0 - scroll + 8; /* Go 版：块顶 +8，块高固定 16 */
        if (y < -2 || y > height + 2) continue;
        hline(hdc, RD_LM, width - RD_RM, y, COL_HR);
    }

    /* 表格网格 */
    for (int i = 0; i < d->nblock; i++) {
        Block *b = &d->blocks[i];
        if (strcmp(b->kind, "table") != 0 || !b->table) continue;
        TableData *td = b->table;
        if (td->nrow == 0 || td->cols == 0 || !td->col_x || !td->col_w) continue;
        if (td->x1 <= td->x0) continue;
        if (td->rows[td->nrow - 1].y1 < scroll || td->rows[0].y0 > scroll + height) continue;

        int top = nonneg(td->rows[0].y0 - scroll);
        int bot = nonneg(td->rows[td->nrow - 1].y1 - scroll);
        hline(hdc, td->x0, td->x1, top, COL_HR);
        hline(hdc, td->x0, td->x1, bot, COL_HR);
        for (int r = 0; r < td->nrow; r++) {
            if (r > 0) hline(hdc, td->x0, td->x1, nonneg(td->rows[r].y0 - scroll), COL_HR);
        }
        for (int c = 0; c < td->cols; c++) {
            vline(hdc, td->col_x[c], top, bot, COL_HR);
        }
        vline(hdc, td->x1, top, bot, COL_HR);
    }
}

void rd_paint(Doc *d, HDC hdc, int scroll, int width, int height) {
    rd_paint_to(d, hdc, scroll, width, height, (SelPos){0, 0, 0},
                (SelPos){0, 0, 0}, 0);
}

/* ------------------------------------------------------------------ 双缓冲 */

/* 双缓冲包装：整帧先画到内存位图，再一次性 BitBlt 到目标 DC。
 *
 * 直接往窗口 DC 画会看到每一行文字依次"跳"出来（拖选、缩放时尤其明显），
 * 因为 GDI 是即时提交的，用户看见的是半成品帧。
 *
 * 位图按窗口尺寸缓存：resize 时只在尺寸真的变了才重建。
 * 任何一步失败都直接降级为"直接画"，绝不让绘制路径整体失败。 */
typedef struct {
    HDC dc;
    HBITMAP bmp;
    HGDIOBJ old_bmp;
    int w, h;
} Buf;

static Buf g_buf = {0};

static int buf_ensure(HDC target, int w, int h) {
    if (w <= 0 || h <= 0) return 0;
    if (g_buf.dc && g_buf.w == w && g_buf.h == h && g_buf.bmp) return 1;
    buf_release();
    HDC dc = CreateCompatibleDC(target);
    if (!dc) return 0;
    /* 关键：必须用 CreateDIBSection 强制 32 位色深。
     * 若用 CreateCompatibleBitmap(dc, ...)，而 dc 是 CreateCompatibleDC
     * 出来的内存 DC（默认挂 1x1 单色位图），会跟着创建出 1 位单色位图，
     * 所有颜色被量化成黑/白 —— 表现为整页文字纯黑、毫无着色。 */
    BITMAPINFOHEADER bi;
    memset(&bi, 0, sizeof(bi));
    bi.biSize = sizeof(bi);
    bi.biWidth = w;
    bi.biHeight = -h; /* top-down */
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;
    RGBQUAD *bits = NULL;
    HBITMAP bmp = CreateDIBSection(dc, (BITMAPINFO *)&bi, DIB_RGB_COLORS,
                                   (void **)&bits, NULL, 0);
    if (!bmp) {
        DeleteDC(dc);
        return 0;
    }
    g_buf.dc = dc;
    g_buf.bmp = bmp;
    g_buf.w = w;
    g_buf.h = h;
    return 1;
}

void buf_release(void) {
    if (g_buf.dc) {
        /* 位图必须先从 DC 上摘掉再 DeleteObject，否则 GDI 句柄悬垂 */
        if (g_buf.old_bmp) SelectObject(g_buf.dc, g_buf.old_bmp);
        if (g_buf.bmp) DeleteObject(g_buf.bmp);
        DeleteDC(g_buf.dc);
    }
    memset(&g_buf, 0, sizeof(g_buf));
}

void rd_paint_buffered(Doc *d, HDC target, int scroll, int width, int height,
                       SelPos a, SelPos b, int selOn) {
    if (!d || !target) return;
    if (width <= 0 || height <= 0) return;
    if (!buf_ensure(target, width, height)) {
        rd_paint_to(d, target, scroll, width, height, a, b, selOn); /* 降级：直接画 */
        return;
    }
    g_buf.old_bmp = SelectObject(g_buf.dc, g_buf.bmp);
    rd_paint_to(d, g_buf.dc, scroll, width, height, a, b, selOn);
    BitBlt(target, 0, 0, width, height, g_buf.dc, 0, 0, SRCCOPY);
    SelectObject(g_buf.dc, g_buf.old_bmp);
    g_buf.old_bmp = NULL;
}

/* ------------------------------------------------------------------ 命中测试 */

int rd_hit_link(Doc *d, int x, int y, char *buf, int bufsz) {
    if (!d || !buf || bufsz <= 0) return 0;
    buf[0] = '\0';
    for (int i = 0; i < d->nlink; i++) {
        LinkRect *r = &d->link_rects[i];
        if (x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h) {
            int n = r->dest_len;
            if (n >= bufsz) n = bufsz - 1;
            memcpy(buf, r->dest, (size_t)n);
            buf[n] = '\0';
            return n;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ 测试辅助 */

char *rd_dump_layout(const char *src, const char *path, int width) {
    Doc *d = md_parse(src, path);
    rd_layout(d, width);
    int cap = 4096;
    char *out = (char *)malloc((size_t)cap);
    int oi = 0;
    out[oi] = '\0';
    oi += _snprintf_s(out + oi, (size_t)(cap - oi), _TRUNCATE, "blocks=%d lines=%d links=%d h=%d\n",
                      d->nblock, d->nline, d->nlink, (int)d->content_height);
    for (int i = 0; i < d->nline && oi < cap - 256; i++) {
        VisualLine *vl = &d->lines[i];
        oi += _snprintf_s(out + oi, (size_t)(cap - oi), _TRUNCATE, "L%d y=%d h=%d wrap=%d n=%d\n",
                          i, (int)vl->y, (int)vl->height, vl->wrap, vl->nunit);
        for (int k = 0; k < vl->nunit && oi < cap - 256; k++) {
            DrawUnit *du = &vl->units[k];
            oi += _snprintf_s(out + oi, (size_t)(cap - oi), _TRUNCATE, "   x=%d '%.*s'\n",
                              (int)du->x, du->text_len, du->text);
        }
    }
    md_doc_free(d);
    return out;
}

/* =====================================================================
 * 文本选择：把内容坐标映射到选择位置，并计算选区矩形 / 选中文本。
 * 全部基于已排版的 DrawUnit，因此与渲染结果严格一致。
 * 选择状态（anchor/head/on/drag）由 ui.c 持有；这里只提供纯计算。
 * =================================================================== */

/* 前 rrun 个 rune 的字节长度 */
static int rune_prefix_bytes(const char *s, int len, int rrun) {
    int i = 0, rc = 0;
    unsigned int cp;
    while (i < len && rc < rrun) {
        int adv = utf8_next(s, len, i, &cp);
        i += adv;
        rc++;
    }
    return i;
}

/* 串的 rune 数 */
static int rune_count(const char *s, int len) {
    int i = 0, rc = 0;
    unsigned int cp;
    while (i < len) {
        int adv = utf8_next(s, len, i, &cp);
        i += adv;
        rc++;
    }
    return rc;
}

/* x 落在 du 中的 rune 偏移（用前缀整串测量，避免取整漂移导致光标错位） */
static int char_offset(const DrawUnit *du, int x) {
    int n = du->text_len;
    int prev = 0;
    int i = 0, rc = 0;
    unsigned int cp;
    while (i < n) {
        int adv = utf8_next(du->text, n, i, &cp);
        int w = measure_item(&du->item, du->text, i + adv);
        if (x < du->x + prev + (w - prev) / 2) return rc;
        prev = w;
        i += adv;
        rc++;
    }
    return rc;
}

/* du 中第 ch 个 rune 之前的 x 坐标 */
static int unit_x_offset(const DrawUnit *du, int ch) {
    if (ch <= 0) return du->x;
    int bytes = rune_prefix_bytes(du->text, du->text_len, ch);
    if (bytes >= du->text_len)
        return du->x + measure_item(&du->item, du->text, du->text_len);
    return du->x + measure_item(&du->item, du->text, bytes);
}

int rd_sel_less(SelPos a, SelPos b) {
    if (a.line != b.line) return a.line < b.line;
    if (a.unit != b.unit) return a.unit < b.unit;
    return a.ch < b.ch;
}

int rd_sel_equal(SelPos a, SelPos b) {
    return a.line == b.line && a.unit == b.unit && a.ch == b.ch;
}

/* 内容坐标 (x,y) -> 选择位置。y 已经是内容坐标（调用方已加 scroll）。 */
SelPos rd_hit_pos(Doc *d, int x, int y) {
    SelPos r = {0, 0, 0};
    if (d->nline == 0) return r;
    int li = d->nline - 1;
    for (int i = 0; i < d->nline; i++) {
        VisualLine *vl = &d->lines[i];
        if (y < vl->y) { li = i; break; }
        if (y < vl->y + vl->height) { li = i; break; }
    }
    VisualLine *vl = &d->lines[li];
    if (vl->nunit == 0) { r.line = li; return r; }
    for (int i = 0; i < vl->nunit; i++) {
        DrawUnit *du = &vl->units[i];
        int w = measure_item(&du->item, du->text, du->text_len);
        if (x < du->x) { r.line = li; r.unit = i; r.ch = 0; return r; }
        if (x < du->x + w) { r.line = li; r.unit = i; r.ch = char_offset(du, x); return r; }
    }
    int last = vl->nunit - 1;
    DrawUnit *du = &vl->units[last];
    r.line = li; r.unit = last;
    r.ch = rune_count(du->text, du->text_len);
    return r;
}

/* 选区矩形（屏幕坐标，y 已减 scroll）。返回矩形个数，写入 out（最多 maxn）。 */
int rd_selection_rects(Doc *d, int scroll, SelPos a, SelPos b, RECT *out, int maxn) {
    int n = 0;
    if (d->nline == 0) return 0;
    if (a.line >= d->nline) return 0;
    if (b.line >= d->nline) b.line = d->nline - 1;
    for (int li = a.line; li <= b.line && li < d->nline; li++) {
        if (n >= maxn) break;
        VisualLine *vl = &d->lines[li];
        int yTop = vl->y - scroll;
        if (vl->nunit == 0) {
            out[n].left = RD_LM; out[n].top = yTop;
            out[n].right = RD_LM + 6; out[n].bottom = yTop + vl->height;
            n++;
            continue;
        }
        int first = 0, last = vl->nunit - 1;
        int ch0 = 0, ch1 = -1; /* -1 表示取到单元末尾 */
        if (li == a.line) { first = a.unit; ch0 = a.ch; }
        if (li == b.line) { last = b.unit; ch1 = b.ch; }
        if (first > last) continue;
        if (first >= vl->nunit || last >= vl->nunit) continue;
        int x0 = unit_x_offset(&vl->units[first], ch0);
        DrawUnit *du1 = &vl->units[last];
        int x1;
        if (ch1 < 0 || ch1 >= rune_count(du1->text, du1->text_len))
            x1 = du1->x + measure_item(&du1->item, du1->text, du1->text_len);
        else
            x1 = unit_x_offset(du1, ch1);
        if (x1 <= x0) continue;
        out[n].left = x0; out[n].top = yTop;
        out[n].right = x1; out[n].bottom = yTop + vl->height;
        n++;
    }
    return n;
}

/* 选中文本：传入规范化后的 [a,b]。返回 malloc 的 UTF-8 串，需调用方 free。 */
char *rd_selected_text(Doc *d, SelPos a, SelPos b) {
    if (d->nline == 0) return NULL;
    size_t cap = 64;
    for (int li = a.line; li <= b.line && li < d->nline; li++) {
        VisualLine *vl = &d->lines[li];
        for (int ui = 0; ui < vl->nunit; ui++)
            cap += (size_t)vl->units[ui].text_len + 8;
        cap += 8;
    }
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    int pos = 0;
    for (int li = a.line; li <= b.line && li < d->nline; li++) {
        VisualLine *vl = &d->lines[li];
        if (li > a.line && !vl->wrap) buf[pos++] = '\n';
        for (int ui = 0; ui < vl->nunit; ui++) {
            DrawUnit *du = &vl->units[ui];
            if (li == a.line && ui < a.unit) continue;
            if (li == b.line && ui > b.unit) continue;
            int s = 0, e = du->text_len;
            if (li == a.line && ui == a.unit) s = rune_prefix_bytes(du->text, du->text_len, a.ch);
            if (li == b.line && ui == b.unit) e = rune_prefix_bytes(du->text, du->text_len, b.ch);
            if (s < 0) s = 0;
            if (e > du->text_len) e = du->text_len;
            if (s >= e) continue;
            memcpy(buf + pos, du->text + s, (size_t)(e - s));
            pos += (e - s);
        }
    }
    buf[pos] = '\0';
    return buf;
}

/* 全选范围：写入 *a, *b */
void rd_select_all(Doc *d, SelPos *a, SelPos *b) {
    if (d->nline == 0) { *a = (SelPos){0, 0, 0}; *b = (SelPos){0, 0, 0}; return; }
    a->line = 0; a->unit = 0; a->ch = 0;
    int li = d->nline - 1;
    VisualLine *vl = &d->lines[li];
    if (vl->nunit == 0) { b->line = li; b->unit = 0; b->ch = 0; }
    else {
        int u = vl->nunit - 1;
        b->line = li; b->unit = u;
        b->ch = rune_count(vl->units[u].text, vl->units[u].text_len);
    }
}
