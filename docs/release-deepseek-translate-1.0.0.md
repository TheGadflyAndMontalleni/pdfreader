# DeepSeek 生词翻译插件 1.0.0

为阅读时遇到的生词和短语提供简短中文释义。自动识别原文语言，默认翻译为中文。

## 下载

- **[插件压缩包](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/deepseek-translate-v1.0.0/DeepSeekTranslate-1.0.0.zip)**：含 DLL、使用说明和许可证。
- [直接下载 DeepSeekTranslate.dll](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/deepseek-translate-v1.0.0/DeepSeekTranslate.dll)
- [SHA-256 校验值](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/deepseek-translate-v1.0.0/SHA256SUMS.txt)

适用于 Windows x64，兼容 PDF Reader 0.3.0 / 0.4.0 的 Plugin API 1，无需升级或替换阅读器程序。

## 使用

1. 解压插件，在阅读器的 **插件 → 插件管理** 中安装 `DeepSeekTranslate.dll` 并启用。
2. 打开 **插件 → DeepSeek 生词翻译 → DeepSeek API 设置…**，填写自己的 API Key。
3. 在 PDF 中拖动选中生词，按 **Alt+T** 查看中文释义。

需要自动翻译时，勾选小窗中的 **自动划词翻译**。默认模型为 `deepseek-flash`，可在设置中更改。

小窗仅保留词语、语言、释义、复制和设置。只发送当前选词，不上传整页、文件路径或批注。Key 在本机加密保存。支持成功结果的会话缓存、请求取消、超时、错误提示及自动划词防抖。

API 调用使用自己的 DeepSeek 账户，计费规则以官方为准。自动语言识别与词义判断由模型完成；少量歧义词需结合原文理解。插件首次启用会部署内嵌的 HTTPS 组件，无需另装运行库。

使用及独立构建详见 [开发与使用说明](https://github.com/TheGadflyAndMontalleni/pdfreader/tree/deepseek-translate-v1.0.0/plugins/deepseek_translate)。本插件使用合成文本和虚构 Key 验证请求、UI 和生命周期；未使用用户的真实 Key 进行付费翻译。

这是独立插件发布，阅读器主程序的最新版本和安装包下载入口保持独立。
