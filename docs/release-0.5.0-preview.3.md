# 0.5.0-preview.3：修复部分 EPUB 打开后空白的问题

[下载安装包](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/v0.5.0-preview.3/PDFReader-Setup.exe) · [免安装版](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/v0.5.0-preview.3/PDFReader-Portable.exe)

退出旧版后覆盖安装，重新打开 EPUB 即可。无需转换或修改原书，书签、阅读进度和插件继续保留。

- 修复旧版 EPUB 引用 Adobe XPGT 等附属 XML 排版资源时打开空白的问题。
  这类资源以 Blob 传入内容处理器，现在会先根据编码解码，再清理和解析 XML，支持 UTF-8 / UTF-16。
- 章节加载失败会显示错误，不再把没有正文的阅读区标记成加载成功。
  保存的阅读位置失效时，回退到首个正文章节。
- 翻页失败后释放导航锁，避免后续跳转失效；在书籍首尾继续翻页会安全停止。
- 包含 preview.2 的阅读区尺寸同步、固定版式适应页面 / 宽度和百分比缩放修复。

新增原创测试书覆盖 EPUB 2 的 XPGT 资源、UTF-16 资源和损坏 XML 的错误提示。
真实本机样本的 48 个章节逐一加载通过；该样本仅用于本机验证，不随源码或安装包分发。

EPUB 仍为预览功能，暂不支持 DRM、EPUB 批注、打印和导出。
