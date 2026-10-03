package main

import "strings"

// ---------------------------------------------------------------- 代码着色
// 轻量级词法着色：只按语言做关键字/字符串/注释/数字分类，不做语法解析。
// 目的是让源码块一眼能看出结构，而不是精确还原某个主题。

type codeClass uint8

const (
	ccPlain codeClass = iota
	ccKeyword
	ccType
	ccString
	ccComment
	ccNumber
	ccPreproc
	ccFunc
)

// hlState 跨行状态（块注释可能跨行）。
type hlState struct {
	inBlock bool
}

type codeTok struct {
	text  string
	class codeClass
}

func codeColor(c codeClass) COLORREF {
	switch c {
	case ccKeyword:
		return colCodeKeyword
	case ccType:
		return colCodeType
	case ccString:
		return colCodeString
	case ccComment:
		return colCodeComment
	case ccNumber:
		return colCodeNumber
	case ccPreproc:
		return colCodePreproc
	case ccFunc:
		return colCodeFunc
	}
	return colCodeText
}

type langDef struct {
	lineComment  []string  // 行注释前缀（"//" "#" "--"）
	blockComment [2]string // 块注释首尾，例如 /* */
	quotes       string    // 字符串引号集合，例如 "\"'`"
	hasPreproc   bool      // C 家族的 #include/#define
	hasDollar    bool      // shell 的 $VAR
	extraWords   map[string]codeClass
}

// 常见关键字（各语言取并集，命中即着色，误判代价很低）
var commonKeywords = words(`
	if else for while do switch case default break continue return goto
	select defer range fallthrough
	func package import var const type struct interface map chan
	ifdef ifndef endif define include ifndef elif undef
	class public private protected virtual override friend explicit
	namespace using template typename operator this new delete throw
	try catch finally raise except with as from import export
	return function local echo set unset source alias
	assert lambda yield pass global nonlocal with
	match impl trait where async await static mut ref dyn
`)

var commonTypes = words(`
	int int8 int16 int32 int64 uint uint8 uint16 uint32 uint64 uintptr
	float32 float64 complex64 complex128 string bool byte rune error
	void char short long float double signed unsigned size_t wchar_t
	bool int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t
	FILE va_list typeof struct union enum class namespace
	Any Object String Number Array List Dict Map Set
	i8 i16 i32 i64 u8 u16 u32 u64 usize isize f32 f64 str
`)

// wordsClass 生成"关键词 -> 类别"的映射（用于各语言专有词）。
func wordsClass(s string, c codeClass) map[string]codeClass {
	m := make(map[string]codeClass)
	for _, w := range strings.Fields(s) {
		m[w] = c
	}
	return m
}

func words(s string) map[string]bool {
	m := make(map[string]bool)
	for _, w := range strings.Fields(s) {
		m[w] = true
	}
	return m
}

func langDefFor(lang string) *langDef {
	l := strings.ToLower(lang)
	switch {
	case l == "json":
		return &langDef{quotes: `"`, extraWords: wordsClass(`true false null`, ccKeyword)}
	case l == "yaml" || l == "yml" || l == "toml" || l == "ini":
		return &langDef{lineComment: []string{"#"}, quotes: `"'`}
	case l == "sh" || l == "bash" || l == "zsh" || l == "shell" || l == "console" || l == "powershell" || l == "ps1":
		return &langDef{lineComment: []string{"#"}, quotes: `"'`, hasDollar: true,
			extraWords: wordsClass(`if then else elif fi for while do done case esac function return export local echo set unset source alias exit`, ccKeyword)}
	case l == "py" || l == "python":
		return &langDef{lineComment: []string{"#"}, quotes: `"'`, hasDollar: false}
	case l == "sql":
		return &langDef{lineComment: []string{"--"}, quotes: `'"`, extraWords: wordsClass(`select from where insert update delete create table join left right inner outer on group by order limit as distinct and or not null primary key foreign references into values set`, ccKeyword)}
	case l == "lua":
		return &langDef{lineComment: []string{"--"}, blockComment: [2]string{"--[[", "]]"}, quotes: `'"`}
	case l == "html" || l == "xml" || l == "svg":
		return &langDef{blockComment: [2]string{"<!--", "-->"}, quotes: `"'`, extraWords: wordsClass(`html head body div span script style class id href src`, ccKeyword)}
	case l == "css" || l == "scss" || l == "less":
		return &langDef{blockComment: [2]string{"/*", "*/"}, quotes: `"'`, hasDollar: true,
			extraWords: wordsClass(`important media import charset keyframes font face`, ccKeyword)}
	case l == "md" || l == "markdown":
		return &langDef{blockComment: [2]string{"<!--", "-->"}, quotes: `"'`,
			extraWords: wordsClass("# ## ### - * > [] () ![ ]", ccKeyword)}
	}
	// 默认按 C 家族处理（go/c/c++/java/js/ts/rust/swift/kotlin/scala…）
	return &langDef{lineComment: []string{"//"}, blockComment: [2]string{"/*", "*/"},
		quotes: "\"'`", hasPreproc: true}
}

func isIdentStart(c byte) bool {
	return c == '_' || c == '$' || c == '@' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80
}

func isIdentPart(c byte) bool { return isIdentStart(c) || (c >= '0' && c <= '9') }

func isDigit(c byte) bool { return c >= '0' && c <= '9' }

// highlightTokens 把一行源码切成带类别的 token；st 承载跨行状态。
func highlightTokens(line string, def *langDef, st *hlState) []codeTok {
	toks := make([]codeTok, 0, 16)
	i, n := 0, len(line)
	push := func(s string, c codeClass) {
		if s == "" {
			return
		}
		toks = append(toks, codeTok{text: s, class: c})
	}

	for i < n {
		// 块注释中
		if st.inBlock {
			end := strings.Index(line[i:], def.blockComment[1])
			if end < 0 {
				push(line[i:], ccComment)
				return toks
			}
			push(line[i:i+end+len(def.blockComment[1])], ccComment)
			i += end + len(def.blockComment[1])
			st.inBlock = false
			continue
		}

		// 行注释
		for _, lc := range def.lineComment {
			if lc != "" && strings.HasPrefix(line[i:], lc) {
				push(line[i:], ccComment)
				return toks
			}
		}
		// 块注释起始：标记状态后交给下一轮 inBlock 分支整段吃掉
		if def.blockComment[0] != "" && strings.HasPrefix(line[i:], def.blockComment[0]) {
			i += len(def.blockComment[0])
			st.inBlock = true
			continue
		}
		// 预处理指令（仅行首）
		if def.hasPreproc && line[i] == '#' && strings.TrimSpace(line[:i]) == "" {
			push(line[i:], ccPreproc)
			return toks
		}

		c := line[i]
		// 字符串
		if strings.IndexByte(def.quotes, c) >= 0 {
			j := i + 1
			for j < n {
				if line[j] == '\\' {
					j += 2
					continue
				}
				if line[j] == c {
					j++
					break
				}
				j++
			}
			if j > n {
				j = n
			}
			push(line[i:j], ccString)
			i = j
			continue
		}
		// shell 变量
		if def.hasDollar && c == '$' {
			j := i + 1
			if j < n && line[j] == '{' {
				for j < n && line[j] != '}' {
					j++
				}
				if j < n {
					j++
				}
			} else {
				for j < n && isIdentPart(line[j]) {
					j++
				}
			}
			push(line[i:j], ccFunc)
			i = j
			continue
		}
		// 数字
		if isDigit(c) || (c == '.' && i+1 < n && isDigit(line[i+1])) {
			j := i
			for j < n && (isIdentPart(line[j]) || line[j] == '.' ||
				((line[j] == '+' || line[j] == '-') && j > i && (line[j-1] == 'e' || line[j-1] == 'E'))) {
				j++
			}
			push(line[i:j], ccNumber)
			i = j
			continue
		}
		// 标识符 / 关键字
		if isIdentStart(c) {
			j := i
			for j < n && isIdentPart(line[j]) {
				j++
			}
			word := line[i:j]
			cls := ccPlain
			if def.extraWords != nil {
				if k, ok := def.extraWords[word]; ok {
					cls = k
				}
			}
			if cls == ccPlain {
				switch {
				case commonKeywords[word]:
					cls = ccKeyword
				case commonTypes[word]:
					cls = ccType
				}
			}
			if cls == ccPlain {
				// 函数调用：标识符紧跟 '('
				k := j
				for k < n && (line[k] == ' ' || line[k] == '\t') {
					k++
				}
				if k < n && line[k] == '(' {
					cls = ccFunc
				}
			}
			push(word, cls)
			i = j
			continue
		}

		// 其它：按连续同类字符归一块，减少 token 数量
		j := i
		for j < n && (line[j] == ' ' || line[j] == '\t') {
			j++
		}
		if j > i {
			push(line[i:j], ccPlain)
		} else {
			push(line[i:i+1], ccPlain)
			j = i + 1
		}
		i = j
	}
	return toks
}
