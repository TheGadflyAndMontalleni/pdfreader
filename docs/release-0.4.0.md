# PDF Reader 0.4.0

加入类似 Word 的正文批注，在阅读时把想法写在原文旁边。

## 下载与升级

- **[Windows 一键安装包](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/v0.4.0/PDFReader-Setup.exe)**：覆盖安装，保留阅读记录、个人书签、批注和插件数据。
- [免安装版](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/v0.4.0/PDFReader-Portable.exe)
- [Plugin SDK 1.0](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/v0.4.0/PDFReader-PluginSDK-1.0.zip)
- [SHA-256 校验文件](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/v0.4.0/SHA256SUMS.txt)

适用于 Windows 10 / 11 x64，安装包包含运行组件。

## 正文批注

拖动选中正文，点击选区旁的 **＋ 批注** 或按 **Ctrl+Alt+M**，在右侧卡片中输入笔记。顶部工具栏和右键菜单也提供新建批注入口。

选中文字保留淡黄色高亮，当前批注显示边框、连线和页边标记。点击正文标记会打开对应卡片，点击卡片上的页码可返回原文。支持多行、跨页选区，每张卡片均可直接编辑或删除。顶部 **批注** 按钮可收起侧栏，侧栏宽度可拖动调整。

批注自动保存在当前电脑，切换标签或关闭文档时保存尚未写入的修改，不会自动改写原 PDF。需要备份、分享或换电脑阅读时，使用 **文件 → 另存为**，批注的文字和选区位置会随标准 PDF 高亮注释写入文件；在本阅读器中重新打开后可继续编辑。导出 PDF 页范围也保留对应批注。

另存 PDF 支持中文文件名并采用原子替换，写入失败不会截断原文件。若原 PDF 被外部替换或修改，旧的本机批注不会自动套用到变化后的文件，避免选区位置错配。

## 兼容与验证

本阅读器管理自己创建的批注，其他软件已有的注释会保留。原有的普通高亮、下划线和删除线仍使用页面内容标记。Plugin SDK 继续使用 API 1，无需升级插件接口。

新增 38 项批注回归检查，覆盖选区浮动按钮、卡片编辑、跨进程自动保存、标签隔离、跨页批注、正文定位、删除与重开、中文路径、PDF 注释导出及原文件保护；在 100%、150%、200% 显示缩放下验证，并继续运行阅读、书签、目录、打印导出和插件回归。
