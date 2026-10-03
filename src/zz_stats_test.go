package main

import (
	"os"
	"testing"
)

func TestZZStats(t *testing.T) {
	measDC = getDC(0)
	if measDC == 0 {
		t.Skip("no GDI")
	}
	data, _ := os.ReadFile(`D:/Projects/goc/docs/optimization-plan.md`)
	for _, w := range []int32{80, 200, 340, 500, 710, 743, 900} {
		d := ParseMarkdown(data, "x.md")
		d.layout(w)
		maxU := 0
		empty := 0
		for _, vl := range d.lines {
			if len(vl.units) == 0 {
				empty++
			}
			if len(vl.units) > maxU {
				maxU = len(vl.units)
			}
		}
		t.Logf("w=%-5d blocks=%-4d lines=%-5d contentH=%-8d links=%-5d maxUnits=%-4d emptyLines=%d",
			w, len(d.blocks), len(d.lines), d.contentHeight, len(d.linkRects), maxU, empty)
	}
}
