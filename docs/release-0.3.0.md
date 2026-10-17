# PDF Reader 0.3.0

加入插件机制和 Plugin SDK 1.0，后续功能可以通过独立 C++ / Qt DLL 扩展。

## 下载与升级

- **[Windows 一键安装包](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/v0.3.0/PDFReader-Setup.exe)**：关闭阅读器后覆盖安装，保留书签、阅读记录和插件数据。
- [免安装版](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/v0.3.0/PDFReader-Portable.exe)
- **[Plugin SDK 1.0](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/v0.3.0/PDFReader-PluginSDK-1.0.zip)**：公共头文件、CMake 辅助函数、独立插件示例、构建脚本和开发指南。
- [SHA-256 校验文件](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/v0.3.0/SHA256SUMS.txt)

适用于 Windows 10 / 11 x64。安装包包含运行组件，尚未代码签名。

## 使用插件

打开 **插件 → 插件管理**，选择 **安装插件…** 后启用；也可以直接启用内置的“阅读工具示例”，体验复制当前页文字、添加书签和文档信息面板。新插件默认禁用，启用状态跨启动保存。

插件接口支持当前文档快照、页面和选中文字、打开文件、跳页、书签、插件菜单、停靠面板、独立配置和数据目录，以及文档、页码、选区变化回调。插件不需要链接阅读器内部源码或 PDFium。

禁用、初始化失败和退出时会清理宿主注册 UI 和上下文资源。错误 API、IID 和无效元数据会在执行前拒绝；受宿主管理的 C++ 回调异常会停用插件。新增 `--no-plugins`，可在第三方插件导致启动问题时跳过自动加载。

原生插件与阅读器处于同一进程，并非沙箱，请只启用可信插件。当前发布版使用 Qt 6.8.3 / MinGW 13.1.0 / x64 / Release；插件需要匹配工具链。API 1 保持冻结，不兼容的变更使用新 API/IID。已加载 DLL 的替换或移除需禁用并重启。

## 开发与验证

详见 [插件开发指南](https://github.com/TheGadflyAndMontalleni/pdfreader/blob/v0.3.0/docs/plugin-development.md)。提供真实 DLL 生命周期回归、独立 SDK 构建，以及安装版和免安装版中的插件加载自检；继续验证高清渲染、书签、目录与阅读状态。
