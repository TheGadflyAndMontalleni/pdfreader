# 0.5.0-preview.2：修复 EPUB 阅读区缩在角落的问题

[下载安装包](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/v0.5.0-preview.2/PDFReader-Setup.exe) · [免安装版](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/v0.5.0-preview.2/PDFReader-Portable.exe)

覆盖安装后重新打开 EPUB。阅读记录、书签和现有翻译插件继续保留。

- 修复嵌入浏览器仍使用初始小窗口尺寸的情况。首次显示、切换标签、调整窗口和 DPI
  变化后，浏览器会在 Qt 布局完成后同步到实际阅读区，并显式更新浏览器边界。
- 重置浏览器自身的历史缩放；阅读器中的字号 / 页面缩放独立生效。
- 修复固定版式的整书 viewport 字符串解析错误，避免落成浏览器默认的 300×150 小画布。
  同时处理 SVG viewBox 的逗号 / 多空格分隔及非法尺寸回退。
- 固定版式默认适应页面，支持“适应宽度”“适应页面”、百分比和加减按钮；放大后的
  页面可从左上边缘滚动阅读。缩放方式和比例会随阅读记录保存。
- 固定版式显示“缩放”，重排书籍显示“字号”，避免把字号调整误当成整页缩放。

新增回归用例先在旧实现上复现嵌入窗口尺寸不匹配及 300×150 画布错误，修复后验证
首次布局、窗口变化、隐藏 / 显示、标签切换、页面居中、整页缩放和恢复。
EPUB 仍为预览功能；DRM、EPUB 批注和打印尚不支持。

验证：三个 Qt 缩放配置下 EPUB 集成测试全部通过；原 PDF 阅读回归、真实翻译插件
集成以及安装 / 升级 / 卸载的 24 项检查通过。
