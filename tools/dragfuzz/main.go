// dragfuzz 对真实 gomd 窗口疯狂发送随机鼠标拖选消息，用于复现偶发崩溃。
//
// 用法：
//
//	go run ./tools/dragfuzz [-exe dist/gomd_dbg.exe] [-doc 文档.md] [-n 20000]
//
// 崩溃时打印进程退出码与 stderr（调试版带控制台，panic 堆栈会打在 stderr）。
package main

import (
	"bytes"
	"flag"
	"fmt"
	"math/rand"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"time"
	"unsafe"

	"golang.org/x/sys/windows"
)

var (
	user32         = windows.NewLazySystemDLL("user32.dll")
	pFindWindowW   = user32.NewProc("FindWindowW")
	pFindWindowExW = user32.NewProc("FindWindowExW")
	pGetWindowThreadProcessId = user32.NewProc("GetWindowThreadProcessId")
	pPostMessage   = user32.NewProc("PostMessageW")
	pSetWindowPos  = user32.NewProc("SetWindowPos")
	pGetClientRect = user32.NewProc("GetClientRect")
	pUpdateWindow  = user32.NewProc("UpdateWindow")
	kernel32       = windows.NewLazySystemDLL("kernel32.dll")
	pOpenProcess   = kernel32.NewProc("OpenProcess")
	pGetExitCode   = kernel32.NewProc("GetExitCodeProcess")
	pCloseHandle   = kernel32.NewProc("CloseHandle")
)

const (
	WM_LBUTTONDOWN = 0x0201
	WM_MOUSEMOVE   = 0x0200
	WM_LBUTTONUP   = 0x0202
	STILL_ACTIVE   = 259
)

func findWindow(cls string) windows.HWND {
	p, _ := windows.UTF16PtrFromString(cls)
	h, _, _ := pFindWindowW.Call(uintptr(unsafe.Pointer(p)), 0)
	return windows.HWND(h)
}

// findWindows 用 FindWindowExW 自枚举整棵树，返回所有该类名的窗口。
func findWindows(cls string) []windows.HWND {
	p, _ := windows.UTF16PtrFromString(cls)
	var out []windows.HWND
	var walk func(parent windows.HWND, depth int)
	walk = func(parent windows.HWND, depth int) {
		if depth > 3 {
			return
		}
		// FindWindowExW(hwndParent, hwndChildAfter, class, title)
		// hwndParent=0 只搜顶层窗口；hwndChildAfter=NULL 从头开始，迭代用上一个句柄。
		var after uintptr
		for {
			h, _, _ := pFindWindowExW.Call(uintptr(parent), after, uintptr(unsafe.Pointer(p)), 0)
			if h == 0 {
				return
			}
			after = h
			out = append(out, windows.HWND(h))
			walk(windows.HWND(h), depth+1)
		}
	}
	walk(0, 0)
	return out
}

// windowPid 返回窗口所属进程 id（0 表示取不到）。
// 注意：GetWindowThreadProcessId 的 lParam 必须非空，否则只返回线程 id 而不写进程 id。
func windowPid(hwnd windows.HWND) int {
	var pid uint32
	pGetWindowThreadProcessId.Call(uintptr(hwnd), uintptr(unsafe.Pointer(&pid)))
	runtime.KeepAlive(&pid)
	return int(pid)
}

func findChild(parent windows.HWND, cls string) windows.HWND {
	p, _ := windows.UTF16PtrFromString(cls)
	h, _, _ := pFindWindowExW.Call(uintptr(parent), 0, uintptr(unsafe.Pointer(p)), 0)
	return windows.HWND(h)
}

func mk(x, y int32) uintptr {
	return uintptr(uint16(int16(x))) | uintptr(uint16(int16(y)))<<16
}

func post(h windows.HWND, msg uintptr, w, l uintptr) {
	pPostMessage.Call(uintptr(h), msg, w, l)
}

func alive(pid int) (bool, uint32) {
	const PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
	hp, _, _ := pOpenProcess.Call(PROCESS_QUERY_LIMITED_INFORMATION, 0, uintptr(pid))
	if hp == 0 {
		return false, 0
	}
	var code uint32
	pGetExitCode.Call(hp, uintptr(unsafe.Pointer(&code)))
	pCloseHandle.Call(hp)
	return code == STILL_ACTIVE, code
}

func main() {
	exe := flag.String("exe", `D:\Projects\gomd\dist\gomd_dbg.exe`, "调试版 gomd（带控制台，能打印 panic）")
	doc := flag.String("doc", `D:\Projects\goc\docs\fix-roadmap.md`, "要打开的 Markdown 文档")
	n := flag.Int("n", 20000, "随机拖动次数")
	seed := flag.Int64("seed", time.Now().UnixNano(), "随机种子")
	mode := flag.String("mode", "both", "drag | resize | both | sweep")
	wmin := flag.Int("wmin", 60, "sweep 起始宽度")
	wmax := flag.Int("wmax", 1400, "sweep 结束宽度")
	sweepH := flag.Int("sweepH", 800, "sweep 高度")
	step := flag.Int("step", 1, "sweep 步进")
	flag.Parse()

	abs, _ := filepath.Abs(*exe)
	var out bytes.Buffer
	cmd := exec.Command(abs, *doc)
	cmd.Stdout = &out
	cmd.Stderr = &out
	if err := cmd.Start(); err != nil {
		fmt.Println("启动失败:", err)
		os.Exit(1)
	}
	pid := cmd.Process.Pid
	defer func() {
		_ = cmd.Process.Kill()
		_, _ = cmd.Process.Wait()
	}()

	// 等窗口出现。必须校验窗口所属 pid == 我们启动的进程 pid，
	// 否则会连到上一轮遗留（timeout 杀掉本工具但没杀掉子进程）的窗口上，
	// 导致 A/B 实验结论完全失真。
	var preview windows.HWND
	var main windows.HWND
	for i := 0; i < 200; i++ {
		for _, m := range findWindows("GomdMainWindow") {
			if windowPid(m) != pid {
				continue
			}
			if p := findChild(m, "GomdPreview"); p != 0 {
				main, preview = m, p
			}
		}
		if preview != 0 {
			break
		}
		time.Sleep(50 * time.Millisecond)
	}
	if preview == 0 {
		fmt.Println("找不到 gomd 预览窗口；进程输出：", out.String())
		os.Exit(1)
	}
	fmt.Printf("已连接 preview hwnd=%d pid=%d，模式=%s，%d 轮（seed=%d）\n", preview, pid, *mode, *n, *seed)

	if *mode == "sweep" {
		fmt.Printf("扫描宽度 %d..%d 步进 %d，高度 %d\n", *wmin, *wmax, *step, *sweepH)
		for w := *wmin; w <= *wmax; w += *step {
			pSetWindowPos.Call(uintptr(main), 0, 100, 100, uintptr(w), uintptr(*sweepH), 0x0004)
			pUpdateWindow.Call(uintptr(main))
			if ok, code := alive(pid); !ok {
				fmt.Printf("\n!!! 宽度 %d 时崩溃，退出码=%d\n", w, code)
				fmt.Printf("---- 进程输出 ----\n%s\n------------------\n", out.String())
				os.Exit(2)
			}
		}
		fmt.Printf("扫描完成，%d..%d 全部存活\n", *wmin, *wmax)
		return
	}

	rnd := rand.New(rand.NewSource(*seed))
	var lastX0, lastY0, lastX1, lastY1 int32
	var lastW, lastH int32
	for i := 0; i < *n; i++ {
		if *mode == "resize" || *mode == "both" {
			// 来回拖动窗体大小：含极端尺寸，触发 WM_SIZE -> layoutChildren -> relayout
			lastW = int32(rnd.Intn(2400) + 60)
			lastH = int32(rnd.Intn(1800) + 60)
			pSetWindowPos.Call(uintptr(main), 0, 100, 100, uintptr(lastW), uintptr(lastH), 0x0004)
			pUpdateWindow.Call(uintptr(main))
		}
		if *mode == "drag" || *mode == "both" {
			x0 := int32(rnd.Intn(2400) - 600)
			y0 := int32(rnd.Intn(2400) - 600)
			x1 := int32(rnd.Intn(2400) - 600)
			y1 := int32(rnd.Intn(2400) - 600)
			lastX0, lastY0, lastX1, lastY1 = x0, y0, x1, y1
			post(preview, WM_LBUTTONDOWN, 1, mk(x0, y0))
			steps := 1 + rnd.Intn(6)
			for s := 0; s < steps; s++ {
				post(preview, WM_MOUSEMOVE, 1, mk(int32(rnd.Intn(2400)-600), int32(rnd.Intn(2400)-600)))
			}
			post(preview, WM_MOUSEMOVE, 1, mk(x1, y1))
			post(preview, WM_LBUTTONUP, 0, mk(x1, y1))
		}
		if i%100 == 0 {
			if ok, code := alive(pid); !ok {
				fmt.Printf("\n!!! 第 %d 轮后进程退出，退出码=%d\n", i, code)
				fmt.Printf("最后: 拖动(%d,%d)->(%d,%d) 尺寸=%dx%d\n", lastX0, lastY0, lastX1, lastY1, lastW, lastH)
				fmt.Printf("---- 进程输出 ----\n%s\n------------------\n", out.String())
				os.Exit(2)
			}
			fmt.Printf("\r已跑 %d/%d 轮…", i, *n)
		}
	}
	if ok, code := alive(pid); !ok {
		fmt.Printf("\n!!! 全部跑完但进程已退出，退出码=%d\n最后: 拖动(%d,%d)->(%d,%d) 尺寸=%dx%d\n%s\n",
			code, lastX0, lastY0, lastX1, lastY1, lastW, lastH, out.String())
		os.Exit(2)
	}
	fmt.Printf("\r已跑 %d 轮（模式=%s），进程存活，未复现崩溃。\n", *n, *mode)
}
