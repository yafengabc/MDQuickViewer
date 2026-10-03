package main

import (
	"math/rand"
	"os"
	"path/filepath"
	"testing"
)

func TestStressAllDocs(t *testing.T) {
	measDC = getDC(0)
	if measDC == 0 {
		t.Skip("no GDI")
	}
	files, _ := filepath.Glob(`D:/Projects/goc/docs/*.md`)
	files = append(files, samplePath())
	rnd := rand.New(rand.NewSource(99))
	for _, f := range files {
		src, err := os.ReadFile(f)
		if err != nil {
			t.Logf("skip %s: %v", f, err)
			continue
		}
		for _, w := range []int32{-5, 0, 60, 320, 700, 1400} {
			d := ParseMarkdown(src, filepath.Base(f))
			d.layout(w)
			for i := 0; i < 800; i++ {
				a := d.hitPos(int32(rnd.Intn(4000)-1200), int32(rnd.Intn(int(d.contentHeight)+2000)-800))
				b := d.hitPos(int32(rnd.Intn(4000)-1200), int32(rnd.Intn(int(d.contentHeight)+2000)-800))
				selAnchor, selHead = a, b
				selOn = !a.equal(b)
				_ = d.selectionRects(int32(rnd.Intn(3000) - 500))
				_ = d.selectedText()
				_ = d.hitLink(int32(rnd.Intn(4000)-1200), int32(rnd.Intn(int(d.contentHeight)+2000)-800))
			}
		}
		t.Logf("%s ok", filepath.Base(f))
	}
	clearSelection()
}
