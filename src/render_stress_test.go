package main

import (
	"testing"
	"unsafe"
)

// 全特性压力测试：覆盖表格/图片/自动链接/原始HTML/脚注/嵌套列表/任务列表/
// 代码块/删除线/长URL等，确认解析+layout+真实GDI paint 不 panic。
func TestRenderStress(t *testing.T) {
	measDC = getDC(0)
	defer releaseDC(0, measDC)

	const md = "# 标题一 H1\n" +
		"## 标题二 H2\n" +
		"### H3 `代码` 混合\n" +
		"\n" +
		"普通段落，含 **粗体**、*斜体*、~~删除线~~ 与 [链接](https://example.com/path?q=1&x=2)。\n" +
		"\n" +
		"> 引用块第一行\n" +
		"> 第二行，带 `内联代码`\n" +
		"\n" +
		"- 无序列表项一\n" +
		"- 列表项二\n" +
		"  - 嵌套一\n" +
		"  - 嵌套二\n" +
		"    1. 有序嵌套\n" +
		"- [ ] 未完成任务\n" +
		"- [x] 已完成任务\n" +
		"\n" +
		"```go\n" +
		"func main() {\n" +
		"    fmt.Println(\"hello\")\n" +
		"}\n" +
		"```\n" +
		"\n" +
		"| 列A | 列B | 列C |\n" +
		"|-----|----:|----:|\n" +
		"| 1   | 2  | 3  |\n" +
		"| a   | b  | c  |\n" +
		"\n" +
		"![一张图](https://img.example.com/x.png)\n" +
		"\n" +
		"自动链接 <https://auto.example.com> 与裸链 www.example.com。\n" +
		"\n" +
		"原始 HTML <div>test</div> 忽略。\n" +
		"\n" +
		"超长无空格链接行 https://very-long-domain.example.com/a/b/c/d/e/f/g/h/i/j/k/l/m/n/o/p/q/r/s/t/u/v/w/x/y/z/end\n" +
		"\n" +
		"---\n" +
		"\n" +
		"结尾段落。\n"

	doc := ParseMarkdown([]byte(md), "stress.md")
	W := int32(900)
	doc.layout(W)

	// 创建内存 DC + DIB 走真实 paint 路径
	hdcmem := createCompatibleDC(measDC)
	defer deleteDC(hdcmem)
	var hdr BITMAPINFOHEADER
	hdr.BiSize = uint32(unsafe.Sizeof(hdr))
	hdr.BiWidth = W
	hdr.BiHeight = -1400
	hdr.BiPlanes = 1
	hdr.BiBitCount = 32
	var bits uintptr
	bm := createDIBSection(hdcmem, &hdr, 0, &bits, 0, 0)
	if bm == 0 || bits == 0 {
		t.Fatal("createDIBSection failed")
	}
	defer deleteObject(HGDIOBJ(bm))
	old := selectObject(hdcmem, HGDIOBJ(bm))
	defer selectObject(hdcmem, old)

	// 多滚动位置都画一遍
	for s := int32(0); s < doc.contentHeight; s += 200 {
		doc.paint(hdcmem, s, W, 1400)
	}
	doc.paint(hdcmem, doc.contentHeight, W, 1400) // 越界滚动

	// 命中链接
	if len(doc.linkRects) == 0 {
		t.Fatalf("期望至少命中 1 个链接矩形，实际 0")
	}
	dest := doc.hitLink(doc.linkRects[0].x+2, doc.linkRects[0].y+2)
	if dest == "" {
		t.Fatalf("hitLink 在已知链接矩形内应返回目标")
	}
	t.Logf("stress: blocks ok, lines=%d links=%d contentHeight=%d sampleDest=%q",
		len(doc.lines), len(doc.linkRects), doc.contentHeight, dest)
}

// 空文档与超简文档不应 panic
func TestRenderEmpty(t *testing.T) {
	measDC = getDC(0)
	defer releaseDC(0, measDC)
	for _, s := range []string{"", "# 仅标题", "纯文本无格式", "```\n只有代码块\n```"} {
		doc := ParseMarkdown([]byte(s), "empty.md")
		doc.layout(800)
		hdcmem := createCompatibleDC(measDC)
		var hdr BITMAPINFOHEADER
		hdr.BiSize = uint32(unsafe.Sizeof(hdr))
		hdr.BiWidth = 800
		hdr.BiHeight = -600
		hdr.BiPlanes = 1
		hdr.BiBitCount = 32
		var bits uintptr
		bm := createDIBSection(hdcmem, &hdr, 0, &bits, 0, 0)
		if bm == 0 {
			t.Fatal("createDIBSection failed")
		}
		old := selectObject(hdcmem, HGDIOBJ(bm))
		doc.paint(hdcmem, 0, 800, 600)
		selectObject(hdcmem, old)
		deleteObject(HGDIOBJ(bm))
		deleteDC(hdcmem)
	}
	t.Log("empty/simple docs painted without panic")
}
