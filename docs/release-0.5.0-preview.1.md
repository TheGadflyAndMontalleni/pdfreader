# 0.5.0-preview.1：EPUB 阅读试用版

这一版让阅读器可以直接打开无 DRM 的 EPUB 2 / EPUB 3，并与 PDF 放在同一窗口中阅读。

## 下载安装

- [Windows x64 一键安装包](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/v0.5.0-preview.1/PDFReader-Setup.exe)
- [免安装版](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/v0.5.0-preview.1/PDFReader-Portable.exe)
- [SHA-256 校验](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/v0.5.0-preview.1/SHA256SUMS.txt)

安装后按 **Ctrl+O** 选择 `.epub`，也可以在 Windows “打开方式”中选择 PDF Reader。
安装器会检测并在必要时联网补装微软 WebView2 Runtime；免安装版需要电脑已有该组件。
这是预览版本，GitHub 的稳定版 Latest 仍保持 0.4.1。

## 可以试什么

- 目录、中文正文、图片、内部链接；UTF-8 / UTF-16 章节。
- 字号调整时重新排版，连续滚动或翻页，夜间配色。
- 章节跳转、全文查找及命中定位。
- 按正文 CFI 记录阅读位置，改变字号或窗口大小后恢复；书签可添加、跳转、重命名和删除。
- 使用现有翻译插件选词、按 **Alt+T** 查离线词典或理解上下文，浮窗避开选区。
- 同时打开 PDF 和 EPUB，切换、关闭标签。

PDF 继续用 PDFium，EPUB 新增 WebView2 / Foliate JS 后端。ZIP 按范围读取，章节和资源
按需解压，打开 PDF 时不启动浏览器。详细方案、与 SumatraPDF 等路线的比较、依赖和
能力边界见 [EPUB 技术方案](epub-design.md)。

## 试用范围

暂不支持 DRM、EPUB 批注、EPUB 打印和改写导出；不执行书内 JavaScript，不打开外部
网站。固定版式提供基础打开，复杂竖排、脚注、音视频及出版社特殊版式仍需更多书籍验证。
翻译插件仍独立安装，[安装说明](../plugins/deepseek_translate/README.md)。

验证使用 Windows 11、Qt 6.8.3、MinGW 13.1 和 WebView2 155。集成测试运行真实浏览器，
覆盖 EPUB 2 / 3、固定版式、UTF-16、损坏文件、正文重排、CFI 恢复、书签、查找、
翻译 DLL 和混合标签，并检查本机来源限制及书内脚本清理。原有 PDF 阅读、插件生命周期、
批注及安装 / 升级 / 卸载回归同时执行。

本次三种 Qt 缩放配置（1 / 1.5 / 2）下 EPUB 检查均通过；原 PDF 阅读 47 项、
插件生命周期 35 项、批注 38 项、PDF 翻译集成及安装器 24 项检查均通过。
Runtime 缺失时的补装路径未在本机实际执行：本机已有 Runtime，隔离安装测试也刻意跳过系统组件安装。
