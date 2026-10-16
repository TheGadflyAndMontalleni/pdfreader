# pdfreader

一个自用的 PDF 阅读器：连续滚动、文字选择、查找、标签页、缩略图、标注、打印，
以及它存在的真正理由 —— **给没有大纲的 PDF 自动生成可跳转的目录树**。

C++17 / Qt 6 / PDFium，全部依赖都是可自由分发的许可证，见下方「技术选型」。

---

## 快速开始

```powershell
# 构建（首次会配置 CMake，之后是增量编译）
pwsh -NoProfile -File tools/build.ps1

# 运行
build\pdfreader.exe "D:\path\to\book.pdf"

# 验证：跑完整套检查，全部通过才返回 0
pwsh -NoProfile -File tools/verify.ps1

# 打包成单个 exe（可选）
pwsh -NoProfile -File tools/make_sfx.ps1
```

`verify.ps1` 是这份交付的**证据**：它把下面每一条声明都跑一遍并打印 PASS/FAIL。
`-SkipBuild` 跳过重新编译，`-Corpus <目录>` 换一个语料目录做目录覆盖率普查。

### 构建前提

仓库里**只放源码**（约 320 KB）。工具链和预编译依赖体积很大，需要自行准备，
按下面的路径放好即可 —— `tools/build.ps1` 会从这里找：

| 需要什么 | 放到哪 | 从哪来 |
|---|---|---|
| MinGW-w64 13.1.0 | `tools/Tools/mingw1310_64/` | [winlibs](https://winlibs.com/) 的 GCC 13.1.0 + POSIX 线程版 |
| Qt 6.8.3（动态版） | `tools/`（含 `bin/`、`lib/cmake/`） | `aqt install-qt windows desktop 6.8.3 win64_mingw` |
| PDFium 预编译 | `tools/pdfium/`（含 `include/`、`lib/`、`bin/`） | [bblanchon/pdfium-binaries](https://github.com/bblanchon/pdfium-binaries) 的 `chromium/8100` |
| CMake + Ninja | `.venv/Scripts/`（或自行加入 PATH） | `python -m venv .venv; .venv\Scripts\pip install cmake ninja` |

PDFium 官方预编译包带的是 MSVC 导入库，MinGW 链接不了。
`tools/make_pdfium_implib.ps1` 会把它转换成 MinGW 可用的 `libpdfium.a`。

### 打包成单个 exe

```powershell
pwsh -NoProfile -File tools/make_sfx.ps1
# 产出 dist\PDFReader.exe（约 63 MB）
```

`make_sfx.ps1` 把主程序和全部 15 个运行时文件（Qt、PDFium、MinGW 运行时、
Qt 平台插件）作为资源嵌进一个小的 Win32 启动器。运行它时：

1. 首次：解压到 `%LOCALAPPDATA%\PDFReader`，然后启动主程序；
2. 之后：`.stamp` 匹配当前构建，**跳过解压**直接启动。

命令参数会透传给主程序，所以 `PDFReader.exe --selftest book.pdf > log.txt` 也能用
（启动器等待子进程并返回它的退出码）。


---

## 功能

### (1) 阅读：连续滚动 / 缩放 / 导航 / 快捷键

整篇文档是**一条连续的页面长条**，滚动没有页面边界。缩放锚定当前页，滚动位置不跳。

| 操作 | 快捷键 |
|---|---|
| 上/下一页 | PageUp / PageDown |
| 向下/向上翻屏 | Space / Shift+Space |
| 首/末页 | Home / End |
| 上/下/左/右滚动 | 方向键 |
| 放大 / 缩小 | Ctrl++ / Ctrl+- |
| 适应宽度 / 适应页面 | Ctrl+0 / Ctrl+1 |
| 连续视图 / 单页视图 | C / Ctrl+6 |
| 跳转到页 | Ctrl+G |
| 中键拖拽平移 | 按住鼠标中键拖动 |
| 反色（夜间阅读） | I |
| 整页滚动 | 鼠标滚轮；Ctrl+滚轮缩放 |

工具栏还提供 打开 / 上一页 / 下一页 / 放大 / 缩小 / 适应宽度 / 适应页面 / 单页·连续。

### (2) 文字选择与复制

在页面上拖拽选择，双击选整行，Ctrl+A 全选，Ctrl+C 复制。
选择跨行、跨页都成立；选中内容按阅读顺序拼接，中日韩文本不额外插空格。

### (3) 查找

Ctrl+F 打开查找条，F3 / Shift+F3 在结果间跳转。
大小写敏感可选；命中项高亮，当前项用不同颜色标出并滚动到可见；状态栏显示 `3 / 27`。
匹配基于码点而非字节，因此中文不会被半个字符切断。

### (4) 日常可用性

- **标签页**：一个标签栏加一套侧栏/视图，多文档之间切换。每个标签**独立记住**页码、
  缩放、缩放模式、视图模式和反色状态。
- **最近文件**：文件菜单里最多 12 条，自动剔除已不存在的路径，可一键清空。
- **记住上次阅读位置**：按文档路径哈希存进 INI，**跨进程**生效。
- **重新打开上次关闭的文件**：Ctrl+Shift+T。
- **恢复上次的标签页**：启动时不带参数即恢复上次打开的整组标签。
- **缩略图侧栏**：分帧懒生成，长文档打开不卡；点击即跳转，当前页跟随高亮。
- **打印**：Ctrl+P，支持全部 / 当前页 / 页码范围。
- **标注**：高亮（Ctrl+H）、下划线（Ctrl+U）、删除线、撤销（Ctrl+Z）、另存为。

### (5) 自动目录 —— 核心差异能力

**无论原 PDF 有没有大纲，侧边栏总能给出可点击跳转的目录树。**

四层策略，逐层降级，前一层拿不到才试下一层：

| 层 | 来源 | 适用 |
|---|---|---|
| **L0** | 内嵌 `/Outlines` 书签大纲 | 原生就带目录的文档 |
| **L1** | `/PageLabels` 页码映射 | 不是独立来源，供 L2 把「印刷页码」换算成物理页 |
| **L2** | **印刷目录页** | 正文前有几页排好的目录列表 |
| **L3** | **排版启发式** | 既无大纲也无目录页，只能从版式反推 |

**L2（印刷目录页）** 扫描正文前的若干页，逐行解析「标题 …… 页码」，
用 `PlausibleToc` 做可信度判定（条目数、页码是否落在 `[1, 页数+2]`、
页码是否大体单调不减），再用 `/PageLabels` 或偏移量把印刷页码换成物理页。

**L3（排版启发式）** 是这一层的兜底，也是最难的部分。它做的事：

1. **局部基准**：取**每页**行高的中位数作为该页的"正文高度"。
   *（为什么不能用全局字符数众数：表格密集的文档里众数是表格文字 7.5pt，
   于是正文段落 15pt 会被算成"2 倍于正文"，**每一段都变成标题**。
   这一条把误检从 297 条降到 15 条。）*
2. **相对信号**：粗体只在**正文本身不是粗体时**才算信号 —— 整篇都用粗体时它零区分力。
3. **成批排除**：同一页有 4 行以上字高完全相同，那是正文段落或表格，不是标题。
4. **留白**：与相邻行的间距超过页中位间距 1.5 倍，算"被排版隔开"。
5. **文字性过滤**：标题必须含文字（排除页码 `15`、碎片 `63f`），排除纯日期行。
6. **编号序列恢复**：全文递增的顶层编号（`一、二、三…` 或 `第1章、第2章…`）
   本身就是一份"文档没写出来的目录"。**单个看它和列表项无异，连成序列就只有章节能做到。**
   这一步专门用来救回**与正文完全同字号**的章节标题。

当前语料成绩（`verify.ps1` 会重新跑一遍）：

```
内嵌大纲     6 篇
印刷目录页   9 篇
排版推断     5 篇
------------------
可加载文档  20 篇，20 篇全部生成目录，共 932 条，0 条跳转目标无法解析
```

---

## 技术选型

**为什么不用 SumatraPDF 的代码。** SumatraPDF 是 GPL-3.0。照搬它的代码会把整个
项目变成 GPL，这不是"属于我自己的软件"想要的结果。所以：**用 REA 分析它的实现思路，
但不复制它的代码**。分析结论见 [docs/analysis.md](docs/analysis.md)。

| 依赖 | 版本 | 许可证 | 说明 |
|---|---|---|---|
| Qt | 6.8.3 | LGPL-3.0 | **动态链接**，满足 LGPL 的可替换要求 |
| PDFium | chromium/8100 | BSD-3-Clause | 宽松许可，可静态链接 |
| MinGW-w64 | 13.1.0 | GPL + 运行时例外 | 编译器运行时例外允许分发产物 |

产物共约 60 MB，主要是 `pdfium.dll`（30.7 MB）和各 Qt 模块。

**为什么 Qt 用动态链接**：LGPL 要求最终用户能替换该库。动态链接是最省事的合规方式，
不需要提供目标文件或静态库重链接能力。

---

## 代码结构

```
src/
  doc.{h,cpp}           Document / Library（PDFium 生命周期、内存加载）
  text.{h,cpp}          字符与行的抽取、字重/字号判定、UTF-8 编解码
  render.{h,cpp}        页面光栅化
  find.{h,cpp}          跨文档查找（基于码点）
  annot.{h,cpp}         标注：烘焙进页面内容
  toc.{h,cpp}           目录模型 + L0/L2/L3 调度
  printed_toc.cpp       L2：印刷目录页解析
  toc_typography.cpp    L3：排版启发式
  app/
    main.cpp            命令行入口与验证开关
    mainwindow.{h,cpp}  菜单、标签页、侧栏、查找条、验证辅助
    pdfview.{h,cpp}     自绘滚动视图：布局、选择、查找、标注、缩放手势
    thumbnails.{h,cpp}  缩略图侧栏（分帧懒生成）
    printing.{h,cpp}    打印与矢量导出
    session.{h,cpp}     跨进程状态：阅读位置、最近文件、标签组
```

约 5,900 行。

---

## 验证

每一条声明都有可复现的证据。`tools/verify.ps1` 是总入口，覆盖：

| 检查 | 手段 |
|---|---|
| 阅读器自检（54 条断言） | 真实对象上的程序化断言，见下 |
| 标签页与每标签状态 | `--selftest-tabs`，两个不同文档 |
| 跨进程会话 | 写一次、**另一个进程**读回、外加一个必然失败的反向对照 |
| 矢量导出 | 导出 5 页 → 用 PDFium **重新打开** → 断言页数与字符数；外加不可写路径的反向对照 |
| 目录覆盖率 | 遍历语料，统计来源分布与未解析跳转数 |

自检里几条值得单独说明的断言（它们验的是**看到的像素**，不是标志位）：

```
PASS  continuous content (81581px) is taller than the viewport
PASS  fit-page keeps the whole page inside the viewport
PASS  page content lies inside the viewport horizontally (x=387)
PASS  single page fits vertically (754px vs 754px)
PASS  thumbnail strip built 61 row(s) for 61 page(s)
PASS  thumbnail of page 3 has real content
PASS  activating thumbnail 5 moved the view there (got 5)
PASS  invert darkens the page (242.6 -> 6.6)
PASS  turning invert off restores the page (6.6 -> 242.6)
PASS  the highlight is visible on screen (2964 changed px)
PASS  undo removes the mark (2964 -> 0 changed px)
PASS  the mark survived saving (1093 px)
```

调试开关（`--selftest` 之外的独立入口）：

```
--select-line 页:行     选中某行          --find 文本        执行查找
--select-all            全选              --markup 类型      高亮/下划线/删除线
--view single|continuous                  --fit width|page|none
--side toc|thumbs                         --invert
--print-test 文件 --print-out 产物.pdf --page N --page-to M
--session-write / --session-read 文件 --page N --zoom Z
```

设置项走 `PDFREADER_SETTINGS` 环境变量，所以测试永远不碰真实配置。

---

## 已知限制

写在前面，因为它们都是实测出来的，不是猜测：

1. **打印到真实打印机是光栅的。** PDFium 的公开 API 不提供矢量打印输出，
   这是与 SumatraPDF（MuPDF 可矢量打印）的真实差距。
   **但"打印成 PDF 文件"走的是矢量路径**（`FPDF_ImportPages`），
   保留文字层、体积小一个数量级，我们的目录引擎在产物上依然可用。
2. **标注是烘焙进页面内容的，是单向的。** 不是其他阅读器能选中/编辑/删除的
   交互式注释。原因见 [docs/analysis.md](docs/analysis.md) 第 4 节：
   PDFium 无法设置文本标记注释的 `/QuadPoints`，用它的 API 造出来的高亮
   **在文件里存在但在任何阅读器里都不可见**。

3. **L3 排版推断的精度在附录表格区一般。** 跳转目标永远是对的（标题的跳转目标
   就是它所在的页），但表格单元偶尔会被提升成条目。

4. **标注是全同步操作，89 ms/页。** 对 61 页文档全选标注约 5.5 秒（已加等待光标）。

5. **未实现**：命令面板、EPUB/CBZ/DjVu/CHM/XPS 等其他格式、
   标注的交互式编辑、多标签的并排视图、表单填写。

---

## 第三方

- Qt 6.8.3（LGPL-3.0）— 动态链接
- PDFium（BSD-3-Clause）— 来自 chromium/8100 分支
- 构建工具链 MinGW-w64 13.1.0
