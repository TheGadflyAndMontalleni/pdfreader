# DeepSeek 生词翻译 1.0.0

面向阅读时偶遇生词的小型插件。选中单词或短语，按 **Alt+T**，自动识别原文语言，默认给出简短的中文释义。

兼容 PDF Reader **0.3.0 / 0.4.0** 的 Plugin API 1，Windows 10 / 11 x64。无需替换阅读器程序；只安装本插件的 DLL 即可。插件自带与阅读器匹配的 Windows HTTPS 组件。

## 安装和配置

1. [下载插件压缩包](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/deepseek-translate-v1.0.0/DeepSeekTranslate-1.0.0.zip)，解压得到 `DeepSeekTranslate.dll`。
2. 在阅读器中打开 **插件 → 插件管理 → 安装插件…**，选择该 DLL，再点击 **启用**。
3. 打开 **插件 → DeepSeek 生词翻译 → DeepSeek API 设置…**，填写自己的 DeepSeek API Key 并保存。
4. 在 PDF 中拖动选中生词或短语，按 **Alt+T**。小窗显示识别出的语言和中文释义，可复制结果。

也可以直接 [下载 DLL](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/deepseek-translate-v1.0.0/DeepSeekTranslate.dll)。更新插件时请先禁用并退出阅读器，再替换旧 DLL；配置保留。

默认模型为 `deepseek-flash`，可在设置中修改。根据 [DeepSeek 官方文档](https://api-docs.deepseek.com/)（2026-10-20 核对），插件请求官方 `https://api.deepseek.com/chat/completions`，使用非思考模式和 JSON 输出，让模型自动判断语言并简短译为简体中文。模型歧义词最多列举两种常见释义，未发送整页上下文，因此个别词义仍需结合原文判断。

## 保持阅读流程简单

- 不需要选择原文语言，目标固定为中文。
- 默认按快捷键或菜单触发；勾选小窗中的 **自动划词翻译** 后，松开鼠标、选词稳定约半秒才请求。
- 仅处理生词或短语，最多 200 字符、30 个空格分隔的词；长段落不发送。
- 同一模型下最近 128 次成功查词在本次启用期间复用，不重复请求；**翻译 / 重试** 可强制重新查询。
- 更换选词、关闭翻译窗、切换文档或禁用插件时，会取消旧请求，避免旧结果覆盖新词。界面保持响应，单次请求最多等待 30 秒。

## API Key 与数据

API Key 使用 Windows 当前用户的 DPAPI 加密后保存到插件配置中，不以明文写入配置文件、日志或插件包。复制配置到另一台电脑或另一个 Windows 账户后，需要重新填写 Key。也可使用 `DEEPSEEK_API_KEY` 环境变量；仅在未保存可解密的 Key 时读取它。

请求只包含系统翻译指令和当前选词，不发送文件名、PDF 路径、整页正文、批注、书签或阅读记录。缓存仅存在内存中，禁用即清空。勾选自动划词后会持续观察当前选词；不需要时可取消勾选或禁用插件。

网络使用系统代理和正常证书校验，不跟随服务重定向。API 调用由自己的 DeepSeek 账户计费；[计费规则](https://api-docs.deepseek.com/quick_start/pricing/)和模型可用性以官方信息为准。余额不足、Key 无效、限流和超时会直接显示简短提示。

## 独立构建

在仓库中使用与阅读器一致的 Qt 6.8.3 / MinGW GCC 13.1.0 / x64 / Release：

```powershell
pwsh -NoProfile -File tools/build_plugin.ps1 `
  -Source plugins/deepseek_translate -BuildDir build/deepseek-translate
```

输出：`build/deepseek-translate/reader-plugins/DeepSeekTranslate.dll`。只依赖公开 SDK，不链接阅读器内部源码或 PDFium。构建时从 Qt 安装目录嵌入 `qschannelbackend.dll`；非标准布局可在 CMake 配置中设置 `PDFREADER_SCHANNEL_DLL`。

```powershell
pwsh -NoProfile -File tools/verify_translation_plugin.ps1
pwsh -NoProfile -File tools/make_translation_plugin.ps1
```

验证使用合成文本、虚构 Key、本机模拟 HTTP 服务和独立配置，不读取用户 PDF 或真实 Key。可加 `-ProbeNetwork` 做官方 HTTPS 连通性检查：仅提交固定测试词和无效的虚构 Key，预期收到认证拒绝，不进行付费翻译。正式翻译效果需要填写自己的有效 Key 后验证。

## 代码

- `translation_client.*`：DeepSeek 请求、自动语言识别指令、响应验证、超时与取消。
- `translation_controller.*`：小窗、快捷键、配置、自动划词和内存缓存。
- `credentials.*`：Windows 当前用户密钥加解密。
- `tls_runtime.*`：内嵌 HTTPS 后端部署，兼容旧阅读器运行包。
- `tests/translation_tests.cpp`：真实网络流程的本机模拟、UI 和生命周期回归。

插件代码沿用仓库 MIT 许可。内嵌 Qt HTTPS 后端的许可证与版权信息随压缩包一并提供，见 `licenses/`。
