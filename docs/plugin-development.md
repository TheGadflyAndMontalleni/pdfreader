# 插件开发指南：SDK 1.1 / API 1

PDF Reader 0.3.0 支持独立 C++ / Qt 插件 DLL，无需修改或链接阅读器源码。

## 安装与使用

1. 打开 **插件 → 插件管理**，点击 **安装插件…**，选择插件 DLL。
2. 选中插件并点击 **启用**。命令会显示在 **插件 → 插件名称** 中。
3. 内置的 **阅读工具示例** 提供复制当前页文字、添加书签和文档信息面板，可直接启用体验。

新插件和示例默认禁用。启用状态会保存，后续启动在首屏显示后加载。禁用会清理注册菜单、面板以及宿主上下文中的计时器和连接。

**打开插件目录** 可找到当前用户的插件位置，即 `settings.ini` 同级的 `plugins/`。插件配置和数据另行保存，升级阅读器、移除插件均保留这些数据。安装版和免安装版使用同一用户配置；测试中的 `PDFREADER_SETTINGS` 会一并隔离插件路径。

已加载的 DLL 在本次运行中可能仍被 Windows 占用。更新时先禁用、关闭并重新启动阅读器，再移除旧插件和安装新版。内置示例只能禁用。

原生插件在阅读器进程中运行，具有当前账户的权限，并非沙箱，请只启用可信插件。若第三方插件导致启动异常，运行以下命令可跳过本次自动加载，再到插件管理中禁用该插件：

```powershell
.\pdfreader.exe --no-plugins
```

## 获取和独立构建 SDK

- [下载 Plugin SDK 1.1](https://github.com/TheGadflyAndMontalleni/pdfreader/releases/latest/download/PDFReader-PluginSDK-1.1.zip)
- [公共接口](../sdk/pdfreader_plugin.h)、[CMake 辅助函数](../sdk/PdfReaderPlugin.cmake)
- [完整插件示例](../plugins/reading_tools/)

SDK 包含接口、示例、文档和构建脚本，不含 PDFium 或阅读器源码。当前发布版使用 **Qt 6.8.3 / MinGW GCC 13.1.0 / x64 / Release / C++17**；插件请使用相同工具链。MSVC、32 位及不匹配的 Qt 构建不能直接混用。

在完整源码仓库中，使用已配置依赖即可独立编译插件：

```powershell
pwsh -NoProfile -File tools/build_plugin.ps1
```

输出为 `build/plugin-development/reader-plugins/reading_tools.dll`；这个构建不链接 `reader_ui` 或 PDFium。

只有解压后的 SDK 时，在 SDK 根目录运行，并替换为自己的工具路径：

```powershell
pwsh -NoProfile -File tools/build_plugin.ps1 `
  -QtPrefix "C:\Qt\6.8.3\mingw_64" `
  -Compiler "C:\Qt\Tools\mingw1310_64\bin\g++.exe" `
  -CMake "C:\Tools\CMake\bin\cmake.exe" `
  -Ninja "C:\Tools\Ninja\ninja.exe"
```

也可直接构建：

```powershell
cmake -G Ninja -S plugins/reading_tools -B build/my-plugin `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\mingw_64" `
  -DCMAKE_CXX_COMPILER="C:\Qt\Tools\mingw1310_64\bin\g++.exe"
cmake --build build/my-plugin
```

开发新插件时，复制示例目录，修改 JSON 的 **Id、Name、Version** 和 CMake 目标名。Id 必须唯一，例如 `com.example.pdfnotes`；示例 Id 已被内置插件占用，不能重复安装。

## 可选选区坐标能力（阅读器 0.4.1 起）

SDK 1.1 保持 `HostV1` / `PluginV1` 的虚函数表和 API 1 IID 不变，新增独立的 `SelectionGeometryV1` 能力接口。对 `host->context()` 使用 `qobject_cast<pdfreader::SelectionGeometryV1*>` 检测支持，再调用 `selectionScreenRect()`。返回值使用屏幕全局逻辑像素，已裁剪到可见 PDF 视口；无选区或选区滚出视野时返回空矩形。坐标随滚动、缩放、单页模式和窗口移动改变，不是物理像素。浮窗应在显示期间重新查询位置，并使用对应屏幕的 `availableGeometry()` 避免越界。

旧阅读器返回空能力指针；插件必须处理缺少该能力的情况。新阅读器仍可加载旧 API 1 插件，旧版 SDK 下载地址保留作为兼容入口。

## 最小插件示例

元数据 `my_plugin.json` 在编译时嵌入 DLL，不需单独复制：

```json
{
  "Id": "com.example.pdfnotes",
  "Name": "我的阅读插件",
  "Version": "1.0.0",
  "ApiVersion": 1,
  "Description": "演示独立扩展功能。"
}
```

```cpp
#include "pdfreader_plugin.h"
#include <QObject>

class MyPlugin : public QObject, public pdfreader::PluginV1 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PDFREADER_PLUGIN_IID FILE "my_plugin.json")
    Q_INTERFACES(pdfreader::PluginV1)
public:
    bool initialize(pdfreader::HostV1* host, QString*) override {
        host_ = host;
        return host_->addCommand("bookmark", tr("收藏当前页"), [this] {
            host_->addBookmark(tr("插件收藏"));
        }) != nullptr;
    }
    void shutdown() override { host_ = nullptr; }
private:
    pdfreader::HostV1* host_ = nullptr;
};
#include "my_plugin.moc"
```

若插件位于 SDK 的 `plugins/my_plugin/`，独立 `CMakeLists.txt` 如下：

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyPDFPlugin LANGUAGES CXX)
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets)
include("${CMAKE_CURRENT_LIST_DIR}/../../sdk/PdfReaderPlugin.cmake")
add_pdfreader_plugin(my_plugin my_plugin.cpp my_plugin.json)
```

## 宿主接口

| 接口 | 用途 |
| --- | --- |
| `document()` | 当前文档路径、标题、总页数、当前页；无文档时路径为空、页数及页码为 0 |
| `pageText(pageNumber)` | 按需读取一页文字；页码从 **1** 开始，无效页返回空字符串；不提供 OCR |
| `selectedText()` | 当前选中文字，无选择时为空 |
| `openDocument(path)` / `goToPage(pageNumber)` | 打开文档、跳转到从 1 开始的物理页码 |
| `addBookmark(title)` | 保存当前页个人书签，与内置书签共用持久化逻辑 |
| `addCommand(id, title, callback)` | 在本插件菜单下注册命令，返回宿主拥有的 `QAction*` |
| `addPanel(id, title, content)` | 注册可停靠面板；成功后宿主接管内容控件，失败时仍由插件释放 |
| `setting(key, fallback)` / `setSetting(key, value)` | 按插件 Id 隔离的持久配置 |
| `dataDirectory()` | 创建并返回独立数据目录，失败时为空字符串 |
| `context()` | 为计时器、连接和其他 QObject 提供生命周期父对象 |
| `window()` / `showMessage(text, timeout)` | 对话框父窗口、状态提示 |

命令和面板 id 在同一插件内不能重复；注册失败返回空指针。宿主不暴露 PDFium 指针或内部窗口类型，避免插件依赖核心实现。

## 生命周期与线程

EPUB 试用版复用 API 1：`DocumentInfo` 的页码 / 页数及 `goToPage()` 表示章节。
`selectedText()` 和可选选区屏幕坐标接口可直接用于 EPUB；`pageText()` 返回已加载过的
章节文字（近期最多 16 节），尚未加载或已移出缓存时为空。插件不要假设所有文档都有固定书页或 PDFium 后端。
详见 [EPUB 方案与能力范围](epub-design.md)。

1. 扫描仅读取嵌入元数据。错误 IID、API 不兼容或元数据无效时，不执行插件代码。
2. 启用时调用 `initialize(host, error)`，成功后立即发送当前的 `documentChanged(info)`。
3. 打开、切换、关闭文档触发 `documentChanged`；页码变化触发 `pageChanged`；选区变化触发 `selectionChanged`。事件在 GUI 线程合并投递，不保证发送每个中间滚动位置。需要即时快照时调用 `document()`。
4. 禁用、初始化失败和退出时均调用 `shutdown()`；该函数须支持只完成部分初始化的情况。插件应在此取消异步任务、停止并等待自己的工作线程。
5. 宿主随后销毁注册 UI、上下文子对象和连接，再释放插件根对象。不要自行删除宿主窗口或上下文，也不要在关闭后继续使用 `host`。

所有宿主接口和回调只能在 GUI 线程使用。不要在滚动回调中扫描全文。耗时工作放到后台，将结果通过以 `host->context()` 为接收上下文的排队连接送回主线程，并在关闭时取消。不要从后台线程调用宿主接口或直接操作宿主 PDFium；需要独立 PDF 处理时使用单独进程。

初始化、宿主命令回调和文档事件中的 C++ 异常会停用插件并回收注册 UI。访问越界、进程崩溃及插件自己直接连接的 Qt 槽函数异常不受此保护；插件必须自行处理。

## 兼容性和分发

- **API 1 / IID `org.pdfreader.Plugin/1.0` 的现有虚函数、顺序和数据结构保持冻结。** 不兼容扩展使用新的 API/IID，不静默改变现有 ABI。
- API 相同仍需匹配编译器、架构和 Qt 运行库，工具链升级时可能需重新编译。这不是跨编译器的 C ABI。
- 当前管理器安装单个 DLL。资源可嵌入 Qt `.qrc`；额外原生依赖需自行静态链接或另行部署，管理器不自动下载依赖。
- 功能插件不同于 Qt 平台插件。内置功能插件在程序旁的 `reader-plugins/`；用户插件在配置目录的 `plugins/`。
- 使用 Qt 默认防卸载策略；禁用会销毁实例和注册 UI，但不保证本次进程解除 DLL 占用，替换文件应在退出后进行。

实现参考 Qt 官方的 [应用插件接口](https://doc.qt.io/qt-6.8/plugins-howto.html)、[QPluginLoader](https://doc.qt.io/qt-6.8/qpluginloader.html) 和 [部署兼容性说明](https://doc.qt.io/qt-6.8/deployment-plugins.html)。

## 验证

```powershell
pwsh -NoProfile -File tools/verify.ps1 -Corpus "C:\path\to\pdfs"
.\build\pdfreader.exe --selftest-plugins "C:\path\to\text.pdf"
```

真实 DLL 回归测试覆盖扫描、安装、启用持久化、接口版本拒绝、文档事件、文字访问、菜单和面板、异常回滚、禁用清理、重启移除及独立 SDK 构建。验证脚本隔离用户配置；手工使用 `--selftest-plugins` 时，也应设置 `PDFREADER_SETTINGS` 指向临时配置。
