package main

import (
	"image"
	"image/color"
	"image/png"
	"os"
	"testing"
	"unsafe"
)

func TestZZDumpPNG(t *testing.T) {
	measDC = getDC(0)
	if measDC == 0 {
		t.Skip("no dc")
	}
	data, err := os.ReadFile(samplePath())
	if err != nil {
		t.Fatal(err)
	}
	d := ParseMarkdown(data, "sample.md")
	const W, H = 900, 1500
	renderScale = 1.0
	d.layout(int32(W))
	sdc := getDC(0)
	mem := createCompatibleDC(sdc)
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
	d.paint(mem, 0, int32(W), int32(H))
	selectObject(mem, old)
	pix := unsafe.Slice((*byte)(unsafe.Pointer(bits)), W*H*4)
	img := image.NewRGBA(image.Rect(0, 0, W, H))
	for y := 0; y < H; y++ {
		for x := 0; x < W; x++ {
			o := (y*W + x) * 4
			img.Set(x, y, color.RGBA{pix[o+2], pix[o+1], pix[o+0], 255})
		}
	}
	f, _ := os.Create("render_dump.png")
	defer f.Close()
	png.Encode(f, img)
	t.Logf("wrote render_dump.png lines=%d", len(d.lines))
}
