# PDF Reader 0.4.1

为阅读翻译浮窗提供准确的选区坐标。新插件可以在选中文字右下方显示，并在滚动、缩放或移动窗口时重新定位，避免盖住选区。配合 [阅读翻译插件 1.1.0](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/tag/deepseek-translate-v1.1.0)，可在毛玻璃浮窗中查看完整离线词典释义或用 DeepSeek 理解长上下文。

- **[下载 Windows 一键安装包](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/v0.4.1/PDFReader-Setup.exe)**
- [下载免安装版](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/v0.4.1/PDFReader-Portable.exe)
- [下载 SDK 1.1](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/v0.4.1/PDFReader-PluginSDK-1.1.zip)
- [SHA-256 校验](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/v0.4.1/SHA256SUMS.txt)

安装面向 Windows 10 / 11 x64 当前用户；覆盖安装可保留阅读记录、书签、批注和插件设置。翻译插件单独安装，在插件管理中启用。

SDK 1.1 新增可选 `SelectionGeometryV1` 能力，通过插件上下文查询。原 `HostV1`、`PluginV1` 和 API 1 IID 保持不变，旧插件无需重新编译。坐标采用全局逻辑像素，并裁剪到当前可见 PDF 视口；选区不在视野中时返回空矩形。几何查询只访问可见页，不扫描整份文档。

沿用 0.4.0 的高清阅读、书签、Word 风格正文批注和标准 PDF 注释保存功能。插件 UI、定位、滚动和清理使用实际 DLL 与合成 PDF 验证，并覆盖多种屏幕缩放。
