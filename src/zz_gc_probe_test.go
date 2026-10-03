package main

import (
	"runtime"
	"testing"
)

// TestMeasureUnderGC 验证：GC 高压下 getTextExtentPoint32 结果是否稳定。
// 若 Go 指针被转成 uintptr 后对象被回收，GDI 会读到被复用的内存 -> 宽度跳变或崩溃。
func TestMeasureUnderGC(t *testing.T) {
	measDC = getDC(0)
	if measDC == 0 {
		t.Skip("no GDI")
	}
	f := getFont(uiFont, 16, false, false)
	old := selectObject(measDC, HGDIOBJ(f))
	defer selectObject(measDC, old)

	const s = "Hello, 世界! gomd markdown"
	want, _ := getTextExtentPoint32(measDC, s)

	stop := make(chan struct{})
	done := make(chan struct{})
	go func() {
		defer close(done)
		for {
			select {
			case <-stop:
				return
			default:
				runtime.GC()
			}
		}
	}()
	bad := 0
	for i := 0; i < 300000; i++ {
		got, _ := getTextExtentPoint32(measDC, s)
		if got != want {
			bad++
			if bad < 5 {
				t.Logf("第 %d 次宽度跳变: want=%d got=%d", i, want, got)
			}
		}
	}
	close(stop)
	<-done
	if bad > 0 {
		t.Fatalf("宽度不一致 %d 次（悬垂指针已复现）", bad)
	}
	t.Logf("300000 次测量宽度恒为 %d", want)
}
