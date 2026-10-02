package main

import (
	"strings"
)

// ---------------------------------------------------------------- 文本选择
// 选择位置用 (视觉行, 行内绘制单元, 单元内 rune 偏移) 三元组表示。
// 定位全部基于已排版的 drawUnit，因此与渲染结果严格一致。

type selPos struct {
	line int32
	unit int32
	ch   int32
}

var (
	selAnchor selPos // 按下处
	selHead   selPos // 拖动/松开处
	selOn     bool   // 存在非空选区
	selDrag   bool   // 正在鼠标拖动
)

func (p selPos) less(q selPos) bool {
	if p.line != q.line {
		return p.line < q.line
	}
	if p.unit != q.unit {
		return p.unit < q.unit
	}
	return p.ch < q.ch
}

func (p selPos) equal(q selPos) bool {
	return p.line == q.line && p.unit == q.unit && p.ch == q.ch
}

func clearSelection() {
	selOn = false
	selDrag = false
	selAnchor, selHead = selPos{}, selPos{}
}

// orderedSel 返回规范化的选区起止（a <= b）。
func orderedSel() (selPos, selPos) {
	a, b := selAnchor, selHead
	if b.less(a) {
		return b, a
	}
	return a, b
}

// hitPos 把内容坐标映射为选择位置。
func (d *Doc) hitPos(x, y int32) selPos {
	n := len(d.lines)
	if n == 0 {
		return selPos{}
	}
	li := n - 1
	for i, vl := range d.lines {
		if y < vl.y {
			li = i // 落在行间隙，归到后面那行
			break
		}
		if y < vl.y+vl.height {
			li = i
			break
		}
	}
	vl := d.lines[li]
	if len(vl.units) == 0 {
		return selPos{line: int32(li)}
	}
	for i, du := range vl.units {
		w := measureItem(du.item, du.text)
		if x < du.x {
			return selPos{line: int32(li), unit: int32(i), ch: 0}
		}
		if x < du.x+w {
			return selPos{line: int32(li), unit: int32(i), ch: charOffset(du, x)}
		}
	}
	// 行尾之后
	last := len(vl.units) - 1
	return selPos{line: int32(li), unit: int32(last), ch: runeLen(vl.units[last].text)}
}

// charOffset 返回 x 落在 du 中的 rune 偏移。
// 用"前缀整串测量"而不是逐字累加，避免取整漂移导致光标错位。
func charOffset(du drawUnit, x int32) int32 {
	rs := []rune(du.text)
	prev := int32(0)
	for i := range rs {
		w := measureItem(du.item, string(rs[:i+1]))
		if x < du.x+prev+(w-prev)/2 {
			return int32(i)
		}
		prev = w
	}
	return int32(len(rs))
}

// unitXOffset 返回 du 中第 ch 个 rune 之前的 x 坐标。
func unitXOffset(du drawUnit, ch int32) int32 {
	rs := []rune(du.text)
	if ch <= 0 {
		return du.x
	}
	if int(ch) >= len(rs) {
		return du.x + measureItem(du.item, du.text)
	}
	return du.x + measureItem(du.item, string(rs[:ch]))
}

func runeLen(s string) int32 {
	return int32(len([]rune(s)))
}

// selectionRects 返回选区在屏幕坐标下的高亮矩形（每个视觉行一个）。
func (d *Doc) selectionRects(scroll int32) []RECT {
	if !selOn {
		return nil
	}
	a, b := orderedSel()
	if int(a.line) >= len(d.lines) {
		return nil
	}
	if int(b.line) >= len(d.lines) {
		b.line = int32(len(d.lines) - 1)
	}
	out := []RECT{}
	for li := a.line; li <= b.line && int(li) < len(d.lines); li++ {
		vl := d.lines[li]
		yTop := vl.y - scroll
		if len(vl.units) == 0 {
			out = append(out, RECT{LM, yTop, LM + 6, yTop + vl.height})
			continue
		}
		first, last := 0, len(vl.units)-1
		ch0, ch1 := int32(0), int32(-1) // -1 表示取到单元末尾
		if li == a.line {
			first = int(a.unit)
			ch0 = a.ch
		}
		if li == b.line {
			last = int(b.unit)
			ch1 = b.ch
		}
		if first > last {
			continue
		}
		if first >= len(vl.units) || last >= len(vl.units) {
			continue
		}
		x0 := unitXOffset(vl.units[first], ch0)
		du1 := vl.units[last]
		var x1 int32
		if ch1 < 0 || ch1 >= runeLen(du1.text) {
			x1 = du1.x + measureItem(du1.item, du1.text)
		} else {
			x1 = unitXOffset(du1, ch1)
		}
		if x1 <= x0 {
			continue
		}
		out = append(out, RECT{x0, yTop, x1, yTop + vl.height})
	}
	return out
}

// selectedText 返回选中区域的纯文本。折行续行不会插入多余换行。
func (d *Doc) selectedText() string {
	if !selOn {
		return ""
	}
	a, b := orderedSel()
	if int(a.line) >= len(d.lines) {
		return ""
	}
	var sb strings.Builder
	for li := a.line; li <= b.line && int(li) < len(d.lines); li++ {
		vl := d.lines[li]
		if li > a.line && !vl.wrap {
			sb.WriteString("\n")
		}
		for ui, du := range vl.units {
			if li == a.line && ui < int(a.unit) {
				continue
			}
			if li == b.line && ui > int(b.unit) {
				continue
			}
			rs := []rune(du.text)
			s, e := 0, len(rs)
			if li == a.line && ui == int(a.unit) {
				s = int(a.ch)
			}
			if li == b.line && ui == int(b.unit) {
				e = int(b.ch)
			}
			if s < 0 {
				s = 0
			}
			if e > len(rs) {
				e = len(rs)
			}
			if s >= e {
				continue
			}
			sb.WriteString(string(rs[s:e]))
		}
	}
	return sb.String()
}

// selectAll 选中全文。
func (d *Doc) selectAll() {
	if len(d.lines) == 0 {
		clearSelection()
		return
	}
	selAnchor = selPos{line: 0, unit: 0, ch: 0}
	li := int32(len(d.lines) - 1)
	vl := d.lines[li]
	if len(vl.units) == 0 {
		selHead = selPos{line: li, unit: 0, ch: 0}
	} else {
		u := int32(len(vl.units) - 1)
		selHead = selPos{line: li, unit: u, ch: runeLen(vl.units[u].text)}
	}
	selOn = !selAnchor.equal(selHead)
}

// ---------------------------------------------------------------- 命令层
func copySelection() {
	if app.doc == nil || !selOn {
		setStatus("没有选中的文本（用鼠标拖选，或 Ctrl+A 全选）")
		return
	}
	text := app.doc.selectedText()
	if text == "" {
		setStatus("没有选中的文本")
		return
	}
	if setClipboardText(text) {
		setStatus("已复制 " + itoa(len([]rune(text))) + " 个字符到剪贴板")
	} else {
		setStatus("复制失败：无法打开剪贴板")
	}
}

func selectAllText() {
	if app.doc == nil {
		return
	}
	app.doc.selectAll()
	invalidateRect(app.preview, nil, true)
	setStatus("已全选（Ctrl+C 复制）")
}

// itoa 自带实现，避免为此引入 strconv 依赖路径差异
func itoa(n int) string {
	if n == 0 {
		return "0"
	}
	neg := n < 0
	if neg {
		n = -n
	}
	buf := [24]byte{}
	i := len(buf)
	for n > 0 {
		i--
		buf[i] = byte('0' + n%10)
		n /= 10
	}
	if neg {
		i--
		buf[i] = '-'
	}
	return string(buf[i:])
}
