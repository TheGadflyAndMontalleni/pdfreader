# 0.5.0-preview.4：EPUB 正文批注

[下载安装包](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/v0.5.0-preview.4/PDFReader-Setup.exe) · [免安装版](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/v0.5.0-preview.4/PDFReader-Portable.exe)

退出旧版后覆盖安装，原有阅读记录、书签和插件继续保留。

打开 EPUB，拖动选中一个词或一段正文，点击选区旁的 **＋ 批注**，也可以右键选择
**新建批注**，或按 **Ctrl+Alt+M**。右侧卡片保留引文，可直接输入中文、多行笔记，
支持编辑、删除；点击卡片的章节按钮可返回原文，点击正文高亮可打开对应卡片。
顶部 **批注** 按钮可随时收起或展开侧栏。

- 重排 EPUB 通过范围 CFI 记录正文位置，调整字号、窗口宽度、翻页模式和夜间配色后仍能定位。
- 固定版式 EPUB 增加章节和高亮图层接口，图层随整页缩放；在放大的页面中，卡片可把原文滚动到可见区域。
- 同一段文字可添加多条独立批注。跨章节笔记按书籍保存，关闭重开后恢复；删除也会自动保存。
- 批注保存在本机用户配置中，不修改原 EPUB。源文件被替换或修改后，不自动套用旧位置。
- 现有 PDF 批注、PDF 内嵌导出和翻译插件继续可用。

本版仍为 EPUB 预览版，暂不支持 DRM、EPUB 打印、批注导出或跨设备同步。
包含此前预览版的 EPUB 打开兼容性和页面尺寸修复。

验证：三个显示缩放配置下各 19 项 EPUB 集成检查通过，覆盖批注完整流程、固定页面
高亮几何、关闭重开和删除持久化；PDF 批注 38 项、PDF 阅读 47 项、真实翻译 DLL
集成及安装 / 升级 / 卸载 24 项检查通过。本机真实 EPUB 的 48 个内容分节也逐一加载通过。
