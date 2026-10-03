/* 代码语法着色实现。配色沿用 Go 版的 GitHub Light 主题。
 *
 * 结构与 Go 版 highlight.go 一一对应：语言表 → 词法主循环。
 */
#include "highlight.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ 配色 */

/* 与 render.c 的 COLR 一致：0xRRGGBB → COLORREF（R/B 互换）。
 * 早期漏做转换，hl_color 返回的色值 R/B 通道对调，代码着色色相全是反的。 */
#define HC(x) RGB(((x) >> 16) & 0xFF, ((x) >> 8) & 0xFF, (x) & 0xFF)

#define COL_CODE_TEXT    HC(0x1F2328)
#define COL_CODE_KEYWORD HC(0xCF222E)
#define COL_CODE_TYPE    HC(0x953800)
#define COL_CODE_STRING  HC(0x0A3069)
#define COL_CODE_COMMENT HC(0x6E7781)
#define COL_CODE_NUMBER  HC(0x0550AE)
#define COL_CODE_PREPROC HC(0x116329)
#define COL_CODE_FUNC    HC(0x8250DF)

COLORREF hl_color(unsigned char cls) {
    switch (cls) {
    case CC_KEYWORD: return COL_CODE_KEYWORD;
    case CC_TYPE: return COL_CODE_TYPE;
    case CC_STRING: return COL_CODE_STRING;
    case CC_COMMENT: return COL_CODE_COMMENT;
    case CC_NUMBER: return COL_CODE_NUMBER;
    case CC_PREPROC: return COL_CODE_PREPROC;
    case CC_FUNC: return COL_CODE_FUNC;
    }
    return COL_CODE_TEXT;
}

/* ------------------------------------------------------------------ 词表 */

static const char *const kw_words[] = {
    "if", "else", "for", "while", "do", "switch", "case", "default", "break", "continue",
    "return", "goto", "select", "defer", "range", "fallthrough", "func", "package", "import",
    "var", "const", "type", "struct", "interface", "map", "chan", "ifdef", "ifndef", "endif",
    "define", "include", "elif", "undef", "class", "public", "private", "protected", "virtual",
    "override", "friend", "explicit", "namespace", "using", "template", "typename", "operator",
    "this", "new", "delete", "throw", "try", "catch", "finally", "raise", "except", "with",
    "as", "from", "export", "function", "local", "echo", "set", "unset", "source", "alias",
    "assert", "lambda", "yield", "pass", "global", "nonlocal", "match", "impl", "trait",
    "where", "async", "await", "static", "mut", "ref", "dyn", NULL};

static const char *const type_words[] = {
    "int", "int8", "int16", "int32", "int64", "uint", "uint8", "uint16", "uint32", "uint64",
    "uintptr", "float32", "float64", "complex64", "complex128", "string", "bool", "byte",
    "rune", "error", "void", "char", "short", "long", "float", "double", "signed",
    "unsigned", "size_t", "wchar_t", "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t",
    "uint16_t", "uint32_t", "uint64_t", "FILE", "va_list", "typeof", "union", "enum", "Any",
    "Object", "String", "Number", "Array", "List", "Dict", "Map", "Set", "i8", "i16", "i32",
    "i64", "u8", "u16", "u32", "u64", "usize", "isize", "f32", "f64", "str", NULL};

static const char *const json_words[] = {"true", "false", "null", NULL};
static const char *const shell_words[] = {
    "if", "then", "else", "elif", "fi", "for", "while", "do", "done", "case", "esac",
    "function", "return", "export", "local", "echo", "set", "unset", "source", "alias",
    "exit", NULL};
static const char *const sql_words[] = {
    "select", "from", "where", "insert", "update", "delete", "create", "table", "join",
    "left", "right", "inner", "outer", "on", "group", "by", "order", "limit", "as",
    "distinct", "and", "or", "not", "null", "primary", "key", "foreign", "references",
    "into", "values", "set", NULL};
static const char *const html_words[] = {"html", "head", "body", "div", "span", "script",
                                         "style", "class", "id", "href", "src", NULL};
static const char *const css_words[] = {"important", "media", "import", "charset", "keyframes",
                                        "font", "face", NULL};
static const char *const python_words[] = {"def", "lambda", "pass", "None",   "True", "False",
                                           "elif", "except", "with", "as",   "and",  "or",
                                           "not",  "in",     "is",   "raise", "yield", "from",
                                           "import", "global", "nonlocal", "del", "elif",
                                           "assert", "async", "await", "self",  "print",
                                           NULL};
static const char *const md_words[] = {"#", "##", "###", "-", "*", ">", "[]", "()", "![", NULL};

/* ------------------------------------------------------------------ 语言表 */

/* 默认按 C 家族处理 */
static const LangDef def_c = {{"//", NULL}, "/*", "*/", "\"'`", 1, 0, NULL, CC_PLAIN};
static const LangDef def_json = {{NULL, NULL}, NULL, NULL, "\"", 0, 0, json_words, CC_KEYWORD};
static const LangDef def_yaml = {{"#", NULL}, NULL, NULL, "\"`", 0, 0, NULL, CC_PLAIN};
static const LangDef def_shell = {
    {"#", NULL}, NULL, NULL, "\"'", 0, 1, shell_words, CC_KEYWORD};
static const LangDef def_python = {{"#", NULL}, NULL, NULL, "\"'", 0, 0, python_words, CC_KEYWORD};
static const LangDef def_sql = {{"--", NULL}, NULL, NULL, "'\"", 0, 0, sql_words, CC_KEYWORD};
static const LangDef def_lua = {{"--", NULL}, "--[[", "]]", "'\"", 0, 0, NULL, CC_PLAIN};
static const LangDef def_html = {{NULL, NULL}, "<!--", "-->", "\"'", 0, 0, html_words,
                                 CC_KEYWORD};
static const LangDef def_css = {{NULL, NULL}, "/*", "*/", "\"'", 0, 1, css_words, CC_KEYWORD};
static const LangDef def_md = {{NULL, NULL}, "<!--", "-->", "\"'", 0, 0, md_words, CC_KEYWORD};

static int eq_ci(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

const LangDef *hl_lang_def(const char *lang) {
    if (!lang || !*lang) return &def_c;
    if (eq_ci(lang, "json")) return &def_json;
    if (eq_ci(lang, "yaml") || eq_ci(lang, "yml") || eq_ci(lang, "toml") || eq_ci(lang, "ini"))
        return &def_yaml;
    if (eq_ci(lang, "sh") || eq_ci(lang, "bash") || eq_ci(lang, "zsh") || eq_ci(lang, "shell") ||
        eq_ci(lang, "console") || eq_ci(lang, "powershell") || eq_ci(lang, "ps1"))
        return &def_shell;
    if (eq_ci(lang, "py") || eq_ci(lang, "python")) return &def_python;
    if (eq_ci(lang, "sql")) return &def_sql;
    if (eq_ci(lang, "lua")) return &def_lua;
    if (eq_ci(lang, "html") || eq_ci(lang, "xml") || eq_ci(lang, "svg")) return &def_html;
    if (eq_ci(lang, "css") || eq_ci(lang, "scss") || eq_ci(lang, "less")) return &def_css;
    if (eq_ci(lang, "md") || eq_ci(lang, "markdown")) return &def_md;
    return &def_c;
}

/* ------------------------------------------------------------------ 词法主体 */

/* 词表匹配。SQL 关键字大小写不敏感（select/SELECT/SELECT 都算），
 * 其余语言（shell/python）本身全小写，用不区分大小写匹配也不会误判，
 * 所以统一按不区分大小写处理。 */
static int in_list(const char *const *list, const char *s, int len) {
    if (!list) return 0;
    for (int i = 0; list[i]; i++) {
        const char *w = list[i];
        int wl = (int)strlen(w);
        if (wl != len) continue;
        int k = 0, same = 1;
        for (; k < wl; k++) {
            char ca = w[k], cb = s[k];
            if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
            if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
            if (ca != cb) {
                same = 0;
                break;
            }
        }
        if (same) return 1;
    }
    return 0;
}

static int is_ident_start(unsigned char c) {
    return c == '_' || c == '$' || c == '@' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           c >= 0x80;
}
static int is_ident_part(unsigned char c) { return is_ident_start(c) || (c >= '0' && c <= '9'); }
static int is_digit(unsigned char c) { return c >= '0' && c <= '9'; }

static int has_prefix_at(const char *s, int len, int i, const char *pfx) {
    int pl = (int)strlen(pfx);
    if (pl == 0 || i + pl > len) return 0;
    return memcmp(s + i, pfx, (size_t)pl) == 0;
}

/* 找到 pfx 在 s[i..len) 里的首次出现位置，找不到返回 -1 */
static int index_of(const char *s, int len, int i, const char *pfx) {
    int pl = (int)strlen(pfx);
    if (pl == 0) return -1;
    for (int k = i; k + pl <= len; k++)
        if (memcmp(s + k, pfx, (size_t)pl) == 0) return k;
    return -1;
}

static void push_tok(TokVec *v, const char *s, int len, unsigned char cls) {
    if (len <= 0) return;
    if (v->n == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 16;
        CodeTok *nd = (CodeTok *)realloc(v->d, (size_t)v->cap * sizeof(CodeTok));
        if (!nd) return;
        v->d = nd;
    }
    v->d[v->n].text = s;
    v->d[v->n].len = len;
    v->d[v->n].cls = cls;
    v->n++;
}

void hl_tokenize(const char *line, int len, const LangDef *def, HLState *st, TokVec *out) {
    if (!def) def = &def_c;
    int i = 0;
    int block_start = -1; /* 本行块注释的开标记位置，-1 表示当前不在块中 */
    while (i < len) {
        /* 块注释中：找结束标记 */
        if (st->in_block) {
            int from = (block_start >= 0) ? block_start : i;
            int end = def->block_close ? index_of(line, len, from, def->block_close) : -1;
            int cl = def->block_close ? (int)strlen(def->block_close) : 0;
            if (end < 0) {
                push_tok(out, line + from, len - from, CC_COMMENT);
                block_start = -1;
                return;
            }
            push_tok(out, line + from, end - from + cl, CC_COMMENT);
            i = end + cl;
            st->in_block = 0;
            block_start = -1;
            continue;
        }

        /* 行注释：命中即到行尾 */
        for (int k = 0; k < 2; k++) {
            const char *lc = def->line_comment[k];
            if (lc && has_prefix_at(line, len, i, lc)) {
                push_tok(out, line + i, len - i, CC_COMMENT);
                return;
            }
        }

        /* 块注释起始：置状态后交给下一轮 inBlock 分支整段吃掉。
         * 记住开标记位置，inBlock 分支会从这里开始输出，
         * 这样整个块注释（含开标记）是一个 token，开标记不会丢。 */
        if (def->block_open && has_prefix_at(line, len, i, def->block_open)) {
            block_start = i;
            i += (int)strlen(def->block_open);
            st->in_block = 1;
            continue;
        }

        /* 预处理指令（仅行首） */
        if (def->has_preproc && line[i] == '#') {
            int allsp = 1;
            for (int k = 0; k < i; k++)
                if (line[k] != ' ' && line[k] != '\t') allsp = 0;
            if (allsp) {
                push_tok(out, line + i, len - i, CC_PREPROC);
                return;
            }
        }

        unsigned char c = (unsigned char)line[i];

        /* 字符串 */
        if (def->quotes && c && strchr(def->quotes, (char)c)) {
            int j = i + 1;
            while (j < len) {
                if (line[j] == '\\') {
                    j += 2;
                    continue;
                }
                if (line[j] == (char)c) {
                    j++;
                    break;
                }
                j++;
            }
            if (j > len) j = len;
            push_tok(out, line + i, j - i, CC_STRING);
            i = j;
            continue;
        }

        /* shell 变量 $VAR / ${VAR} */
        if (def->has_dollar && c == '$') {
            int j = i + 1;
            if (j < len && line[j] == '{') {
                while (j < len && line[j] != '}') j++;
                if (j < len) j++;
            } else {
                while (j < len && is_ident_part((unsigned char)line[j])) j++;
            }
            push_tok(out, line + i, j - i, CC_FUNC);
            i = j;
            continue;
        }

        /* 数字（含 1e+10 形式） */
        if (is_digit(c) || (c == '.' && i + 1 < len && is_digit((unsigned char)line[i + 1]))) {
            int j = i;
            while (j < len) {
                char d = line[j];
                if (is_ident_part((unsigned char)d) || d == '.') {
                    j++;
                } else if ((d == '+' || d == '-') && j > i &&
                           (line[j - 1] == 'e' || line[j - 1] == 'E')) {
                    j++;
                } else {
                    break;
                }
            }
            push_tok(out, line + i, j - i, CC_NUMBER);
            i = j;
            continue;
        }

        /* 标识符 / 关键字 */
        if (is_ident_start(c)) {
            int j = i;
            while (j < len && is_ident_part((unsigned char)line[j])) j++;
            const char *word = line + i;
            int wlen = j - i;
            unsigned char cls = CC_PLAIN;
            if (def->extra_words && in_list(def->extra_words, word, wlen)) {
                cls = def->extra_class;
            }
            if (cls == CC_PLAIN) {
                if (in_list(kw_words, word, wlen)) cls = CC_KEYWORD;
                else if (in_list(type_words, word, wlen)) cls = CC_TYPE;
            }
            if (cls == CC_PLAIN) {
                /* 函数调用：标识符（可隔空格）紧跟 ( */
                int k = j;
                while (k < len && (line[k] == ' ' || line[k] == '\t')) k++;
                if (k < len && line[k] == '(') cls = CC_FUNC;
            }
            push_tok(out, word, wlen, cls);
            i = j;
            continue;
        }

        /* 其它：连续空白归一块，减少 token 数 */
        if (c == ' ' || c == '\t') {
            int j = i;
            while (j < len && (line[j] == ' ' || line[j] == '\t')) j++;
            push_tok(out, line + i, j - i, CC_PLAIN);
            i = j;
        } else {
            push_tok(out, line + i, 1, CC_PLAIN);
            i++;
        }
    }
}

void hl_tokvec_free(TokVec *v) {
    if (!v) return;
    free(v->d);
    v->d = NULL;
    v->n = v->cap = 0;
}

/* ------------------------------------------------------------------ 测试辅助 */

static const char *cls_name(unsigned char c) {
    switch (c) {
    case CC_KEYWORD: return "kw";
    case CC_TYPE: return "ty";
    case CC_STRING: return "st";
    case CC_COMMENT: return "cm";
    case CC_NUMBER: return "nu";
    case CC_PREPROC: return "pp";
    case CC_FUNC: return "fn";
    }
    return "pl";
}

char *hl_dump(const char *src, const char *lang) {
    /* 逐行 tokenize，拼成 "文本|类别 " 的可读串 */
    const LangDef *def = hl_lang_def(lang);
    HLState st = {0};
    TokVec v = {0};
    int cap = (int)strlen(src) * 2 + 64;
    char *out = (char *)malloc((size_t)cap);
    int oi = 0;
    out[oi] = '\0';

    const char *p = src;
    while (*p) {
        const char *nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int)strlen(p);
        hl_tokenize(p, len, def, &st, &v);
        for (int i = 0; i < v.n; i++) {
            const char *nm = cls_name(v.d[i].cls);
            int need = v.d[i].len + 32;
            if (oi + need >= cap) {
                cap += need * 2;
                out = (char *)realloc(out, (size_t)cap);
            }
            memcpy(out + oi, v.d[i].text, (size_t)v.d[i].len);
            oi += v.d[i].len;
            oi += _snprintf_s(out + oi, (size_t)(cap - oi), _TRUNCATE, "|%s ", nm);
        }
        if (nl) {
            if (oi + 2 < cap) {
                out[oi++] = '\n';
                out[oi] = '\0';
            }
            p = nl + 1;
        } else {
            break;
        }
    }
    hl_tokvec_free(&v);
    return out;
}
