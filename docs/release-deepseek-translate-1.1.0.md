# 阅读翻译插件 1.1.0：毛玻璃浮窗、离线词典与语境理解

- [下载插件包](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/deepseek-translate-v1.1.0/DeepSeekTranslate-1.1.0.zip)
- [直接下载 DLL](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/deepseek-translate-v1.1.0/DeepSeekTranslate.dll)
- [SHA-256 校验](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/deepseek-translate-v1.1.0/SHA256SUMS.txt)
- [配套阅读器 0.4.1](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/tag/v0.4.1)

选中文字后按 **Alt+T**。默认自动判断查询方式：单个英文词查内置离线词典；短语、句子和长上下文使用 DeepSeek 翻译并解释语境，也可以手动切换。

毛玻璃小窗优先显示在选区右下方，靠近屏幕边缘时换位避开选中文字；随滚动、缩放和窗口移动更新，选区滚出视野时隐藏。材质使用阅读器背景采样模糊，兼容 Windows 10 / 11。右上角 × 或 Esc 关闭。

内置 ECDICT 770,611 条词条，展示词库提供的音标、中英文完整释义和词形变化。离线查询不需要 Key；词典未收录时提示手动切换 AI，不自动产生付费请求。长上下文最多 8,000 字符，仅发送选区文字，Key 仍在本机加密保存。

**安装**：在阅读器中打开 **插件 → 插件管理 → 安装插件…**，选择解压的 `DeepSeekTranslate.dll` 并启用。DeepSeek 的 Key 在 **插件 → DeepSeek 生词翻译 → 翻译与 DeepSeek 设置…** 中填写。

**从 1.0 升级**：先禁用旧插件，退出并重启阅读器，移除旧插件后安装新版；保留原配置和 Key。为了准确定位，请同时升级阅读器到 **0.4.1**。旧 0.3 / 0.4 阅读器仍能加载，但只能估算鼠标附近位置。

词库、HTTPS 组件都嵌入同一 DLL，许可证与数据来源校验随 ZIP 提供。源码及更多说明见 [插件使用和开发指南](https://github.com/TheGadflyAndMontalleni/pdfreader/tree/deepseek-translate-v1.1.0/plugins/deepseek_translate)。自动化测试使用合成文本与虚构 Key；未用真实 Key 执行付费 AI 翻译。

本发布为独立插件，仓库 Latest 保留给阅读器安装包。
