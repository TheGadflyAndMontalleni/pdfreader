# PDF Reader 0.2.1

修复书签删除入口不可见的问题。

## 下载与升级

- **[Windows 一键安装包](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/v0.2.1/PDFReader-Setup.exe)**：关闭阅读器后双击安装，保留已有书签和阅读记录。
- [免安装版](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/v0.2.1/PDFReader-Portable.exe)
- [SHA-256 校验文件](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/download/v0.2.1/SHA256SUMS.txt)

适用于 Windows 10 / 11 x64，包含所需运行组件，无需管理员权限。当前安装包尚未代码签名。

## 书签删除与重命名

1. 打开左侧“书签”标签，选中书签。
2. 点击上方“删除”；按钮现在在最窄侧栏中也保持可见。
3. 也可以右键书签选择“删除所选书签”，或在书签列表内按 Delete；重命名支持 F2。

顶部“书签”菜单也补充了删除和重命名。删除会立即保存；右键操作针对点击的书签，删除后自动选中剩余书签。Delete 仅在书签列表获得焦点时生效。

## 验证

新增界面回归检查覆盖最小侧栏宽度、顶部菜单点击、按钮点击、Delete 键、右键选中正确条目、删除持久化和空列表禁用状态。继续验证多倍率渲染、目录、阅读状态，以及安装、升级和卸载。
