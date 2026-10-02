# gomd 项目长期笔记

纯 Go + `golang.org/x/sys/windows`（v0.48，已移除 `windows.Syscall*` 与多数类型，需本地补类型别名）实现 Win32 原生 GUI 的 Markdown 浏览器。解析用 goldmark+GFM，预览区 GDI 自绘。

## 必踩的坑 / 复用经验
- **消息循环必须 `runtime.LockOSThread()`**（main 开头），否则界面"无响应"。
- **manifest 启用现代外观**：`gomd.manifest` + `gomd.rc` → `windres gomd.rc -o gomd.syso`（本机 `/d/msys64/ucrt64/bin/windres.exe`），Go 自动链接。内容需含 `Microsoft.Windows.Common-Controls` v6 依赖 + `supportedOS` + DPI 感知。
- `TB_ADDSTRINGW = WM_USER + 38`；**`TB_ADDBUTTONSW = WM_USER + 68`（+20 是 ANSI 版，误用→按钮文字全丢只剩分隔符）**。
- **`LVM_INSERTCOLUMNW = LVM_FIRST + 97`（+27 是 ANSI 版，误用→列头 UTF-16 被 ANSI 解码成乱码；行内容纯 ASCII 时侥幸看不出）**。
- **绘制单元必须自带文本**：`drawUnit` 若只存 `item`（整段文字）而 paint 画 `item.text`，会导致每个拆分单位都把整段画一遍→整页叠影。`drawUnit` 需带 `text` 字段，paint 画 `du.text`；回归判定：排版步进 == `measureItem(item, du.text)`。
- 工具栏字符串池不能用 `UTF16PtrFromString`（内嵌 NUL 返回 nil），手动构造 UTF-16。
- `FillRect` 在 user32，不在 gdi32。
- **离屏渲染自检（GUI 无法截图时的排错利器）**：`CreateCompatibleDC`+`CreateDIBSection`(32bpp top-down) → 调 `paint()` → 手写 BMP 头落盘 → python 纯 stdlib 转 PNG → 用 Read 看图。已沉淀在 `render_dump_test.go`。
- 调试版 `gomd_dbg.exe` 用 `-ldflags="-s -w"`（带控制台）便于看 panic；交付版加 `-H windowsgui` 去黑窗口。
- x64 下 `callX` 传多余尾部 0 参数无害（caller-cleaned）；`nargs` 仅文档作用。
- **goldmark 表格 AST 形状坑**：`NewTableHeader` 会把表头行的单元格**直接挂到 `*extast.TableHeader` 下，丢掉 `TableRow` 包装层**；而数据行才是 `Table` 的直接 `*extast.TableRow` 子节点（本版本 **无 `TableBody` 节点**）。按"TableHeader 内含 TableRow"写会丢表头。兼容写法：对 TableHeader 同时接受 TableCell 子节点和嵌套 TableRow。列对齐优先用 `tbl.Alignments[col]`（`AlignLeft=1 / AlignRight=2 / AlignCenter=3 / AlignNone=0`）。
- **文本必须"整段一次 TextOut"**：逐字/逐单位单独 `TextOut` 会丢 GDI kerning，且每个单位单独 `GetTextExtentPoint32` 取整后累加会漂移 → 字间距偏大。做法：加 `sameItem()` 比较显示属性，`wrapUnits`/`doCode` 把**相邻同样式单元合并成一个 `drawUnit`**（text 拼接、x 取首单元），整段一次输出。

## 交付偏好（用户设定）
- 发布时先优化体积（`-trimpath -ldflags="-s -w"`），再 UPX `--best --lzma`；本机当前因网络未压缩。
