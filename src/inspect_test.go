package main

import (
	"fmt"
	"strings"
	"testing"

	"github.com/yuin/goldmark"
	"github.com/yuin/goldmark/ast"
	"github.com/yuin/goldmark/extension"
	extast "github.com/yuin/goldmark/extension/ast"
	"github.com/yuin/goldmark/text"
)

func TestTableAST(t *testing.T) {
	md := "| 名字 | 数量 | 备注 |\n|---|:---:|---:|\n| 苹果 | 3 | 红富士 |\n| 香蕉 | 5 | 进口 |\n"
	doc := goldmark.New(goldmark.WithExtensions(extension.GFM)).Parser().Parse(text.NewReader([]byte(md)))

	// print column alignments from the Table node
	var walk func(n ast.Node, d int)
	walk = func(n ast.Node, d int) {
		name := strings.TrimPrefix(fmt.Sprintf("%T", n), "*extension/ast.")
		extra := ""
		switch x := n.(type) {
		case *extast.Table:
			extra = fmt.Sprintf(" cols=%d alignments=%v", len(x.Alignments), x.Alignments)
		case *extast.TableCell:
			extra = fmt.Sprintf(" align=%v", x.Alignment)
		}
		t.Logf("%s%s%s", strings.Repeat("  ", d), name, extra)
		for c := n.FirstChild(); c != nil; c = c.NextSibling() {
			walk(c, d+1)
		}
	}
	walk(doc, 0)
}

func spanTexts(sp []Span) []string {
	out := []string{}
	for _, s := range sp {
		out = append(out, s.Text)
	}
	return out
}

// TestTableParse 验证 buildBlocks 能把 GFM 表格解析成 Block{kind:"table"}。
// 这一段只依赖 goldmark（纯 Go），不需要 GDI，可在任意平台运行。
func TestTableParse(t *testing.T) {
	md := "# 标题\n\n" +
		"| 名字 | 数量 | 备注 |\n" +
		"|---|:---:|---:|\n" +
		"| 苹果 | 3 | 红富士 |\n" +
		"| 香蕉 | 5 | 进口 |\n\n" +
		"结尾段落。\n"

	d := ParseMarkdown([]byte(md), "")
	kinds := []string{}
	for _, b := range d.blocks {
		kinds = append(kinds, b.kind)
	}
	t.Logf("blocks=%v", kinds)

	var tb *Block
	for _, b := range d.blocks {
		if b.kind == "table" {
			tb = b
			break
		}
	}
	if tb == nil {
		t.Fatal("没有生成 table 块")
	}
	td := tb.table
	if td.cols != 3 {
		t.Fatalf("cols=%d want 3", td.cols)
	}
	if len(td.rows) != 3 {
		t.Fatalf("rows=%d want 3 (1 header + 2 body)", len(td.rows))
	}
	if !td.rows[0].header {
		t.Fatal("第一行不是 header 行")
	}
	if td.rows[1].header || td.rows[2].header {
		t.Fatal("body 行被误判为 header")
	}

	want := [][]string{{"苹果", "3", "红富士"}, {"香蕉", "5", "进口"}}
	for ri, row := range td.rows[1:] {
		for ci, cell := range row.cells {
			got := strings.Join(spanTexts(cell.spans), "")
			if got != want[ri][ci] {
				t.Errorf("row %d col %d = %q, want %q", ri, ci, got, want[ri][ci])
			}
		}
	}

	al := []int{}
	for _, c := range td.rows[0].cells {
		al = append(al, c.align)
	}
	t.Logf("列对齐=%v (0=left 1=center 2=right)", al)
	if al[0] != 0 || al[1] != 1 || al[2] != 2 {
		t.Errorf("对齐解析错误: %v", al)
	}
}
