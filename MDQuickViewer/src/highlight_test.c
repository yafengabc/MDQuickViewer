/* 语法着色测试 */
#include "highlight_test.h"
#include "highlight.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
static const char *g_group = "";
static FILE *g_out = NULL;

static void tout(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (g_out) { vfprintf(g_out, fmt, ap); fflush(g_out); }
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
    if (g_out) { fprintf(g_out, "  FAIL [%s] %s:%d: %s\n", g_group, file, line, buf); fflush(g_out); }
    printf("  FAIL [%s] %s:%d: %s\n", g_group, file, line, buf);
    fflush(stdout);
}

#define CHECK(cond, ...)                     \
    do {                                     \
        if (cond) g_pass++;                  \
        else fail(__FILE__, __LINE__, __VA_ARGS__); \
    } while (0)

#define LIT(s) (s), (int)(sizeof(s) - 1)

/* 在 token 流里找某个类别且文本匹配的 token */
static int has_tok(TokVec *v, const char *text, unsigned char cls) {
    int tl = (int)strlen(text);
    for (int i = 0; i < v->n; i++) {
        if (v->d[i].cls == cls && v->d[i].len == tl &&
            memcmp(v->d[i].text, text, (size_t)tl) == 0)
            return 1;
    }
    return 0;
}

/* 某个类别是否存在（不检查文本） */
static int has_cls(TokVec *v, unsigned char cls) {
    for (int i = 0; i < v->n; i++)
        if (v->d[i].cls == cls) return 1;
    return 0;
}

static void run(const char *src, int len, const char *lang, TokVec *v, HLState *st) {
    const LangDef *def = hl_lang_def(lang);
    *st = (HLState){0};
    v->d = NULL;
    v->n = v->cap = 0;
    hl_tokenize(src, len, def, st, v);
}

static void test_lang_def(void) {
    g_group = "lang";
    CHECK(hl_lang_def("go") == hl_lang_def("unknown-xyz"), "未知语言应走 C 家族默认");
    CHECK(hl_lang_def("GO") != NULL && hl_lang_def("Go") != NULL, "大小写不敏感");
    const LangDef *js = hl_lang_def("json");
    CHECK(strcmp(js->quotes, "\"") == 0, "json 引号应为双引号");
    const LangDef *sh = hl_lang_def("bash");
    CHECK(sh->has_dollar == 1, "shell 应启用 $ 变量");
    const LangDef *py = hl_lang_def("python");
    CHECK(strcmp(py->line_comment[0], "#") == 0, "python 行注释为 #");
    CHECK(py->has_dollar == 0, "python 不应有 $ 变量");
    const LangDef *sql = hl_lang_def("sql");
    CHECK(strcmp(sql->line_comment[0], "--") == 0, "sql 行注释为 --");
    const LangDef *lua = hl_lang_def("lua");
    CHECK(strcmp(lua->block_open, "--[[") == 0, "lua 块注释为 --[[");
    const LangDef *c = hl_lang_def("c");
    CHECK(strcmp(c->block_open, "/*") == 0 && c->has_preproc, "C 应有块注释与预处理");
    CHECK(hl_lang_def(NULL) != NULL && hl_lang_def("") != NULL, "空语言不应崩");
}

static void test_c_family(void) {
    g_group = "c";
    TokVec v;
    HLState st;
    run(LIT("int main(void) { return 0; }"), NULL, &v, &st);
    CHECK(has_tok(&v, "int", CC_TYPE), "int 应识别为类型");
    CHECK(has_tok(&v, "return", CC_KEYWORD), "return 应识别为关键字");
    CHECK(has_tok(&v, "main", CC_FUNC), "main() 应识别为函数");
    CHECK(has_cls(&v, CC_PLAIN), "应有普通文本");
    hl_tokvec_free(&v);

    /* 字符串 */
    run(LIT("char *s = \"hi\";"), NULL, &v, &st);
    CHECK(has_tok(&v, "\"hi\"", CC_STRING), "\"hi\" 应为字符串");
    hl_tokvec_free(&v);

    /* 数字 */
    run(LIT("x = 42; y = 3.14; z = 0xFF;"), NULL, &v, &st);
    CHECK(has_tok(&v, "42", CC_NUMBER), "42 应为数字");
    CHECK(has_tok(&v, "3.14", CC_NUMBER), "3.14 应为数字");
    CHECK(has_tok(&v, "0xFF", CC_NUMBER), "0xFF 应为数字");
    hl_tokvec_free(&v);

    /* 科学计数法 */
    run(LIT("v = 1e+10;"), NULL, &v, &st);
    CHECK(has_tok(&v, "1e+10", CC_NUMBER), "1e+10 应为数字");
    hl_tokvec_free(&v);

    /* 行注释 */
    run(LIT("x = 1; // note"), NULL, &v, &st);
    CHECK(has_tok(&v, "// note", CC_COMMENT), "// 之后应全为注释");
    hl_tokvec_free(&v);

    /* 预处理仅在行首 */
    run(LIT("#include <stdio.h>"), NULL, &v, &st);
    CHECK(has_tok(&v, "#include <stdio.h>", CC_PREPROC), "行首 # 应为预处理");
    hl_tokvec_free(&v);

    run(LIT("a = b # not preproc"), NULL, &v, &st);
    CHECK(!has_cls(&v, CC_PREPROC), "非行首 # 不应是预处理");
    hl_tokvec_free(&v);

    /* 反引号字符串（C 家族含 `） */
    run(LIT("s = `raw`;"), NULL, &v, &st);
    CHECK(has_tok(&v, "`raw`", CC_STRING), "反引号应为字符串");
    hl_tokvec_free(&v);
}

static void test_block_comment(void) {
    g_group = "block";
    TokVec v;
    HLState st;

    /* 单行闭合 */
    run(LIT("a /* c */ b"), NULL, &v, &st);
    CHECK(has_tok(&v, "/* c */", CC_COMMENT), "单行块注释应整体着色");
    CHECK(st.in_block == 0, "闭合后不应仍在块注释中");
    hl_tokvec_free(&v);

    /* 跨行：状态要延续 */
    const LangDef *def = hl_lang_def("c");
    HLState s2 = {0};
    TokVec t1 = {0}, t2 = {0};
    hl_tokenize("a /* start", 10, def, &s2, &t1);
    CHECK(s2.in_block == 1, "未闭合时应置 in_block");
    CHECK(has_cls(&t1, CC_COMMENT), "块注释内容应着色为注释");
    hl_tokvec_free(&t1);

    hl_tokenize("end */ b", 8, def, &s2, &t2);
    CHECK(s2.in_block == 0, "闭合后应清 in_block");
    CHECK(has_tok(&t2, "end */", CC_COMMENT), "跨行块注释应着色");
    CHECK(has_tok(&t2, "b", CC_PLAIN), "闭合后应恢复普通文本");
    hl_tokvec_free(&t2);
}

static void test_json(void) {
    g_group = "json";
    TokVec v;
    HLState st;
    run(LIT("{\"k\": true, \"n\": null}"), "json", &v, &st);
    CHECK(has_tok(&v, "true", CC_KEYWORD), "json true 应为关键字");
    CHECK(has_tok(&v, "null", CC_KEYWORD), "json null 应为关键字");
    CHECK(has_tok(&v, "\"k\"", CC_STRING), "json 键应为字符串");
    hl_tokvec_free(&v);

    /* json 里 // 不是注释 */
    run(LIT("{\"u\": \"http://x\"}"), "json", &v, &st);
    CHECK(!has_cls(&v, CC_COMMENT), "json 不应把 // 当注释");
    hl_tokvec_free(&v);
}

static void test_shell(void) {
    g_group = "shell";
    TokVec v;
    HLState st;
    run(LIT("echo $HOME"), "bash", &v, &st);
    CHECK(has_tok(&v, "$HOME", CC_FUNC), "$VAR 应着色");
    CHECK(has_tok(&v, "echo", CC_KEYWORD), "echo 应为 shell 关键字");
    hl_tokvec_free(&v);

    run(LIT("echo ${MY_VAR}"), "bash", &v, &st);
    CHECK(has_tok(&v, "${MY_VAR}", CC_FUNC), "${VAR} 应着色");
    hl_tokvec_free(&v);

    /* hl_tokenize 是单行接口（渲染时逐行调用），这里只传第一行 */
    run(LIT("# comment"), "sh", &v, &st);
    CHECK(has_tok(&v, "# comment", CC_COMMENT), "# 应为 shell 注释");
    hl_tokvec_free(&v);
}

static void test_python(void) {
    g_group = "python";
    TokVec v;
    HLState st;
    run(LIT("def f(x):"), "python", &v, &st);
    CHECK(has_tok(&v, "def", CC_KEYWORD), "def 应为关键字");
    CHECK(has_tok(&v, "f", CC_FUNC), "f() 应为函数");
    hl_tokvec_free(&v);

    run(LIT("    return x"), "python", &v, &st);
    CHECK(has_tok(&v, "return", CC_KEYWORD), "return 应为关键字");
    hl_tokvec_free(&v);
}

static void test_sql(void) {
    g_group = "sql";
    TokVec v;
    HLState st;
    run(LIT("SELECT * FROM t -- note"), "sql", &v, &st);
    CHECK(has_tok(&v, "SELECT", CC_KEYWORD), "SELECT 应为关键字");
    CHECK(has_tok(&v, "FROM", CC_KEYWORD), "FROM 应为关键字");
    CHECK(has_tok(&v, "-- note", CC_COMMENT), "-- 应为注释");
    hl_tokvec_free(&v);
}

static void test_html(void) {
    g_group = "html";
    TokVec v;
    HLState st;
    run(LIT("<!-- c --><div class=\"x\">"), "html", &v, &st);
    CHECK(has_tok(&v, "<!-- c -->", CC_COMMENT), "HTML 注释应着色");
    CHECK(has_tok(&v, "div", CC_KEYWORD), "div 应为 HTML 关键字");
    CHECK(has_tok(&v, "\"x\"", CC_STRING), "属性值应为字符串");
    hl_tokvec_free(&v);
}

static void test_edge(void) {
    g_group = "edge";
    TokVec v;
    HLState st;

    /* 空行 */
    run("", 0, NULL, &v, &st);
    CHECK(v.n == 0, "空行应无 token");
    hl_tokvec_free(&v);

    /* 未闭合字符串不应崩 */
    run(LIT("s = \"unclosed"), NULL, &v, &st);
    CHECK(v.n > 0, "未闭合字符串仍应产出 token");
    hl_tokvec_free(&v);

    /* 转义引号 */
    run(LIT("s = \"a\\\"b\";"), NULL, &v, &st);
    CHECK(has_tok(&v, "\"a\\\"b\"", CC_STRING), "转义引号应整体为字符串");
    hl_tokvec_free(&v);

    /* 纯空白 */
    run(LIT("    "), NULL, &v, &st);
    CHECK(v.n == 1 && v.d[0].cls == CC_PLAIN, "纯空白应是单个普通 token");
    hl_tokvec_free(&v);

    /* 标识符含高位字节（中文注释） */
    run(LIT("// 中文注释"), NULL, &v, &st);
    CHECK(has_tok(&v, "// 中文注释", CC_COMMENT), "含中文的注释应正常着色");
    hl_tokvec_free(&v);

    /* dump 不应崩 */
    char *d = hl_dump("int x = 1;", "go");
    CHECK(d && strstr(d, "int|ty") != NULL, "hl_dump 应包含类型标记，实际 %s", d ? d : "(null)");
    free(d);

    d = hl_dump("", "go");
    CHECK(d != NULL, "空串 dump 不应返回 NULL");
    free(d);
}

static void test_color(void) {
    g_group = "color";
    CHECK(hl_color(CC_PLAIN) == COLR(0x1F2328), "普通文本色");
    CHECK(hl_color(CC_KEYWORD) == COLR(0xCF222E), "关键字色");
    CHECK(hl_color(CC_TYPE) == COLR(0x953800), "类型色");
    CHECK(hl_color(CC_STRING) == COLR(0x0A3069), "字符串色");
    CHECK(hl_color(CC_COMMENT) == COLR(0x6E7781), "注释色");
    CHECK(hl_color(CC_NUMBER) == COLR(0x0550AE), "数字色");
    CHECK(hl_color(CC_PREPROC) == COLR(0x116329), "预处理色");
    CHECK(hl_color(CC_FUNC) == COLR(0x8250DF), "函数名色");
}

int hl_tests_run(void) {
    g_pass = g_fail = 0;
    wchar_t logpath[MAX_PATH];
    if (GetTempPathW(MAX_PATH, logpath)) {
        wcscat_s(logpath, MAX_PATH, L"MDQuickViewer-test.log");
        g_out = _wfopen(logpath, L"a"); /* 追加，与 markdown 测试共处一个文件 */
    }
    tout("=== 语法着色测试 ===\n");
    test_lang_def();
    test_c_family();
    test_block_comment();
    test_json();
    test_shell();
    test_python();
    test_sql();
    test_html();
    test_edge();
    test_color();
    tout("=== 高亮: %d 通过, %d 失败 ===\n", g_pass, g_fail);
    /* tout() 已经同时写日志和 stdout，这里只关文件，不要再打一遍 */
    if (g_out) {
        fclose(g_out);
        g_out = NULL;
    }
    return g_fail == 0 ? 0 : 1;
}
