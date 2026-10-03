package main

import (
	"math/rand"
	"testing"
)

// TestDragStress 模拟"打开文件后来回拖动"：极端坐标拖选 + 各种视口宽度排版 + 绘制。
// 目的是逼出偶发 panic（越界索引、负宽度、空行、行尾等边界）。
func TestDragStress(t *testing.T) {
	measDC = getDC(0)
	if measDC == 0 {
		t.Skip("no GDI")
	}
	src, err := readSample()
	if err != nil {
		t.Fatal(err)
	}
	widths := []int32{-10, 0, 1, 8, 40, 120, 320, 700, 1600, 5000}
	rnd := rand.New(rand.NewSource(20261003))
	for _, w := range widths {
		d := ParseMarkdown(src, "sample.md")
		d.layout(w)
		for i := 0; i < 1500; i++ {
			// 覆盖：窗口上方(y<0)、下方(y>视口)、左侧(负x)、行尾之后、超大值
			x1 := int32(rnd.Intn(4000) - 1200)
			y1 := int32(rnd.Intn(int(d.contentHeight)+2000) - 800)
			x2 := int32(rnd.Intn(4000) - 1200)
			y2 := int32(rnd.Intn(int(d.contentHeight)+2000) - 800)
			a := d.hitPos(x1, y1)
			b := d.hitPos(x2, y2)
			selAnchor, selHead = a, b
			selOn = !a.equal(b)
			_ = d.selectionRects(int32(rnd.Intn(3000) - 500))
			_ = d.selectedText()
			_ = d.hitLink(x1, y1)
		}
		t.Logf("width=%d lines=%d contentHeight=%d ok", w, len(d.lines), d.contentHeight)
	}
	clearSelection()
}

// TestDragStressPaint 在真实内存 DC 上按随机滚动位置反复绘制带选区的帧。
func TestDragStressPaint(t *testing.T) {
	measDC = getDC(0)
	if measDC == 0 {
		t.Skip("no GDI")
	}
	mem := createCompatibleDC(measDC)
	if mem == 0 {
		t.Skip("no memdc")
	}
	defer deleteDC(mem)
	bmp := createCompatibleBitmap(measDC, 800, 600)
	if bmp == 0 {
		t.Skip("no bmp")
	}
	old := selectObject(mem, HGDIOBJ(bmp))
	defer func() {
		selectObject(mem, old)
		deleteObject(HGDIOBJ(bmp))
	}()

	src, err := readSample()
	if err != nil {
		t.Fatal(err)
	}
	d := ParseMarkdown(src, "sample.md")
	d.layout(800)
	rnd := rand.New(rand.NewSource(7))
	for i := 0; i < 400; i++ {
		selAnchor = d.hitPos(int32(rnd.Intn(900)-50), int32(rnd.Intn(int(d.contentHeight))))
		selHead = d.hitPos(int32(rnd.Intn(900)-50), int32(rnd.Intn(int(d.contentHeight))))
		selOn = !selAnchor.equal(selHead)
		scroll := int32(rnd.Intn(int(d.contentHeight) + 600))
		d.paint(mem, scroll, 800, 600)
	}
	clearSelection()
	t.Logf("paint stress ok, lines=%d", len(d.lines))
}
