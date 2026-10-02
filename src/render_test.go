package main

import (
	"testing"
)

func TestRenderSample(t *testing.T) {
	measDC = getDC(0)
	if measDC == 0 {
		t.Fatal("no device context")
	}
	src := []byte(`# 标题 H1

一些 **粗体** 与 *斜体* 以及 ` + "`行内代码`" + ` 和 [链接](https://example.com)。

## 二级标题

- 列表项一
- 列表项二
  - 嵌套项
- [x] 已完成任务
- [ ] 未完成任务

` + "```go\nfunc main() {\n\tfmt.Println(\"hello\")\n}\n```" + `

> 这是一段引用文字，用来测试引用块的渲染效果。

---

1. 有序一
2. 有序二

| 列A | 列B |
| --- | --- |
| 1 | 2 |
`)
	d := ParseMarkdown(src, "test.md")
	d.layout(820)
	if d.contentHeight <= 0 {
		t.Fatalf("contentHeight <= 0: %d", d.contentHeight)
	}
	if len(d.lines) == 0 {
		t.Fatal("no visual lines produced")
	}
	if len(d.linkRects) == 0 {
		t.Fatal("expected at least one link rect")
	}
	t.Logf("blocks=%d lines=%d links=%d contentHeight=%d", len(d.blocks), len(d.lines), len(d.linkRects), d.contentHeight)
}
