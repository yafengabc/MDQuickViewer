package main

import (
	"os"
	"strings"
	"testing"
)

// TestSelectionText 验证选区取文本、全选、以及坐标→位置映射的自洽性。
func TestSelectionText(t *testing.T) {
	measDC = getDC(0)
	if measDC == 0 {
		t.Skip("no GDI")
	}
	src, err := readSample()
	if err != nil {
		t.Fatal(err)
	}
	d := ParseMarkdown(src, "sample.md")
	d.layout(700)
	if len(d.lines) == 0 {
		t.Fatal("没有视觉行")
	}

	// 全选：文本应为非空，且包含文档首行标题中的关键字
	d.selectAll()
	if !selOn {
		t.Fatal("selectAll 后 selOn 应为 true")
	}
	all := d.selectedText()
	if strings.TrimSpace(all) == "" {
		t.Fatal("全选文本为空")
	}
	if !strings.Contains(all, "gomd") {
		t.Fatalf("全选文本未包含标题关键字: %.60q", all)
	}
	t.Logf("全选字符数 = %d", len([]rune(all)))

	// 单行内选择：取第 0 行，从头选到行尾，应与该行拼接文本一致
	vl := d.lines[0]
	want := ""
	for _, du := range vl.units {
		want += du.text
	}
	selAnchor = selPos{line: 0, unit: 0, ch: 0}
	selHead = selPos{line: 0, unit: int32(len(vl.units) - 1), ch: runeLen(vl.units[len(vl.units)-1].text)}
	selOn = true
	if got := d.selectedText(); got != want {
		t.Fatalf("单行选区文本不一致:\n got=%q\nwant=%q", got, want)
	}

	// 跨行选择：第 0 行到第 2 行，应包含三行内容且不含越界内容
	selAnchor = selPos{line: 0, unit: 0, ch: 0}
	selHead = selPos{line: 2, unit: int32(len(d.lines[2].units) - 1), ch: runeLen(d.lines[2].units[len(d.lines[2].units)-1].text)}
	selOn = true
	multi := d.selectedText()
	if !strings.Contains(multi, want) {
		t.Fatalf("跨行选区应包含首行内容:\n got=%.80q", multi)
	}
	t.Logf("跨行选区 = %.80q", multi)

	// 高亮矩形：跨行选择应每行至少一个矩形
	rects := d.selectionRects(0)
	if len(rects) < 2 {
		t.Fatalf("跨行选区高亮矩形应 >=2 个, got %d", len(rects))
	}
	for _, r := range rects {
		if r.Right <= r.Left {
			t.Fatalf("选中矩形宽度非法: %v", r)
		}
	}

	// 坐标 → 位置：取第 1 行中间单元的起点，映射回来应落在同一单元
	vl1 := d.lines[1]
	du := vl1.units[0]
	p := d.hitPos(du.x+2, vl1.y+vl1.height/2)
	if p.line != 1 || p.unit != 0 {
		t.Fatalf("hitPos 映射错误: got line=%d unit=%d, want line=1 unit=0", p.line, p.unit)
	}
	// 行尾之后应落在最后一个单元末尾
	lastU := vl1.units[len(vl1.units)-1]
	pend := d.hitPos(lastU.x+measureItem(lastU.item, lastU.text)+50, vl1.y+vl1.height/2)
	if pend.line != 1 || pend.unit != int32(len(vl1.units)-1) {
		t.Fatalf("行尾 hitPos 错误: got %+v", pend)
	}

	clearSelection()
	if d.selectedText() != "" {
		t.Fatal("清除选区后仍返回文本")
	}
}

// readSample 读取示例文档（路径不依赖工作目录）。
func readSample() ([]byte, error) {
	return os.ReadFile(samplePath())
}
