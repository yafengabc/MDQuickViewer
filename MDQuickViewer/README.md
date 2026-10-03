# MDQuickViewer — 纯 C + Win32 Markdown 浏览器

用 **纯 C + Win32 API** 实现的 Markdown 阅读器，不依赖 WebView、Electron 或任何第三方
GUI 框架。解析、语法着色与 GDI 自绘排版全部手写。

## 构建

需要 MSYS2 UCRT64 工具链（`gcc`、`windres`）。在本目录下：

```bash
./build.sh            # 构建 build/MDQuickViewer.exe
./build.sh test       # 构建并跑全部自检（解析/着色/渲染/选择，共 438 项）
```

编译选项说明：

- `-mwindows`：把 PE 子系统设为 GUI（启动无控制台黑框）
- `-municode`：入口用 `wWinMain` 直接拿宽字符命令行，省去手工转 argv
- `windres`：把图标与 manifest 编成 `.res.o` 链进 exe（对应 Go 版的 `go:embed syso`）

## 运行

- 直接打开 `build/MDQuickViewer.exe`：若 `build/sample.md` 存在则自动载入，否则空白待打开。
- 拖入文件 / 文件夹，或用菜单 / 工具栏 / `Ctrl+O`、`Ctrl+F` 打开。
- 命令行：`MDQuickViewer.exe <文件或目录>`，只取第一个有效参数。

## 功能

- 原生窗口 / 工具栏（图标+文字）/ 菜单 / 状态栏 / 左侧文件列表（默认折叠，`Ctrl+L` 切换）
- GFM：表格、任务列表、删除线、引用、代码块语法着色
- 缩放：`Ctrl + 滚轮`、`Ctrl +/-`、`Ctrl+0` 重置
- 滚动：`WS_VSCROLL` + 滚轮 + 方向键
- 文本选择：鼠标拖选、`Ctrl+A` 全选、`Ctrl+C` 复制
- **右键菜单**：在预览区点击右键弹出"复制选中文本 / 全选"，无选区时"复制"自动灰掉
- 链接点击：`http/https/mailto` 用默认程序打开
- 拖放文件 / 文件夹；注册表（`HKCU\Software\MDQuickViewer\LastFolder`）记忆上次目录

## 源码结构

| 文件 | 职责 |
| --- | --- |
| `main.c` | `wWinMain` 入口，`--test*` 自检分支 |
| `ui.c` | 窗口外壳：控件创建、布局、命令、拖放、选择、滚动、命令行 |
| `dlg.c` | `GetOpenFileNameW` / `SHBrowseForFolderW` 封装 |
| `settings.c` | 注册表读写（`MDQV_REG_KEY` 可重定向子键，测试用） |
| `win32.c/.h` | Win32 头与共用宏 |
| `model.h` | `Block` / `Span` / `TableData` 中间模型 |
| `markdown.c` | Markdown 解析（GFM） |
| `highlight.c` | 代码语法着色（单行 `hl_tokenize` + 跨行 `HLState`） |
| `render.c` | GDI 排版、绘制、命中测试、文本选择、双缓冲 |

字符串池 `pool.c` 逻辑在 `markdown.c` 内：固定 8KB 块链表，**永不 realloc**
（一旦搬动内存，此前发出的所有 `const char *` 都会悬垂）。

## 测试

自检结果写入 `%TEMP%\MDQuickViewer-test.log`（GUI 子系统 stdout 不保证接到管道），
构建脚本从那里读取：

- `--test-md` 解析器（96）
- `--test-hl` 语法着色（62）
- `--test-render` 排版/绘制/选择/着色回归（280）
- `--test` 全部

`src/layout_stress.c` 是针对 resize 崩溃根因的离线压测：直接调用真实的
`split_list_preview`，对负宽、零、极小、巨大、随机等上百万人次输入断言
列表/预览宽度恒为非负且和不超过客户区宽度。编译方式见文件头注释。

## 实现要点

- **中间模型与排版分层**：`markdown.c` 只产出 `Block`/`Span`，`render.c` 负责折行、
  定位与绘制，两者通过 `DrawUnit` 数组解耦。命中测试与文本选择都基于已排版的
  `DrawUnit`，因此选择高亮框与实际绘制像素天然一致。
- **选区绘制顺序**：块背景 → 选区高亮（不透明浅蓝）→ 文字 → 删除线/下划线 →
  分隔线 → 表格网格。选区必须在文字之前画。
- **离屏缓冲一律 `CreateDIBSection(32bpp)`**：`CreateCompatibleBitmap` 在内存 DC 上
  会生成单色位图，把颜色全量化成黑白。
- **`COLORREF` 是 `0x00BBGGRR`**：手写色值宏 `COLR()` 必须做 R/B 互换。
- **resize 安全**：所有传给 `MoveWindow` 的宽高都先夹到 `>= 0`；`split_list_preview`
  保证列表宽 + 预览宽不超过客户区宽。
- **消息循环**：`wWinMain` 开头无需 `LockOSThread`（那是 Go 的要求），但窗口过程里的
  崩溃无法被 Go 式 recover 兜底，因此要显式校验尺寸。
