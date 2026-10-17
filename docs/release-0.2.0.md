# PDF Reader 0.2.0

高清阅读、快速打开、个人书签，以及 Windows 一键安装。

## 下载

- **[Windows 安装版 PDFReader-Setup.exe](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/v0.2.0/PDFReader-Setup.exe)**：双击后点击“一键安装”，完成后自动启动；包含运行库、快捷方式和 PDF 打开方式注册。
- [免安装版 PDFReader-Portable.exe](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/v0.2.0/PDFReader-Portable.exe)：双击运行，首次释放组件后直接启动。
- [SHA256SUMS.txt](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/v0.2.0/SHA256SUMS.txt)：两个下载文件的 SHA-256 校验值。

适用于 Windows 10 / 11 x64。安装面向当前用户，升级和卸载保留阅读记录与书签。当前安装包尚未代码签名。

## 改动

- 适配屏幕实际像素倍率，修复高分屏文字发虚；大幅放大时仅渲染可见区域。
- 使用快速页面尺寸查询，先显示正文，再独立生成目录；缓存目录并检测文件变化。
- 移除缩略图和后台生成任务，关闭文档会取消对应的目录任务。
- 加入个人书签：Ctrl+D 添加、跳转、重命名、删除，按文档保存在本机。
- 目录加入全部展开/收起，记住侧栏宽度，切换标签保留展开状态。
- 整理工具栏和空白界面，增加页码、缩放输入，改善深色主题可读性。
- 修复标签拖动错位、多文档阅读状态串档，以及调整布局后页码回退。
- 增加原生安装程序、升级和卸载支持，并随安装文件分发运行库与许可证。

## 验证

61 页样本在 Windows 原生平台、2×屏幕倍率的 Release 构建中，打开文件调用约从 **248 ms 降至 25 ms**，首屏约 **65 ms**；位图由 937×1325 提升到 1874×2650。测量不包含进程冷启动、系统检查和首次释放组件。

- 阅读器共 17 组检查通过，包含阅读、复制、查找、标注、打印、会话和目录。
- 1×、1.5×、2× 下分别通过 40 条新增回归断言，另检查原生 Windows 界面。
- 安装器验证安装、覆盖升级、快捷方式、PDF 注册、许可文件、已安装程序自检和卸载；保留用户数据与其他 PDF 应用的注册项。
- 免安装包执行真实阅读器自检，确认目录子进程可运行。

源码构建及验证命令见 [README](https://github.com/TheGadflyAndMontalleni/pdfreader#验证)。
