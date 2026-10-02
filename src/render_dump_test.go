package main

import (
	"encoding/binary"
	"os"
	"testing"
	"unsafe"
)

// TestRenderDump 把 paint() 的输出渲染到 32bpp 内存 DIB 并保存为 BMP，
// 用于离屏诊断真实渲染效果（叠影/错位等）。
func TestRenderDump(t *testing.T) {
	measDC = getDC(0)
	if measDC == 0 {
		t.Fatal("no device context")
	}
	data, err := os.ReadFile(samplePath())
	if err != nil {
		t.Fatal("read sample.md:", err)
	}
	d := ParseMarkdown(data, "sample.md")

	const W, H = 900, 700
	renderScale = 1.0
	d.layout(int32(W))

	sdc := getDC(0)
	if sdc == 0 {
		t.Fatal("no screen dc")
	}
	mem := createCompatibleDC(sdc)
	if mem == 0 {
		t.Fatal("no mem dc")
	}
	defer deleteDC(mem)

	var hdr BITMAPINFOHEADER
	hdr.BiSize = uint32(unsafe.Sizeof(hdr))
	hdr.BiWidth = W
	hdr.BiHeight = -H // top-down
	hdr.BiPlanes = 1
	hdr.BiBitCount = 32

	var bits uintptr
	hbm := createDIBSection(mem, &hdr, 0, &bits, 0, 0)
	if hbm == 0 || bits == 0 {
		t.Fatal("createDIBSection failed")
	}
	old := selectObject(mem, HGDIOBJ(hbm))
	d.paint(mem, 0, int32(W), int32(H))
	selectObject(mem, old)

	pix := unsafe.Slice((*byte)(unsafe.Pointer(bits)), W*H*4)
	if err := writeBMP("render_dump.bmp", W, H, pix); err != nil {
		t.Fatal("write bmp:", err)
	}
	t.Logf("layout: lines=%d contentHeight=%d -> render_dump.bmp", len(d.lines), d.contentHeight)

	// 回归断言：du.text 必须是排版时用来量宽的那段文本。
	// 历史 bug：drawUnit 丢失自身文本，paint 误画整段 item.text，导致同段文字重复叠画。
	for i, vl := range d.lines {
		for j, du := range vl.units {
			// 空 code 行会有合法的空文本 unit（item.text 也为空，画出来是空操作）。
			// 危险情形是：du.text 为空但 item.text 非空 → paint 会退化画整段文字。
			if du.text == "" && du.item.text != "" {
				t.Errorf("line %d unit %d: drawUnit 文本为空但 item.text=%q", i, j, du.item.text)
				continue
			}
			if du.text == "" {
				continue
			}
			if j+1 >= len(vl.units) {
				continue
			}
			adv := vl.units[j+1].x - du.x
			w := measureItem(du.item, du.text)
			if adv != w {
				t.Errorf("line %d unit %d: 排版步进 %d != 绘制文本宽度 %d (text=%q) → 绘制文本与排版文本不一致",
					i, j, adv, w, du.text)
			}
		}
	}
	t.Logf("layout: lines=%d contentHeight=%d；%d 行步进/宽度一致性校验通过",
		len(d.lines), d.contentHeight, len(d.lines))
}

func writeBMP(path string, w, h int, pix []byte) error {
	rowSize := w * 4
	imgSize := rowSize * h
	fh := make([]byte, 14)
	fh[0], fh[1] = 'B', 'M'
	binary.LittleEndian.PutUint32(fh[2:], uint32(54+imgSize))
	binary.LittleEndian.PutUint32(fh[10:], 54)
	ih := make([]byte, 40)
	binary.LittleEndian.PutUint32(ih[0:], 40)
	binary.LittleEndian.PutUint32(ih[4:], uint32(w))
	binary.LittleEndian.PutUint32(ih[8:], uint32(h)) // 底向上
	binary.LittleEndian.PutUint16(ih[12:], 1)
	binary.LittleEndian.PutUint16(ih[14:], 32)
	binary.LittleEndian.PutUint32(ih[20:], uint32(imgSize))
	buf := make([]byte, 0, 54+imgSize)
	buf = append(buf, fh...)
	buf = append(buf, ih...)
	for y := h - 1; y >= 0; y-- {
		buf = append(buf, pix[y*rowSize:(y+1)*rowSize]...)
	}
	return os.WriteFile(path, buf, 0o644)
}
