package main

import (
	"os"
	"testing"
	"unsafe"
)

// TestPaintStressRealDocs 对真实文档在各种视口宽度下反复 paint，捕捉 panic。
func TestPaintStressRealDocs(t *testing.T) {
	measDC = getDC(0)
	if measDC == 0 {
		t.Skip("no GDI")
	}
	sdc := getDC(0)
	files := []string{
		`D:/Projects/goc/docs/optimization-plan.md`,
		`D:/Projects/goc/docs/c23-roadmap.md`,
		`D:/Projects/goc/docs/fix-roadmap.md`,
		samplePath(),
	}
	const W, H = 900, 700
	mem := createCompatibleDC(sdc)
	if mem == 0 {
		t.Skip("no memdc")
	}
	defer deleteDC(mem)
	var hdr BITMAPINFOHEADER
	hdr.BiSize = uint32(unsafe.Sizeof(hdr))
	hdr.BiWidth = W
	hdr.BiHeight = -H
	hdr.BiPlanes = 1
	hdr.BiBitCount = 32
	var bits uintptr
	hbm := createDIBSection(mem, &hdr, 0, &bits, 0, 0)
	old := selectObject(mem, HGDIOBJ(hbm))
	defer func() {
		selectObject(mem, old)
		deleteObject(HGDIOBJ(hbm))
	}()

	for _, f := range files {
		data, err := os.ReadFile(f)
		if err != nil {
			t.Logf("skip %s: %v", f, err)
			continue
		}
		// 覆盖真实拖动会经过的宽度序列（含极窄）
		for _, w := range []int32{80, 120, 200, 260, 300, 340, 400, 500, 620, 710, 743, 900, 1200} {
			d := ParseMarkdown(data, f)
			d.layout(w)
			for _, scroll := range []int32{0, 1, 37, d.contentHeight / 2, d.contentHeight - H, d.contentHeight, -50, d.contentHeight + 500} {
				selOn = false
				d.paint(mem, scroll, W, H)
			}
			t.Logf("%s w=%d lines=%d ok", f[len(f)-12:], w, len(d.lines))
		}
	}
	selOn = false
}
