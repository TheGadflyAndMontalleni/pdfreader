# EPUB 试用版的技术方案

## 选择

保留 PDFium 处理 PDF，新增 `EpubView`，用 Windows WebView2 + Foliate JS
处理 EPUB。两种文档复用主窗口、标签页、目录、书签、最近文件和插件入口。
EPUB 正文保持 HTML / CSS 文字排版，字号变化会重新排版，位置保存为 EPUB CFI。

| 方案 | 当前项目中的取舍 |
| --- | --- |
| SumatraPDF / MuPDF 路线 | Sumatra 自 3.4 起使用 MuPDF 电子书引擎，统一选词、搜索和书签。值得借鉴分层，但直接移植会增加引擎、工具链与许可集成工作。 |
| Qt WebEngine | HTML / CSS 能力完整，但 Qt 6.8 官方明确不支持 MinGW；切到 MSVC 会影响现有 Qt、PDFium 导入库及已分发插件。 |
| Qt 富文本 / 先转 PDF | 接入较小，但复杂书籍 CSS、字体、竖排、脚注和重排适配需要自己补齐；转换还增加打开等待。 |
| **WebView2 + Foliate JS** | 沿用 Qt / MinGW 和现有插件 ABI，借助成熟 EPUB 排版代码；浏览器运行组件由系统共享和更新。代价是首次 EPUB 要启动浏览器进程，以及部分机器需要安装 WebView2。 |

这是一版验证该路线的可用实现，不宣称已经覆盖所有出版商和所有 EPUB 特性。
没有复制 SumatraPDF 源码。依赖版本、许可和本地补丁见
[third_party/README.md](../third_party/README.md)。

## 数据流

`MainWindow` 根据扩展名选择 PDF 或 EPUB。PDF 沿用原有 `PdfView`；每个 EPUB
标签拥有自己的 `EpubView`、浏览器控制器和临时本机 HTTP 服务，切换标签保留排版状态。
Windows 原生子窗口嵌入 Qt 的阅读区域，浏览器消息把目录、CFI、选区文字和矩形发送给宿主。

`EpubServer` 只绑定 `127.0.0.1` 随机端口，路径带随机 token，只提供编译进程序的
阅读页面和当前已打开的一本书。HTTP Range + zip.js 的 BlobReader 适配器按需读取
ZIP 目录、章节和图片；不先完整下载、解压或转换书籍。未使用的资源无需加载。
首次 EPUB 会包含 WebView2 的初始化时间，PDF 不初始化浏览器。

CFI 与字号、连续 / 翻页模式和夜间配色一起保存。EPUB 书签增加可选 `locator`
字段；原有 PDF 书签和设置兼容。界面中的“章节”是 spine 章节序号，阅读进度按章节
大小估算，不将重排后变化的屏幕页数当作固定书页。

## 本版范围

- 无 DRM EPUB 2（NCX）、EPUB 3（nav），常见重排正文与固定版式基础打开。
- 原书文字、图片、CSS、中文、目录与内部链接；固定版式支持适应页面、适应宽度和页面缩放。
- 连续滚动 / 翻页、字号、夜间配色、章节跳转、全文查找（最多 1000 个命中）。
- CFI 阅读记录和个人书签的添加、跳转、重命名、删除。
- 既有翻译 DLL 的选词、Alt+T 和选区屏幕坐标；无需重编译翻译插件。
- PDF 与 EPUB 混合标签，PDF 的打印、标注和批注保持原有行为。

暂未实现 EPUB 文字批注、改写导出、打印、DRM、音视频 / SMIL、书内 JavaScript
和外部网站链接。固定版式遵循原书排版，缩放整个页面；重排书籍则调整字号。
固定版式不提供与重排书籍相同的全文搜索保证。
本版对书籍限制为 ZIP 文件 ≤ 1 GB、资源数 ≤ 20000、单个解压资源 ≤ 64 MB、
解压总大小 ≤ 512 MB。复杂竖排、脚注交互及出版社特殊 CSS 还需要更多真实书籍验证。

插件 API 1 的 vtable / IID 保持不变。EPUB 下 `DocumentInfo` 的页数、页码、
`goToPage()` 对应章节；`pageText()` 同步返回缓存中的近期章节文字（最多 16 节），
尚未加载或已移出缓存的章节返回空字符串。需要完整跨格式文字、稳定位置和异步检索的插件，后续应使用新增可选
能力接口，而不修改冻结的 HostV1。

## 内容边界和安装

书籍 iframe 只允许宿主访问 DOM，去掉了上游的 `allow-scripts`。HTML 进入排版前移除
脚本、事件属性、嵌套框架等；CSP 限制脚本来源、外部资源、连接和表单。宿主阻止外部
导航、新窗口与权限请求。本机服务检查 Host / Origin、路径和范围，不暴露目录浏览或
任意文件读取 API。EPUB 阅读本身不上传正文；主动使用 AI 翻译仍遵循翻译插件的选区发送规则。

安装器检测 Evergreen WebView2，缺少时运行微软签名的引导安装程序，需要联网。
已有 Runtime 时跳过。打包脚本验证引导程序的 Microsoft 签名。免安装版需要已有 Runtime；
缺少时给出微软安装链接。测试安装器明确跳过系统 Runtime 安装，避免修改测试机环境。

## 验证

```powershell
pwsh -NoProfile -File tools/verify_epub.ps1 -Pdf "C:\books\sample.pdf" `
  -TranslationPlugin "C:\plugins\DeepSeekTranslate.dll"
```

脚本创建原创的 EPUB 2、EPUB 3、固定版式及损坏文件样本，使用真正的 WebView2
运行集成检查，隔离设置和浏览器缓存。覆盖 Range / 来源限制、按需读取、真实 DOM 和
图像排版、脚本清理、选区屏幕坐标、调字号、换章、夜间模式、查找、书签和 PDF 混合标签。
提供翻译 DLL 时额外验证 Alt+T、离线词条和浮窗避让，并导出实际阅读界面截图。
原有 PDF、插件和批注回归另行运行。

额外覆盖旧版 EPUB 的 Adobe XPGT 附属 XML、UTF-16 编码及章节加载失败提示。
可在运行脚本前设置 `$env:EPUB_TEST_BOOK='C:\books\sample.epub'`，逐章检查一个本机
重排 EPUB；文件只读打开，不复制进测试样本或输出正文，设置和浏览器缓存仍隔离。

## 参考资料

- [SumatraPDF 的电子书界面](https://www.sumatrapdfreader.org/docs/Customize-eBook-UI)
- [Qt WebEngine 6.8 平台限制](https://doc.qt.io/qt-6.8/qtwebengine-platform-notes.html)
- [WebView2 分发和 Runtime 检测](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/distribution)
- [webview 的 MinGW 支持](https://github.com/webview/webview)
- [Foliate JS API 与排版器](https://github.com/johnfactotum/foliate-js)
- [W3C EPUB 3.3](https://www.w3.org/TR/epub-33/)
- [MuPDF 许可说明](https://mupdf.readthedocs.io/en/1.28.5/license.html)
