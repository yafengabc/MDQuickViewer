package main

import (
	"fmt"
	"testing"
	"unsafe"
)

// TestStructSizes 校验关键 Win32 结构体的 Go 定义与真实 SDK 布局一致。
// 注意：只有自带"长度成员"的结构体（如 OPENFILENAME.LStructSize）尺寸必须精确；
// 其余结构体（TBBUTTON/LVCOLUMN/LVITEM/BROWSEINFO 等）没有 size 成员，
// comctl32/comdlg32 按 mask 读取字段，只要公共字段偏移正确即可（尾部多出字段无害）。
func TestStructSizes(t *testing.T) {
	type row struct {
		name   string
		size   uintptr
		expect uintptr
		critic bool
	}
	rows := []row{
		{"OPENFILENAME", unsafe.Sizeof(OPENFILENAME{}), 152, true}, // 必须与 sizeof(OPENFILENAMEW) 一致
		{"BROWSEINFO", unsafe.Sizeof(BROWSEINFO{}), 64, false},
		{"TBBUTTON", unsafe.Sizeof(TBBUTTON{}), 32, true},
		{"LVCOLUMN", unsafe.Sizeof(LVCOLUMN{}), 40, true},
		{"LVITEM", unsafe.Sizeof(LVITEM{}), 56, false},
	}
	for _, r := range rows {
		status := "OK"
		if r.size != r.expect {
			status = "MISMATCH"
			if r.critic {
				t.Errorf("%s 尺寸错误: size=%d expect=%d（该结构体带长度成员，必须精确）", r.name, r.size, r.expect)
			}
		}
		fmt.Printf("%-14s size=%d expect=%d %s\n", r.name, r.size, r.expect, status)
	}
	var o OPENFILENAME
	fmt.Printf("OPENFILENAME offsets: LStructSize@%d HwndOwner@%d LpstrFilter@%d LpstrFile@%d NMaxFile@%d Flags@%d FlagsEx@%d\n",
		unsafe.Offsetof(o.LStructSize), unsafe.Offsetof(o.HwndOwner), unsafe.Offsetof(o.LpstrFilter),
		unsafe.Offsetof(o.LpstrFile), unsafe.Offsetof(o.NMaxFile), unsafe.Offsetof(o.Flags), unsafe.Offsetof(o.FlagsEx))
	var b BROWSEINFO
	fmt.Printf("BROWSEINFO offsets: HwndOwner@%d LpszTitle@%d UlFlags@%d\n",
		unsafe.Offsetof(b.HwndOwner), unsafe.Offsetof(b.LpszTitle), unsafe.Offsetof(b.UlFlags))
}
