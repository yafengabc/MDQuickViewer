package main

import "testing"

// TestSplitListPreviewNeverNegative 是"拖动窗体大小必崩"(0xC0000005)的回归测试。
//
// 历史 bug：layoutChildren 里写成
//
//	listW := app.panelW        // 列表折叠时为 0
//	if listW > cw-100 { listW = cw-100 }
//
// 窗口宽度小于约 116px 时 cw-100 为负数，而 0 > 负数 成立，
// 于是 listW 被改成负数，负宽度被交给 MoveWindow 调整 comctl32 ListView，
// 控件内部按负尺寸计算布局 → 访问违例。
//
// 这里穷举一批"极窄/极矮/正常"的客户区尺寸，断言两个宽度都非负且不超出客户区。
func TestSplitListPreviewNeverNegative(t *testing.T) {
	widths := []int32{
		-1000, -1, 0, 1, 2, 16, 60, 80, 99, 100, 101, 116, 120,
		200, 279, 280, 281, 379, 380, 381, 500, 800, 1000, 1920, 4096,
	}
	for _, showList := range []bool{false, true} {
		for _, cw := range widths {
			for _, panelW := range []int32{0, 120, 280, 600} {
				listW, prevW := splitListPreview(cw, panelW, showList)
				if listW < 0 || prevW < 0 {
					t.Fatalf("splitListPreview(cw=%d, panelW=%d, showList=%v) = (%d, %d)：宽度为负",
						cw, panelW, showList, listW, prevW)
				}
				// 负宽客户区会被内部夹到 0，比较时也要用夹后的值
				want := cw
				if want < 0 {
					want = 0
				}
				if listW+prevW > want {
					t.Fatalf("splitListPreview(cw=%d, panelW=%d, showList=%v) = (%d, %d)：合计 %d 超过客户区宽度 %d",
						cw, panelW, showList, listW, prevW, listW+prevW, want)
				}
				// 列表折叠时列表必须完全不占宽度（否则会露出一条空白栏）
				if !showList && listW != 0 {
					t.Fatalf("列表折叠时 listW 应为 0，实际 %d（cw=%d panelW=%d）", listW, cw, panelW)
				}
			}
		}
	}
}

// TestSplitListPreviewCollapsedIgnoresPanelWidth 验证列表折叠时宽度与 panelW 无关，
// 也就是 cw-100 变负数也不会影响结果（正是旧 bug 的触发条件）。
func TestSplitListPreviewCollapsedIgnoresPanelWidth(t *testing.T) {
	for _, cw := range []int32{0, 30, 60, 99, 100, 140} {
		listW, prevW := splitListPreview(cw, 280, false)
		if listW != 0 {
			t.Errorf("cw=%d：列表折叠时 listW=%d，应为 0", cw, listW)
		}
		if prevW != cw {
			t.Errorf("cw=%d：预览宽 %d，应等于客户区宽 %d", cw, prevW, cw)
		}
	}
}

// TestSplitListPreviewKeepsPreviewVisible 验证列表展开时预览区不会被挤成 0
// （窗口不够宽就整体留给列表，预览至少保留已收敛的宽度）。
func TestSplitListPreviewKeepsPreviewVisible(t *testing.T) {
	// 窗口宽裕：列表 280，预览 520
	if l, p := splitListPreview(800, 280, true); l != 280 || p != 520 {
		t.Errorf("cw=800 得到 (%d,%d)，期望 (280,520)", l, p)
	}
	// 窗口只有 150：给预览留 minPreviewW 不现实，列表收敛到 50
	if l, p := splitListPreview(150, 280, true); l != 50 || p != 100 {
		t.Errorf("cw=150 得到 (%d,%d)，期望 (50,100)", l, p)
	}
	// 窗口比 minPreviewW 还窄：预览保住已有宽度，列表让位
	if l, p := splitListPreview(60, 280, true); l < 0 || p != 60-l {
		t.Errorf("cw=60 得到 (%d,%d)，非法", l, p)
	}
}
