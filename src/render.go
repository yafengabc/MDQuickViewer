package main

import (
	"fmt"
	"strconv"
	"strings"
	"unicode"

	"github.com/yuin/goldmark"
	"github.com/yuin/goldmark/ast"
	"github.com/yuin/goldmark/extension"
	extast "github.com/yuin/goldmark/extension/ast"
	"github.com/yuin/goldmark/text"
)

// ---------------------------------------------------------------- rendering constants
const (
	LM     int32 = 48
	RM     int32 = 48
	TM     int32 = 30
	Lead   int32 = 4
	bodyPx int   = 15
	codePx int   = 14
)

var renderScale float32 = 1.0

func effPx(b int) int {
	return int(float32(b)*renderScale + 0.5)
}

var (
	colText     = COLORREF(0x222222)
	colHead     = COLORREF(0x111111)
	colLink     = COLORREF(0x0B57D0)
	colCode     = COLORREF(0x333333)
	colCodeBg   = COLORREF(0xF4F4F4)
	colQuote    = COLORREF(0x666666)
	colQuoteBar = COLORREF(0xC8C8C8)
	colHr       = COLORREF(0xDDDDDD)
	colImgBord  = COLORREF(0xBBBBBB)
	colSel      = COLORREF(0xEAF2FB)
	uiFont      = "Microsoft YaHei"
	codeFont    = "Consolas"
)

// ---------------------------------------------------------------- fonts
type fontKey struct {
	family       string
	px           int
	bold, italic bool
}

var fontCache = map[fontKey]HFONT{}

func getFont(family string, px int, bold, italic bool) HFONT {
	k := fontKey{family, px, bold, italic}
	if f, ok := fontCache[k]; ok {
		return f
	}
	var lf LOGFONT
	lf.Height = int32(-px)
	if bold {
		lf.Weight = FW_BOLD
	} else {
		lf.Weight = FW_NORMAL
	}
	if italic {
		lf.Italic = 1
	}
	lf.Quality = CLEARTYPE_QUALITY
	lf.CharSet = DEFAULT_CHARSET
	lf.FaceName = fontFace(family)
	f := createFontIndirect(&lf)
	fontCache[k] = f
	return f
}

// ---------------------------------------------------------------- data model
type Span struct {
	Text   string
	Bold   bool
	Italic bool
	Code   bool
	Strike bool
	Link   string
	Color  COLORREF
}

type Block struct {
	kind    string
	spans   []Span
	lines   []string
	lang    string
	indent  int32
	quote   bool
	marker  string
	markerX int32
	color   COLORREF
	y0, y1  int32
	firstLn int
	table   *TableData
}

// ---------------------------------------------------------------- table model
type tableCell struct {
	spans []Span
	align int // 0 left, 1 center, 2 right
}
type tableRow struct {
	cells  []*tableCell
	y0, y1 int32
	header bool
}
type TableData struct {
	cols   int
	colX   []int32
	colW   []int32
	rows   []*tableRow
	x0, x1 int32
}

type runItem struct {
	text      string
	family    string
	px        int
	bold      bool
	italic    bool
	code      bool
	strike    bool
	color     COLORREF
	link      string
	underline bool
}

type unit struct {
	text string
	item runItem
}

type drawUnit struct {
	x    int32
	item runItem
	text string // 该绘制单元实际要画的文本（item.text 是整段，不能直接画）
}

type VisualLine struct {
	block   int
	y       int32
	height  int32
	units   []drawUnit
	marker  string
	markerX int32
	// wrap=true 表示本行是上一行的折行续行（复制时不应在行尾插入换行）
	wrap bool
}

type LinkRect struct {
	x, y, w, h int32
	dest       string
}

type Doc struct {
	src           []byte
	blocks        []*Block
	lines         []VisualLine
	linkRects     []LinkRect
	contentHeight int32
	path          string
}

// ---------------------------------------------------------------- parsing
type style struct {
	bold, italic, strike bool
	link                 string
}

func ParseMarkdown(src []byte, path string) *Doc {
	md := goldmark.New(goldmark.WithExtensions(extension.GFM))
	reader := text.NewReader(src)
	docNode := md.Parser().Parse(reader)
	d := &Doc{src: src, path: path}
	blocks := []*Block{}
	buildBlocks(docNode, src, &blocks, 0, false, false)
	d.blocks = blocks
	return d
}

func applyQuote(b *Block, quote bool) {
	if !quote {
		return
	}
	b.quote = true
	b.indent += 20
	if b.color == 0 {
		b.color = colQuote
	}
}

func buildBlocks(n ast.Node, src []byte, out *[]*Block, depth int, ordered bool, quote bool) {
	for c := n.FirstChild(); c != nil; c = c.NextSibling() {
		switch t := c.(type) {
		case *ast.Document:
			buildBlocks(c, src, out, depth, ordered, quote)
		case *ast.Heading:
			sp := []Span{}
			collectInline(t, src, style{}, &sp)
			b := &Block{kind: fmt.Sprintf("h%d", t.Level), spans: sp}
			applyQuote(b, quote)
			*out = append(*out, b)
		case *ast.Paragraph, *ast.TextBlock:
			sp := []Span{}
			collectInline(c, src, style{}, &sp)
			b := &Block{kind: "p", spans: sp}
			applyQuote(b, quote)
			*out = append(*out, b)
		case *ast.FencedCodeBlock:
			lines, lang := extractFenced(t, src)
			*out = append(*out, &Block{kind: "code", lines: lines, lang: lang})
		case *ast.CodeBlock:
			lines := extractIndented(t, src)
			*out = append(*out, &Block{kind: "code", lines: lines})
		case *ast.ThematicBreak:
			*out = append(*out, &Block{kind: "hr"})
		case *ast.Blockquote:
			buildQuote(t, src, out, depth, quote)
		case *ast.List:
			buildList(t, src, out, depth, quote)
		case *extast.Table:
			buildTable(t, src, out, quote)
		default:
			buildBlocks(c, src, out, depth, ordered, quote)
		}
	}
}

func buildQuote(bq *ast.Blockquote, src []byte, out *[]*Block, depth int, quote bool) {
	inner := []*Block{}
	buildBlocks(bq, src, &inner, depth, false, true)
	for _, ib := range inner {
		*out = append(*out, ib)
	}
}

func buildList(l *ast.List, src []byte, out *[]*Block, depth int, quote bool) {
	ordered := l.IsOrdered()
	start := l.Start
	if start == 0 {
		start = 1
	}
	i := 0
	for c := l.FirstChild(); c != nil; c = c.NextSibling() {
		li, ok := c.(*ast.ListItem)
		if !ok {
			continue
		}
		i++
		marker := "•"
		if ordered {
			marker = strconv.Itoa(start + i - 1)
		} else if depth >= 1 {
			marker = "◦"
		}
		if depth >= 2 {
			marker = "▪"
		}
		// task list checkbox?
		var taskChecked *bool
		for cc := li.FirstChild(); cc != nil; cc = cc.NextSibling() {
			if p, ok := cc.(*ast.Paragraph); ok {
				if f := p.FirstChild(); f != nil {
					if tcb, ok := f.(*extast.TaskCheckBox); ok {
						b := tcb.IsChecked
						taskChecked = &b
					}
				}
			}
		}
		if taskChecked != nil {
			if *taskChecked {
				marker = "☑"
			} else {
				marker = "☐"
			}
		}
		baseIndent := int32(depth*22 + 22)
		markerX := int32(LM) + int32(depth)*22 + 4
		for cc := li.FirstChild(); cc != nil; cc = cc.NextSibling() {
			switch t2 := cc.(type) {
			case *ast.Paragraph, *ast.TextBlock:
				sp := []Span{}
				collectInline(cc, src, style{}, &sp)
				b := &Block{kind: "p", spans: sp, indent: baseIndent, quote: quote, marker: marker, markerX: markerX, color: blockColor(quote)}
				if quote {
					b.indent += 20
				}
				*out = append(*out, b)
				marker = ""
			case *ast.List:
				buildList(t2, src, out, depth+1, quote)
			case *ast.Blockquote:
				buildQuote(t2, src, out, depth, quote)
			}
		}
	}
}

func blockColor(quote bool) COLORREF {
	if quote {
		return colQuote
	}
	return 0
}

// ---------------------------------------------------------------- tables
func buildTable(tbl *extast.Table, src []byte, out *[]*Block, quote bool) {
	td := &TableData{}
	rowCells := func(row *extast.TableRow) []*extast.TableCell {
		cells := []*extast.TableCell{}
		for x := row.FirstChild(); x != nil; x = x.NextSibling() {
			if cell, ok := x.(*extast.TableCell); ok {
				cells = append(cells, cell)
			}
		}
		return cells
	}
	// 注意：goldmark 的 NewTableHeader 会把该行的单元格直接挂到 TableHeader 下，
	// 不再保留 TableRow 包装层；所以这里同时兼容“直接是单元格”和“包了一层 Row”。
	var headerCells, bodyCells [][]*extast.TableCell
	for c := tbl.FirstChild(); c != nil; c = c.NextSibling() {
		switch tc := c.(type) {
		case *extast.TableHeader:
			cells := []*extast.TableCell{}
			for x := tc.FirstChild(); x != nil; x = x.NextSibling() {
				if cell, ok := x.(*extast.TableCell); ok {
					cells = append(cells, cell)
				} else if row, ok := x.(*extast.TableRow); ok {
					cells = append(cells, rowCells(row)...)
				}
			}
			if len(cells) > 0 {
				headerCells = append(headerCells, cells)
			}
		case *extast.TableRow:
			if cells := rowCells(tc); len(cells) > 0 {
				bodyCells = append(bodyCells, cells)
			}
		}
	}
	ncol := 0
	for _, cells := range headerCells {
		if len(cells) > ncol {
			ncol = len(cells)
		}
	}
	for _, cells := range bodyCells {
		if len(cells) > ncol {
			ncol = len(cells)
		}
	}
	if ncol == 0 {
		return
	}
	td.cols = ncol
	buildRow := func(cells []*extast.TableCell, header bool) *tableRow {
		tr := &tableRow{header: header}
		i := 0
		for _, tc := range cells {
			sp := []Span{}
			collectInline(tc, src, style{}, &sp)
			align := 0
			if i < len(tbl.Alignments) {
				switch tbl.Alignments[i] {
				case extast.AlignCenter:
					align = 1
				case extast.AlignRight:
					align = 2
				}
			}
			tr.cells = append(tr.cells, &tableCell{spans: sp, align: align})
			i++
		}
		for i < ncol {
			tr.cells = append(tr.cells, &tableCell{spans: []Span{}, align: 0})
			i++
		}
		return tr
	}
	for _, cells := range headerCells {
		td.rows = append(td.rows, buildRow(cells, true))
	}
	for _, cells := range bodyCells {
		td.rows = append(td.rows, buildRow(cells, false))
	}
	b := &Block{kind: "table", table: td}
	applyQuote(b, quote)
	*out = append(*out, b)
}

func cellRunItems(spans []Span, bold bool) []runItem {
	px := effPx(bodyPx)
	items := []runItem{}
	for _, s := range spans {
		it := runItem{text: s.Text, family: uiFont, px: px, bold: bold}
		if s.Bold {
			it.bold = true
		}
		if s.Italic {
			it.italic = true
		}
		if s.Strike {
			it.strike = true
		}
		if s.Code {
			it.family = codeFont
			it.px = effPx(codePx)
		}
		if s.Link != "" {
			it.link = s.Link
			it.color = colLink
			it.underline = true
		}
		if s.Color != 0 {
			it.color = s.Color
		}
		items = append(items, it)
	}
	return items
}

func (d *Doc) doTable(b *Block, width, y0 int32) int32 {
	td := b.table
	if td == nil || td.cols == 0 {
		return y0
	}
	x0 := LM + b.indent
	avail := width - LM - RM - b.indent
	if avail < 200 {
		avail = 200
	}
	pad := int32(8)
	ncol := td.cols
	nat := make([]int32, ncol)
	for i := range nat {
		nat[i] = 40
	}
	for _, row := range td.rows {
		for ci, cell := range row.cells {
			if ci >= ncol {
				continue
			}
			items := cellRunItems(cell.spans, row.header)
			w := int32(0)
			for _, it := range items {
				w += measureItem(it, it.text)
			}
			w += 2 * pad
			if w > nat[ci] {
				nat[ci] = w
			}
		}
	}
	total := int32(0)
	for _, w := range nat {
		total += w
	}
	colW := make([]int32, ncol)
	if total <= avail {
		copy(colW, nat)
	} else {
		scale := float32(avail) / float32(total)
		for i, w := range nat {
			cw := int32(float32(w)*scale + 0.5)
			if cw < 40 {
				cw = 40
			}
			colW[i] = cw
		}
	}
	td.colW = colW
	td.colX = make([]int32, ncol)
	cx := x0
	for i := 0; i < ncol; i++ {
		td.colX[i] = cx
		cx += colW[i]
	}
	td.x0 = x0
	td.x1 = cx
	y := y0
	for _, row := range td.rows {
		row.y0 = y
		maxH := int32(0)
		for ci, cell := range row.cells {
			if ci >= ncol {
				continue
			}
			items := cellRunItems(cell.spans, row.header)
			cw := colW[ci]
			contentW := cw - 2*pad
			if contentW < 20 {
				contentW = 20
			}
			contentX := td.colX[ci] + pad
			cy := y + pad
			lr0 := len(d.linkRects)
			firstIdx := len(d.lines)
			by := d.wrapCell(items, contentX, contentW, cy)
			// per-visual-line alignment shift (center/right)
			yoff := map[int32]int32{}
			for i := firstIdx; i < len(d.lines); i++ {
				vl := &d.lines[i]
				if len(vl.units) == 0 {
					continue
				}
				firstX := vl.units[0].x
				lastU := vl.units[len(vl.units)-1]
				lastW := measureItem(lastU.item, lastU.text)
				lineW := (lastU.x + lastW) - firstX
				off := int32(0)
				if cell.align == 1 {
					off = (contentW - lineW) / 2
				} else if cell.align == 2 {
					off = contentW - lineW
				}
				if off != 0 {
					for j := range vl.units {
						vl.units[j].x += off
					}
					yoff[vl.y] = off
				}
			}
			for k := lr0; k < len(d.linkRects); k++ {
				if off, ok := yoff[d.linkRects[k].y]; ok {
					d.linkRects[k].x += off
				}
			}
			h := (by - cy) + 2*pad
			if h > maxH {
				maxH = h
			}
		}
		row.y1 = y + maxH
		y = row.y1
	}
	return y
}

func (d *Doc) wrapCell(items []runItem, x0, avail, y0 int32) int32 {
	units := []unit{}
	for _, it := range items {
		units = append(units, splitUnits(it)...)
	}
	return d.wrapUnits(units, &Block{}, -1, x0, avail, y0)
}

func extractFenced(n *ast.FencedCodeBlock, src []byte) ([]string, string) {
	lang := string(n.Language(src))
	var lines []string
	for i := 0; i < n.Lines().Len(); i++ {
		seg := n.Lines().At(i)
		lines = append(lines, string(seg.Value(src)))
	}
	return lines, lang
}

func extractIndented(n *ast.CodeBlock, src []byte) []string {
	var lines []string
	for i := 0; i < n.Lines().Len(); i++ {
		seg := n.Lines().At(i)
		lines = append(lines, string(seg.Value(src)))
	}
	return lines
}

// ---------------------------------------------------------------- inline collection
func collectInline(n ast.Node, src []byte, st style, out *[]Span) {
	for c := n.FirstChild(); c != nil; c = c.NextSibling() {
		switch t := c.(type) {
		case *ast.Text:
			s := Span{Text: string(src[t.Segment.Start:t.Segment.Stop]), Bold: st.bold, Italic: st.italic, Strike: st.strike, Link: st.link}
			if t.HardLineBreak() {
				s.Text += "\n"
			} else if t.SoftLineBreak() {
				s.Text += " "
			}
			*out = append(*out, s)
		case *ast.String:
			*out = append(*out, Span{Text: string(t.Value), Bold: st.bold, Italic: st.italic, Strike: st.strike, Link: st.link})
		case *ast.CodeSpan:
			txt := codeSpanText(t, src)
			*out = append(*out, Span{Text: txt, Code: true, Link: st.link})
		case *ast.Emphasis:
			ns := st
			if t.Level == 2 {
				ns.bold = true
			} else if t.Level == 1 {
				ns.italic = true
			}
			collectInline(c, src, ns, out)
		case *ast.Link:
			ns := st
			ns.link = string(t.Destination)
			collectInline(c, src, ns, out)
		case *ast.AutoLink:
			*out = append(*out, Span{Text: string(t.URL(src)), Link: string(t.URL(src))})
		case *ast.Image:
			alt := plainText(c, src)
			*out = append(*out, Span{Text: "📷 " + alt, Link: string(t.Destination)})
		case *extast.Strikethrough:
			ns := st
			ns.strike = true
			collectInline(c, src, ns, out)
		default:
			collectInline(c, src, st, out)
		}
	}
}

func plainText(n ast.Node, src []byte) string {
	var sb strings.Builder
	var walk func(ast.Node)
	walk = func(c ast.Node) {
		for x := c.FirstChild(); x != nil; x = x.NextSibling() {
			switch t := x.(type) {
			case *ast.Text:
				sb.WriteString(string(src[t.Segment.Start:t.Segment.Stop]))
			case *ast.String:
				sb.WriteString(string(t.Value))
			case *ast.CodeSpan:
				sb.WriteString(codeSpanText(t, src))
			default:
				walk(x)
			}
		}
	}
	walk(n)
	return sb.String()
}

func codeSpanText(n *ast.CodeSpan, src []byte) string {
	var sb strings.Builder
	for c := n.FirstChild(); c != nil; c = c.NextSibling() {
		if t, ok := c.(*ast.Text); ok {
			sb.WriteString(string(t.Segment.Value(src)))
		}
	}
	return sb.String()
}

// ---------------------------------------------------------------- layout
func (d *Doc) layout(width int32) {
	d.lines = nil
	d.linkRects = nil
	y := TM
	for i, b := range d.blocks {
		b.y0 = y
		switch b.kind {
		case "hr":
			y += 16
		case "code":
			y = d.doCode(b, i, width, y)
		case "table":
			y = d.doTable(b, width, y)
		default:
			y = d.doText(b, i, width, y)
		}
		b.y1 = y
		y += blockGap(b)
	}
	d.contentHeight = y + 24
}

func blockGap(b *Block) int32 {
	switch b.kind {
	case "h1":
		return 20
	case "h2":
		return 16
	case "h3":
		return 14
	case "h4", "h5", "h6":
		return 12
	case "hr":
		return 14
	case "code":
		return 16
	default:
		return 10
	}
}

func headingPx(kind string) int {
	switch kind {
	case "h1":
		return effPx(26)
	case "h2":
		return effPx(22)
	case "h3":
		return effPx(18)
	case "h4":
		return effPx(16)
	case "h5":
		return effPx(15)
	case "h6":
		return effPx(14)
	}
	return effPx(bodyPx)
}

func spansToItems(b *Block) []runItem {
	px := headingPx(b.kind)
	items := []runItem{}
	for _, s := range b.spans {
		it := runItem{text: s.Text, family: uiFont, px: px, bold: s.Bold, italic: s.Italic, strike: s.Strike}
		if b.kind != "p" {
			it.bold = true
		}
		if b.kind == "h6" {
			it.italic = true
		}
		if s.Code {
			it.family = codeFont
			it.px = effPx(codePx)
		}
		if s.Link != "" {
			it.link = s.Link
			it.color = colLink
			it.underline = true
		}
		if s.Color != 0 {
			it.color = s.Color
		}
		items = append(items, it)
	}
	return items
}

func (d *Doc) doText(b *Block, idx int, width, y0 int32) int32 {
	items := spansToItems(b)
	avail := width - LM - RM - b.indent
	if avail < 80 {
		avail = 80
	}
	x0 := LM + b.indent
	units := []unit{}
	for _, it := range items {
		units = append(units, splitUnits(it)...)
	}
	return d.wrapUnits(units, b, idx, x0, avail, y0)
}

// sameItem reports whether two runItems should be drawn as one continuous run
// (so we keep kerning and avoid per-glyph advance rounding drift).
func sameItem(a, b runItem) bool {
	return a.family == b.family && a.px == b.px && a.bold == b.bold &&
		a.italic == b.italic && a.code == b.code && a.strike == b.strike &&
		a.color == b.color && a.link == b.link && a.underline == b.underline
}

func (d *Doc) doCode(b *Block, idx int, width, y0 int32) int32 {
	avail := width - LM - RM - b.indent
	if avail < 80 {
		avail = 80
	}
	x0 := LM + b.indent
	y := y0
	for _, line := range b.lines {
		line = strings.TrimRight(line, "\r\n")
		toks := codeTokens(line)
		var curUnits []drawUnit
		curX := x0
		lineH := int32(effPx(codePx) * 3 / 2)
		firstIdx := -1
		isWrap := false
		flush := func() {
			if len(curUnits) == 0 {
				return
			}
			vl := VisualLine{block: idx, y: y, height: lineH, units: curUnits, wrap: isWrap}
			if firstIdx < 0 {
				firstIdx = len(d.lines)
			}
			d.lines = append(d.lines, vl)
			y += lineH + 2
			curX = x0
			curUnits = nil
			isWrap = false
		}
		for _, tk := range toks {
			it := runItem{text: tk, family: codeFont, px: codePx, code: true, color: colCode}
			w := measureItem(it, tk)
			if curX+w > x0+avail && curX > x0 {
				isWrap = true
				flush()
			}
			if n := len(curUnits); n > 0 && sameItem(curUnits[n-1].item, it) {
				curUnits[n-1].text += tk
			} else {
				curUnits = append(curUnits, drawUnit{x: curX, item: it, text: tk})
			}
			curX += w
		}
		flush()
	}
	return y
}

func (d *Doc) wrapUnits(units []unit, b *Block, idx int, x0, avail, y0 int32) int32 {
	y := y0
	curX := x0
	var curUnits []drawUnit
	lineH := int32(0)
	firstIdx := -1
	isWrap := false
	flush := func() {
		if len(curUnits) == 0 {
			return
		}
		vl := VisualLine{block: idx, y: y, height: lineH, units: curUnits, wrap: isWrap}
		if firstIdx < 0 {
			firstIdx = len(d.lines)
			vl.marker = b.marker
			vl.markerX = b.markerX
		}
		d.lines = append(d.lines, vl)
		// link rects
		for _, du := range curUnits {
			if du.item.link != "" {
				w := measureItem(du.item, du.text)
				d.linkRects = append(d.linkRects, LinkRect{x: du.x, y: y, w: w, h: lineH, dest: du.item.link})
			}
		}
		y += lineH + Lead
		curX = x0
		curUnits = nil
		lineH = 0
		isWrap = false
	}
	for _, u := range units {
		if u.text == "\n" {
			flush() // 源码里的换行：新行（不是折行续行）
			continue
		}
		w := measureItem(u.item, u.text)
		if curX+w > x0+avail && curX > x0 {
			isWrap = true
			flush()
		}
		if n := len(curUnits); n > 0 && sameItem(curUnits[n-1].item, u.item) {
			curUnits[n-1].text += u.text
		} else {
			curUnits = append(curUnits, drawUnit{x: curX, item: u.item, text: u.text})
		}
		curX += w
		lh := int32(u.item.px*3/2 + 2)
		if lh > lineH {
			lineH = lh
		}
	}
	flush()
	b.firstLn = firstIdx
	return y
}

// ---------------------------------------------------------------- unit splitting
func isCJK(r rune) bool {
	if r >= 0x3000 && r <= 0x303F {
		return true
	}
	if r >= 0x3040 && r <= 0x309F {
		return true
	}
	if r >= 0x30A0 && r <= 0x30FF {
		return true
	}
	if r >= 0x3400 && r <= 0x4DBF {
		return true
	}
	if r >= 0x4E00 && r <= 0x9FFF {
		return true
	}
	if r >= 0xAC00 && r <= 0xD7AF {
		return true
	}
	if r >= 0xF900 && r <= 0xFAFF {
		return true
	}
	if r >= 0xFF00 && r <= 0xFFEF {
		return true
	}
	return false
}

func splitUnits(it runItem) []unit {
	var us []unit
	if it.code {
		// keep spaces as visible units, break long tokens hard
		toks := codeTokens(it.text)
		for _, tk := range toks {
			us = append(us, unit{text: tk, item: it})
		}
		return us
	}
	var buf strings.Builder
	flush := func() {
		if buf.Len() > 0 {
			us = append(us, unit{text: buf.String(), item: it})
			buf.Reset()
		}
	}
	for _, r := range it.text {
		if r == '\n' {
			flush()
			us = append(us, unit{text: "\n", item: it})
			continue
		}
		if unicode.IsSpace(r) {
			flush()
			us = append(us, unit{text: string(r), item: it})
			continue
		}
		if isCJK(r) {
			flush()
			us = append(us, unit{text: string(r), item: it})
			continue
		}
		buf.WriteRune(r)
	}
	flush()
	return us
}

func codeTokens(s string) []string {
	var toks []string
	i := 0
	n := len(s)
	for i < n {
		if s[i] == ' ' || s[i] == '\t' {
			j := i
			for j < n && (s[j] == ' ' || s[j] == '\t') {
				j++
			}
			toks = append(toks, s[i:j])
			i = j
			continue
		}
		j := i
		for j < n && s[j] != ' ' && s[j] != '\t' {
			j++
		}
		toks = append(toks, s[i:j])
		i = j
	}
	if len(toks) == 0 {
		toks = append(toks, "")
	}
	return toks
}

// ---------------------------------------------------------------- measuring
var measDC HDC

func measureItem(it runItem, text string) int32 {
	if text == "" {
		return 0
	}
	f := getFont(it.family, it.px, it.bold, it.italic)
	old := selectObject(measDC, f)
	w, _ := getTextExtentPoint32(measDC, text)
	selectObject(measDC, old)
	return w
}

// ---------------------------------------------------------------- painting
func (d *Doc) paint(hdc HDC, scroll, width, height int32) {
	// background
	rc := RECT{0, 0, width, height}
	brush := createSolidBrush(COLORREF(0xFFFFFF))
	fillRect(hdc, &rc, brush)
	deleteObject(HGDIOBJ(brush))

	// block backgrounds
	for _, b := range d.blocks {
		if b.y1 < scroll || b.y0 > scroll+height {
			continue
		}
		if b.kind == "code" {
			r := RECT{LM - 12, b.y0 - 8, width - RM + 12, b.y1 + 8}
			cb := createSolidBrush(colCodeBg)
			fillRect(hdc, &r, cb)
			deleteObject(HGDIOBJ(cb))
			pen := createPen(PS_SOLID, 1, colQuoteBar)
			old := selectObject(hdc, HGDIOBJ(pen))
			moveToEx(hdc, LM-12, b.y0-8, nil)
			lineTo(hdc, LM-12, b.y1+8)
			selectObject(hdc, old)
			deleteObject(HGDIOBJ(pen))
		}
		if b.quote {
			r := RECT{LM + 4, b.y0 - 2, LM + 9, b.y1 + 2}
			qb := createSolidBrush(colQuoteBar)
			fillRect(hdc, &r, qb)
			deleteObject(HGDIOBJ(qb))
		}
		if b.kind == "table" && b.table != nil && len(b.table.rows) > 0 &&
			len(b.table.colX) >= b.table.cols && b.table.rows[0].header {
			hdr := b.table.rows[0]
			if !(hdr.y1 < scroll || hdr.y0 > scroll+height) {
				r := RECT{b.table.x0, hdr.y0 - scroll, b.table.x1, hdr.y1 - scroll}
				hb := createSolidBrush(COLORREF(0xF2F4F7))
				fillRect(hdc, &r, hb)
				deleteObject(HGDIOBJ(hb))
			}
		}
	}

	setBkMode(hdc, TRANSPARENT)

	// 文本选择高亮（画在文字下方，保证文字可见）
	if rects := d.selectionRects(scroll); len(rects) > 0 {
		sb := createSolidBrush(colSel)
		for _, r := range rects {
			fillRect(hdc, &r, sb)
		}
		deleteObject(HGDIOBJ(sb))
	}

	// text
	for _, vl := range d.lines {
		yTop := vl.y - scroll
		if yTop > height || yTop+vl.height < 0 {
			continue
		}
		if vl.marker != "" {
			f := getFont(uiFont, bodyPx, true, false)
			old := selectObject(hdc, HGDIOBJ(f))
			setTextColor(hdc, colHead)
			textOut(hdc, vl.markerX, yTop, vl.marker)
			selectObject(hdc, old)
		}
		for _, du := range vl.units {
			f := getFont(du.item.family, du.item.px, du.item.bold, du.item.italic)
			old := selectObject(hdc, HGDIOBJ(f))
			col := du.item.color
			if col == 0 {
				col = colText
			}
			if du.item.link != "" {
				col = colLink
			}
			setTextColor(hdc, col)
			textOut(hdc, du.x, yTop, du.text)
			selectObject(hdc, old)
			if du.item.link != "" {
				w := measureItem(du.item, du.text)
				pen := createPen(PS_SOLID, 1, colLink)
				po := selectObject(hdc, HGDIOBJ(pen))
				moveToEx(hdc, du.x, yTop+vl.height-3, nil)
				lineTo(hdc, du.x+w, yTop+vl.height-3)
				selectObject(hdc, po)
				deleteObject(HGDIOBJ(pen))
			}
			if du.item.strike {
				w := measureItem(du.item, du.text)
				pen := createPen(PS_SOLID, 1, col)
				po := selectObject(hdc, HGDIOBJ(pen))
				mid := yTop + vl.height/2
				moveToEx(hdc, du.x, mid, nil)
				lineTo(hdc, du.x+w, mid)
				selectObject(hdc, po)
				deleteObject(HGDIOBJ(pen))
			}
		}
	}

	// horizontal rules
	for _, b := range d.blocks {
		if b.kind != "hr" {
			continue
		}
		if b.y0 < scroll || b.y0 > scroll+height {
			continue
		}
		yc := b.y0 - scroll + 8
		pen := createPen(PS_SOLID, 1, colHr)
		old := selectObject(hdc, HGDIOBJ(pen))
		moveToEx(hdc, LM, yc, nil)
		lineTo(hdc, width-RM, yc)
		selectObject(hdc, old)
		deleteObject(HGDIOBJ(pen))
	}

	// table grids
	for _, b := range d.blocks {
		if b.kind != "table" || b.table == nil {
			continue
		}
		td := b.table
		if td.x1 <= td.x0 || len(td.rows) == 0 || len(td.colX) < td.cols {
			continue
		}
		if td.rows[len(td.rows)-1].y1 < scroll || td.rows[0].y0 > scroll+height {
			continue
		}
		top := td.rows[0].y0 - scroll
		bot := td.rows[len(td.rows)-1].y1 - scroll
		pen := createPen(PS_SOLID, 1, COLORREF(0xDDDDDD))
		po := selectObject(hdc, HGDIOBJ(pen))
		// horizontal borders (top+bottom of every row)
		for _, row := range td.rows {
			yc := row.y0 - scroll
			moveToEx(hdc, td.x0, yc, nil)
			lineTo(hdc, td.x1, yc)
			yc2 := row.y1 - scroll
			moveToEx(hdc, td.x0, yc2, nil)
			lineTo(hdc, td.x1, yc2)
		}
		// vertical separators
		for i := 0; i < td.cols; i++ {
			moveToEx(hdc, td.colX[i], top, nil)
			lineTo(hdc, td.colX[i], bot)
		}
		moveToEx(hdc, td.x1, top, nil)
		lineTo(hdc, td.x1, bot)
		// darker line under header
		if td.rows[0].header {
			hpen := createPen(PS_SOLID, 1, COLORREF(0xBBBBBB))
			hpo := selectObject(hdc, HGDIOBJ(hpen))
			yc := td.rows[0].y1 - scroll
			moveToEx(hdc, td.x0, yc, nil)
			lineTo(hdc, td.x1, yc)
			selectObject(hdc, hpo)
			deleteObject(HGDIOBJ(hpen))
		}
		selectObject(hdc, po)
		deleteObject(HGDIOBJ(pen))
	}
}

// hitLink returns the destination of a link at content coordinate (x,y), or "".
func (d *Doc) hitLink(x, y int32) string {
	for _, lr := range d.linkRects {
		if x >= lr.x && x <= lr.x+lr.w && y >= lr.y && y <= lr.y+lr.h {
			return lr.dest
		}
	}
	return ""
}
