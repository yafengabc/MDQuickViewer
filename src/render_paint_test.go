package main

import (
	"os"
	"testing"
)

// TestRenderPaint 直接驱动最重的 GDI 绘制链路：layout + paint 在真实 DC 上，
// 覆盖不同滚动位置、缩放、链接命中，确保运行时不崩溃。
func TestRenderPaint(t *testing.T) {
	measDC = getDC(0)
	if measDC == 0 {
		t.Fatal("no device context")
	}
	data, err := os.ReadFile(samplePath())
	if err != nil {
		t.Fatal("read sample.md:", err)
	}
	d := ParseMarkdown(data, "sample.md")

	hdc := getDC(0)
	if hdc == 0 {
		t.Fatal("getDC failed")
	}

	const w = 820
	d.layout(w)
	t.Logf("contentHeight=%d lines=%d links=%d", d.contentHeight, len(d.lines), len(d.linkRects))

	// 多个滚动位置都画一遍（含顶部、中部、底部）
	total := d.contentHeight
	for _, s := range []int32{0, total / 3, total / 2, total - 10, total + 100} {
		d.paint(hdc, s, w, 600)
	}

	// 缩放：放大与重置
	renderScale = 1.5
	d.layout(w)
	d.paint(hdc, 0, w, 600)
	renderScale = 0.7
	d.layout(w)
	d.paint(hdc, 0, w, 600)
	renderScale = 1.0
	d.layout(w)

	// 链接命中：在第一个链接矩形内点击应返回非空
	if len(d.linkRects) > 0 {
		lr := d.linkRects[0]
		hit := d.hitLink(lr.x+2, lr.y+2)
		if hit == "" {
			t.Errorf("hitLink 在已知链接矩形内返回空: %+v", lr)
		} else {
			t.Logf("hitLink 命中: %s", hit)
		}
		// 空白处应返回空
		if outside := d.hitLink(-5, -5); outside != "" {
			t.Errorf("hitLink 在空白区返回非空前: %q", outside)
		}
	}
	t.Log("paint 全路径执行完毕，未崩溃")
}
