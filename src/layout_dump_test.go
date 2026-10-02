package main

import (
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
)

// samplePath 返回 docs/sample.md 的绝对路径。测试进程的工作目录可能被
// GetOpenFileName 等对话框改变，不能依赖相对路径。
func samplePath() string {
	_, file, _, _ := runtime.Caller(0)
	return filepath.Join(filepath.Dir(file), "..", "docs", "sample.md")
}

// TestLayoutDump 打印 sample.md 排版后前若干视觉行的每个绘制单元的
// x / 实测宽度 / 文本，用于定位“字间距偏大”的空隙到底出在排版还是绘制。
func TestLayoutDump(t *testing.T) {
	measDC = getDC(0)
	if measDC == 0 {
		t.Skip("no GDI")
	}
	src, err := os.ReadFile(samplePath())
	if err != nil {
		t.Fatal(err)
	}
	d := ParseMarkdown(src, "sample.md")
	d.layout(700)

	for i, vl := range d.lines {
		if i >= 14 {
			break
		}
		parts := []string{}
		prevEnd := int32(-1)
		for _, du := range vl.units {
			w := measureItem(du.item, du.text)
			gap := ""
			if prevEnd >= 0 {
				gap = fmt.Sprintf("(gap=%d)", du.x-prevEnd)
			}
			txt := strings.ReplaceAll(du.text, " ", "·")
			parts = append(parts, fmt.Sprintf("%sx=%d end=%d w=%d px=%d b=%v %q",
				gap, du.x, du.x+w, w, du.item.px, du.item.bold, txt))
			prevEnd = du.x + w
		}
		t.Logf("line%02d y=%d h=%d\n      %s", i, vl.y, vl.height, strings.Join(parts, "\n      "))
	}
}

// TestMeasureCJK 对照实验：整串测量 vs 逐字测量累加，看是否因取整/字体映射产生系统性偏大。
func TestMeasureCJK(t *testing.T) {
	measDC = getDC(0)
	if measDC == 0 {
		t.Skip("no GDI")
	}
	cases := []struct {
		family string
		px     int
		bold   bool
		text   string
	}{
		{uiFont, 15, false, "这是一个用 "},
		{uiFont, 15, true, "纯 Go + Win32 API"},
		{uiFont, 15, false, " 实现的 Markdown 阅读器"},
		{uiFont, 15, false, "支持 "},
		{uiFont, 15, true, "粗体"},
		{codeFont, 14, false, "行内代码"},
		{uiFont, 15, false, "世界"},
		{uiFont, 15, false, "世"},
		{uiFont, 15, false, "界"},
		{uiFont, 15, false, " "},
	}
	for _, c := range cases {
		it := runItem{family: c.family, px: c.px, bold: c.bold}
		whole := measureItem(it, c.text)
		sum := int32(0)
		for _, u := range splitUnits(it) {
			_ = u
		}
		// 逐字符测量
		perChar := int32(0)
		for _, r := range c.text {
			perChar += measureItem(it, string(r))
		}
		t.Logf("%-9q px=%d bold=%v whole=%d perCharSum=%d", c.text, c.px, c.bold, whole, perChar)
		_ = sum
	}
}
