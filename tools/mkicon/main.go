// mkicon 生成 gomd.ico：Markdown 风格的应用图标。
// 纯 Go 像素绘制（距离场抗锯齿）+ 盒式降采样，输出 16/32/48/256 多尺寸 BMP 型 ICO。
package main

import (
	"bufio"
	"encoding/binary"
	"image"
	"image/color"
	"image/png"
	"os"
)

const S = 256 // 绘制画布

var (
	blue    = color.RGBA{59, 130, 246, 255} // #3B82F6
	bgWhite = color.RGBA{255, 255, 255, 255}
)

func main() {
	out := "gomd.ico"
	if len(os.Args) > 1 {
		out = os.Args[1]
	}
	// 调试模式：mkicon -png preview.png 输出预览图便于目视检查
	if len(os.Args) > 2 && os.Args[1] == "-png" {
		base := drawMaster()
		prev := downsample(base, 64)
		f, err := os.Create(os.Args[2])
		if err != nil {
			panic(err)
		}
		defer f.Close()
		if err := png.Encode(f, prev); err != nil {
			panic(err)
		}
		return
	}
	ico := renderICO()
	f, err := os.Create(out)
	if err != nil {
		panic(err)
	}
	w := bufio.NewWriter(f)
	writeErr := func() error {
		if err := binary.Write(w, binary.LittleEndian, ico.dir); err != nil {
			return err
		}
		for _, e := range ico.entries {
			if err := binary.Write(w, binary.LittleEndian, e.entry); err != nil {
				return err
			}
		}
		for _, e := range ico.entries {
			if _, err := w.Write(e.data); err != nil {
				return err
			}
		}
		// 注意：必须先 Flush 再 Close，否则缓冲区内的小尺寸条目会丢失
		if err := w.Flush(); err != nil {
			return err
		}
		return f.Close()
	}()
	if writeErr != nil {
		panic(writeErr)
	}
}

type icoEntry struct {
	entry [16]byte
	data  []byte
}

type icoFile struct {
	dir     [6]byte
	entries []icoEntry
}

// renderICO 先画 256x256 母版，再降采样成各尺寸并编码为 ICO 图像数据。
func renderICO() icoFile {
	base := drawMaster()
	var f icoFile
	binary.LittleEndian.PutUint16(f.dir[0:], 0)
	binary.LittleEndian.PutUint16(f.dir[2:], 1) // type: icon
	sizes := []int{16, 32, 48, 256}
	binary.LittleEndian.PutUint16(f.dir[4:], uint16(len(sizes)))

	offset := uint32(6 + 16*len(sizes))
	for _, sz := range sizes {
		img := downsample(base, sz)
		data := encodeBMP(img)
		var e icoEntry
		b := byte(sz)
		if sz == 256 {
			b = 0
		}
		e.entry[0] = b                                 // width
		e.entry[1] = b                                 // height
		e.entry[2] = 0                                 // palette
		e.entry[3] = 0                                 // reserved
		binary.LittleEndian.PutUint16(e.entry[4:], 1)  // planes
		binary.LittleEndian.PutUint16(e.entry[6:], 32) // bpp
		binary.LittleEndian.PutUint32(e.entry[8:], uint32(len(data)))
		binary.LittleEndian.PutUint32(e.entry[12:], offset)
		offset += uint32(len(data))
		e.data = data
		f.entries = append(f.entries, e)
	}
	return f
}

// drawMaster 画 256x256 母版：白底圆角方块 + 蓝色描边 + "M↓" 字形。
func drawMaster() *image.RGBA {
	img := image.NewRGBA(image.Rect(0, 0, S, S))
	half, r, bw := float64(S)/2, 46.0, 12.0
	mx, my := half, half
	for py := 0; py < S; py++ {
		for px := 0; px < S; px++ {
			x, y := float64(px)+0.5, float64(py)+0.5
			// 圆角矩形带符号距离（<0 内部）
			dx := abs(x-mx) - (half - r)
			dy := abs(y-my) - (half - r)
			ox, oy := maxF(dx, 0), maxF(dy, 0)
			d := sqrtF(ox*ox+oy*oy) - r
			if maxF(dx, dy) < 0 {
				d = maxF(dx, dy) // 内部取更大的（更靠近边）
			}
			if d > 1.5 {
				continue // 完全在外，透明
			}
			alpha := clampA(0.5 - d) // 边缘 1px 羽化
			// 描边 |d| <= bw/2
			var c color.RGBA
			switch {
			case abs(d) <= bw/2:
				c = blue
			case d < 0:
				c = bgWhite
			default:
				c = blue // 外缘
			}
			a := uint8(float64(c.A) * alpha)
			img.SetRGBA(px, py, color.RGBA{c.R, c.G, c.B, a})
		}
	}

	// "M↓" 字形：M 用三段粗折线，箭头用竖线 + 实心三角
	const y0, y1 = 78.0, 178.0
	stroke(img, 56, y0, 56, y1, 22, blue)        // M 左竖
	stroke(img, 56, y0, 100, 140, 22, blue)      // M 左斜
	stroke(img, 100, 140, 144, y0, 22, blue)     // M 右斜
	stroke(img, 144, y0, 144, y1, 22, blue)      // M 右竖
	stroke(img, 190, y0, 190, 138, 22, blue)     // 箭杆
	tri(img, 162, 138, 218, 138, 190, 182, blue) // 箭头
	return img
}

// stroke 用到线段的距离场画抗锯齿粗线。
func stroke(img *image.RGBA, x0, y0, x1, y1 float64, w float64, c color.RGBA) {
	pad := int(w/2 + 2)
	minx, maxx := int(minF(x0, x1))-pad, int(maxF(x0, x1))+pad
	miny, maxy := int(minF(y0, y1))-pad, int(maxF(y0, y1))+pad
	for py := maxy0(miny, 0); py < miny0(maxy, img.Bounds().Dy()); py++ {
		for px := maxy0(minx, 0); px < miny0(maxx, img.Bounds().Dx()); px++ {
			x, y := float64(px)+0.5, float64(py)+0.5
			d := distSeg(x, y, x0, y0, x1, y1)
			if a := clampA(w/2 + 0.5 - d); a > 0 {
				blend(img, px, py, c, a)
			}
		}
	}
}

// tri 画实心三角（扫描线式，边缘用重心坐标羽化可忽略——图标足够）。
func tri(img *image.RGBA, ax, ay, bx, by, cx, cy float64, c color.RGBA) {
	minx, maxx := int(minF(ax, minF(bx, cx)))-1, int(maxF(ax, maxF(bx, cx)))+1
	miny, maxy := int(minF(ay, minF(by, cy)))-1, int(maxF(ay, maxF(by, cy)))+1
	for py := maxy0(miny, 0); py < miny0(maxy, img.Bounds().Dy()); py++ {
		for px := maxy0(minx, 0); px < miny0(maxx, img.Bounds().Dx()); px++ {
			x, y := float64(px)+0.5, float64(py)+0.5
			w0 := edge(ax, ay, bx, by, x, y)
			w1 := edge(bx, by, cx, cy, x, y)
			w2 := edge(cx, cy, ax, ay, x, y)
			if w0 >= 0 && w1 >= 0 && w2 >= 0 || w0 <= 0 && w1 <= 0 && w2 <= 0 {
				blend(img, px, py, c, 1)
			}
		}
	}
}

func edge(ax, ay, bx, by, x, y float64) float64 {
	return (bx-ax)*(y-ay) - (by-ay)*(x-ax)
}

// blend 按 alpha 把 c 混合到 img。
func blend(img *image.RGBA, x, y int, c color.RGBA, a float64) {
	if a > 1 {
		a = 1
	}
	dst := img.RGBAAt(x, y)
	// 以白底为基底简单覆盖混合：new = c*a + dst*(1-a)
	na := float64(c.A) / 255 * a
	n := color.RGBA{
		R: uint8(float64(c.R)*na + float64(dst.R)*(1-na)),
		G: uint8(float64(c.G)*na + float64(dst.G)*(1-na)),
		B: uint8(float64(c.B)*na + float64(dst.B)*(1-na)),
		A: uint8(255 * maxF(na, float64(dst.A)/255)),
	}
	img.SetRGBA(x, y, n)
}

func downsample(src *image.RGBA, size int) *image.RGBA {
	dst := image.NewRGBA(image.Rect(0, 0, size, size))
	scale := float64(S) / float64(size)
	for y := 0; y < size; y++ {
		for x := 0; x < size; x++ {
			x0, x1 := int(float64(x)*scale), int(float64(x+1)*scale)
			y0, y1 := int(float64(y)*scale), int(float64(y+1)*scale)
			var rs, gs, bs, as, n float64
			for yy := y0; yy < y1 && yy < S; yy++ {
				for xx := x0; xx < x1 && xx < S; xx++ {
					c := src.RGBAAt(xx, yy)
					af := float64(c.A) / 255
					rs += float64(c.R) * af
					gs += float64(c.G) * af
					bs += float64(c.B) * af
					as += af
					n++
				}
			}
			if as == 0 {
				dst.SetRGBA(x, y, color.RGBA{0, 0, 0, 0})
				continue
			}
			dst.SetRGBA(x, y, color.RGBA{
				R: uint8(rs / as), G: uint8(gs / as), B: uint8(bs / as),
				A: uint8(255 * as / n),
			})
		}
	}
	return dst
}

// encodeBMP 把 RGBA 编码为 ICO 图像数据：40 字节头 + 自底向上 BGRA + 全 0 AND 掩码。
func encodeBMP(img *image.RGBA) []byte {
	w, h := img.Bounds().Dx(), img.Bounds().Dy()
	hdr := make([]byte, 40)
	binary.LittleEndian.PutUint32(hdr[0:], 40)
	binary.LittleEndian.PutUint32(hdr[4:], uint32(w))
	binary.LittleEndian.PutUint32(hdr[8:], uint32(h*2)) // XOR+AND 两倍高
	binary.LittleEndian.PutUint16(hdr[12:], 1)
	binary.LittleEndian.PutUint16(hdr[14:], 32)

	rowBytes := ((w*32 + 31) / 32) * 4
	maskRow := ((w + 31) / 32) * 4
	data := make([]byte, 0, 40+rowBytes*h+maskRow*h)
	data = append(data, hdr...)
	for y := h - 1; y >= 0; y-- { // 自底向上
		row := make([]byte, rowBytes)
		for x := 0; x < w; x++ {
			c := img.RGBAAt(x, y)
			row[x*4+0] = c.B
			row[x*4+1] = c.G
			row[x*4+2] = c.R
			row[x*4+3] = c.A
		}
		data = append(data, row...)
	}
	data = append(data, make([]byte, maskRow*h)...) // AND 掩码全 0（透明度由 alpha 通道决定）
	return data
}

// distSeg 点到线段的最短距离。
func distSeg(px, py, x0, y0, x1, y1 float64) float64 {
	vx, vy := x1-x0, y1-y0
	wx, wy := px-x0, py-y0
	len2 := vx*vx + vy*vy
	t := 0.0
	if len2 > 0 {
		t = (wx*vx + wy*vy) / len2
		if t < 0 {
			t = 0
		}
		if t > 1 {
			t = 1
		}
	}
	dx, dy := wx-t*vx, wy-t*vy
	return sqrtF(dx*dx + dy*dy)
}

// ---- 小工具 ----
func abs(v float64) float64 {
	if v < 0 {
		return -v
	}
	return v
}
func maxF(a, b float64) float64 {
	if a > b {
		return a
	}
	return b
}
func minF(a, b float64) float64 {
	if a < b {
		return a
	}
	return b
}
func sqrtF(v float64) float64 {
	x := v
	for i := 0; i < 40; i++ {
		x = (x + v/x) / 2
	}
	return x
}
func clampA(v float64) float64 {
	if v < 0 {
		return 0
	}
	if v > 1 {
		return 1
	}
	return v
}
func maxy0(a, b int) int {
	if a > b {
		return a
	}
	return b
}
func miny0(a, b int) int {
	if a < b {
		return a
	}
	return b
}
