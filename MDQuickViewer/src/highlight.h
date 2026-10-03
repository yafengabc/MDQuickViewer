/* 代码语法着色：轻量级词法分析，只分类不做语法解析。
 * 目的是让源码块一眼看出结构，而不是精确还原某个主题。
 *
 * 与 Go 版 highlight.go 逐条对应：codeClass 分类、hlState 跨行块注释状态、
 * langDef 各语言定义表、highlightTokens 主循环。
 */
#ifndef GOMD_HIGHLIGHT_H
#define GOMD_HIGHLIGHT_H

#include "model.h"

enum {
    CC_PLAIN = 0,
    CC_KEYWORD,
    CC_TYPE,
    CC_STRING,
    CC_COMMENT,
    CC_NUMBER,
    CC_PREPROC,
    CC_FUNC
};

/* 一个着色 token（指向原文，不拷贝） */
typedef struct {
    const char *text;
    int len;
    unsigned char cls;
} CodeTok;

/* 跨行状态：块注释可能跨行 */
typedef struct {
    int in_block;
} HLState;

typedef struct {
    CodeTok *d;
    int n, cap;
} TokVec;

/* 取语言定义（内置表，不支持的语言返回 NULL） */
const LangDef *hl_lang_def(const char *lang);
/* 类别 → 颜色 */
COLORREF hl_color(unsigned char cls);

/* 把一行源码切成带类别的 token。st 需跨行保持。 */
void hl_tokenize(const char *line, int len, const LangDef *def, HLState *st, TokVec *out);
void hl_tokvec_free(TokVec *v);
/* 供测试：着色并渲染成 "文本:类别" 的可读串 */
char *hl_dump(const char *src, const char *lang);

#endif
