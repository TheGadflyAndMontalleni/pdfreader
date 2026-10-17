#include "pluginmanager.h"
#include "mainwindow.h"
#include "session.h"
#include "pdfreader_plugin.h"

#include <QAction>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QLibrary>
#include <QMenu>
#include <QMessageBox>
#include <QPluginLoader>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <exception>

namespace {
QString StorageKey(const QString& id) {
    return "p-" + QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex());
}
bool Error(QString* output, const QString& message) {
    if (output) *output = message;
    return false;
}
app::PluginInfo Inspect(const QString& path, bool userInstalled) {
    app::PluginInfo info;
    info.path = QFileInfo(path).absoluteFilePath();
    info.name = QFileInfo(path).fileName();
    info.userInstalled = userInstalled;
    QPluginLoader loader(info.path);
    const auto root = loader.metaData();
    const auto meta = root.value("MetaData").toObject();
    info.id = meta.value("Id").toString();
    if (!meta.value("Name").toString().isEmpty()) info.name = meta.value("Name").toString();
    info.version = meta.value("Version").toString();
    info.description = meta.value("Description").toString();
    static const QRegularExpression identifier("^[a-z][a-z0-9]*(?:[._-][a-z0-9]+)+$");
    if (root.value("IID").toString() != QLatin1String(PDFREADER_PLUGIN_IID))
        info.error = QObject::tr("不是 PDF Reader API 1 插件，或文件已损坏。%1").arg(loader.errorString());
    else if (meta.value("ApiVersion").toInt(-1) != pdfreader::PluginApiVersion)
        info.error = QObject::tr("插件 API 版本不兼容；阅读器当前支持 API 1。");
    else if (!identifier.match(info.id).hasMatch() || info.id.size() > 128 || info.version.isEmpty() || meta.value("Name").toString().isEmpty())
        info.error = QObject::tr("插件缺少有效的 Id、Name 或 Version 元数据。");
    else info.compatible = true;
    return info;
}
} // namespace

// One host scope per enabled plugin. UI, connection contexts and callback
// captures are destroyed before QPluginLoader releases its root object.
class PluginHost : public QObject, public pdfreader::HostV1 {
public:
    PluginHost(app::PluginManager* manager, MainWindow* window, QMenu* menu, const app::PluginInfo& info)
        : QObject(manager), manager_(manager), window_(window), id_(info.id) {
        menu_ = menu->addMenu(info.name);
        menu_->setObjectName("pluginMenu:" + id_);
    }
    ~PluginHost() override {
        for (auto& dock : docks_) if (dock) { window_->removeDockWidget(dock); delete dock; }
        delete menu_;
    }
    QWidget* window() const override { return window_; }
    QObject* context() const override { return const_cast<PluginHost*>(this); }
    pdfreader::DocumentInfo document() const override { return window_->pluginDocument(); }
    QString selectedText() const override { return window_->pluginSelectedText(); }
    QString pageText(int page) const override { return window_->pluginPageText(page); }
    bool openDocument(const QString& path) override { return window_->openDocument(path); }
    bool goToPage(int page) override {
        return page > 0 && page <= document().pageCount && window_->setActivePage(page - 1);
    }
    bool addBookmark(const QString& title) override { return window_->addBookmark(title); }
    QAction* addCommand(const QString& id, const QString& title, std::function<void()> callback) override {
        if (id.isEmpty() || title.isEmpty() || !callback || ids_.contains(id)) return nullptr;
        ids_.insert(id);
        auto* action = new QAction(title, this);
        action->setObjectName("plugin:" + id_ + "/" + id);
        menu_->addAction(action);
        connect(action, &QAction::triggered, this, [this, callback = std::move(callback)] {
            QScopedValueRollback<int> guard(manager_->busy_, manager_->busy_ + 1);
            try { callback(); }
            catch (const std::exception& e) { manager_->failLater(id_, QString::fromUtf8(e.what())); }
            catch (...) { manager_->failLater(id_, tr("插件命令抛出异常。")); }
        });
        return action;
    }
    QDockWidget* addPanel(const QString& id, const QString& title, QWidget* content) override {
        if (id.isEmpty() || title.isEmpty() || !content || ids_.contains(id)) return nullptr;
        ids_.insert(id);
        auto* dock = new QDockWidget(title, window_);
        dock->setObjectName("plugin:" + id_ + "/" + id);
        dock->setWidget(content);
        window_->addDockWidget(Qt::RightDockWidgetArea, dock);
        menu_->addAction(dock->toggleViewAction());
        docks_.push_back(dock);
        return dock;
    }
    QString dataDirectory() const override {
        const auto path = QFileInfo(app::Session::SettingsPath()).absolutePath() + "/plugin-data/" + StorageKey(id_);
        return QDir().mkpath(path) ? path : QString();
    }
    QVariant setting(const QString& key, const QVariant& fallback) const override {
        QSettings settings(app::Session::SettingsPath(), QSettings::IniFormat);
        return settings.value("plugin-settings/" + StorageKey(id_) + "/" + key, fallback);
    }
    void setSetting(const QString& key, const QVariant& value) override {
        QSettings settings(app::Session::SettingsPath(), QSettings::IniFormat);
        settings.setValue("plugin-settings/" + StorageKey(id_) + "/" + key, value);
        settings.sync();
    }
    void showMessage(const QString& message, int timeout) override { window_->statusBar()->showMessage(message, timeout); }
private:
    app::PluginManager* manager_;
    MainWindow* window_;
    QString id_;
    QMenu* menu_;
    QSet<QString> ids_;
    QVector<QPointer<QDockWidget>> docks_;
};

namespace app {
struct PluginManager::Entry {
    PluginInfo info;
    std::unique_ptr<QPluginLoader> loader;
    std::unique_ptr<PluginHost> host;
    pdfreader::PluginV1* plugin = nullptr;
};

PluginManager::PluginManager(MainWindow* window, QMenu* menu) : QObject(window), window_(window), menu_(menu) {}
PluginManager::~PluginManager() { shutdown(); }
QString PluginManager::userDirectory() const { return QFileInfo(Session::SettingsPath()).absolutePath() + "/plugins"; }
QVector<PluginInfo> PluginManager::plugins() const {
    QVector<PluginInfo> result;
    for (const auto& entry : entries_) result.push_back(entry->info);
    return result;
}
PluginManager::Entry* PluginManager::find(const QString& id) const {
    for (const auto& entry : entries_) if (entry->info.id == id && entry->info.compatible) return entry.get();
    return nullptr;
}
void PluginManager::discover() {
    if (stopped_ || busy_) return;
    // User plugins take precedence over bundled examples with the same ID.
    const QStringList roots{userDirectory(), QCoreApplication::applicationDirPath() + "/reader-plugins"};
    for (int root = 0; root < roots.size(); ++root) {
        for (const auto& file : QDir(roots[root]).entryInfoList(QDir::Files | QDir::NoSymLinks, QDir::Name)) {
            if (!QLibrary::isLibrary(file.fileName())) continue;
            bool seen = false;
            for (const auto& entry : entries_) if (entry->info.path == file.absoluteFilePath()) seen = true;
            if (seen) continue;
            auto entry = std::make_unique<Entry>();
            entry->info = Inspect(file.absoluteFilePath(), root == 0);
            if (entry->info.compatible && find(entry->info.id)) {
                entry->info.compatible = false;
                entry->info.error = tr("存在相同 ID 的插件，仅使用先发现的版本。请移除重复项并重启。");
            }
            entry->loader = std::make_unique<QPluginLoader>(entry->info.path);
            entries_.push_back(std::move(entry));
        }
    }
    emit changed();
}
void PluginManager::loadEnabled() {
    if (stopped_ || busy_) return;
    discover();
    QSettings settings(Session::SettingsPath(), QSettings::IniFormat);
    const auto enabled = settings.value("plugins/enabled").toStringList();
    for (const auto& id : enabled) setEnabled(id, true);
}
void PluginManager::persistEnabled(const QString& id, bool enabled) {
    QSettings settings(Session::SettingsPath(), QSettings::IniFormat);
    auto ids = settings.value("plugins/enabled").toStringList();
    ids.removeAll(id);
    if (enabled) ids.append(id);
    settings.setValue("plugins/enabled", ids);
    settings.sync();
}
void PluginManager::deactivate(Entry& entry) {
    if (entry.plugin) {
        try { entry.plugin->shutdown(); }
        catch (...) { entry.info.error = tr("插件关闭时抛出异常。"); }
    }
    entry.host.reset();
    entry.plugin = nullptr;
    if (entry.loader->isLoaded()) entry.loader->unload();
    entry.info.active = false;
}
bool PluginManager::setEnabled(const QString& id, bool enabled, QString* error) {
    if (stopped_ || busy_) return Error(error, tr("插件正在执行，请稍后重试。"));
    auto* entry = find(id);
    if (!entry) return Error(error, tr("未找到兼容的插件。"));
    if (entry->info.active == enabled) {
        if (!enabled) persistEnabled(id, false);
        return true;
    }
    QScopedValueRollback<int> guard(busy_, busy_ + 1);
    if (!enabled) {
        deactivate(*entry);
        persistEnabled(id, false);
        emit changed();
        return true;
    }
    entry->info.error.clear();
    try {
        auto* object = entry->loader->instance();
        entry->info.loadedThisSession = entry->info.loadedThisSession || entry->loader->isLoaded();
        entry->plugin = object ? qobject_cast<pdfreader::PluginV1*>(object) : nullptr;
        if (!entry->plugin) entry->info.error = object ? tr("插件未实现 SDK API 1 接口。") : tr("无法加载插件：%1").arg(entry->loader->errorString());
        else {
            entry->host = std::make_unique<PluginHost>(this, window_, menu_, entry->info);
            if (entry->plugin->initialize(entry->host.get(), &entry->info.error)) {
                entry->plugin->documentChanged(window_->pluginDocument());
                entry->info.active = true;
                persistEnabled(id, true);
                emit changed();
                return true;
            }
            if (entry->info.error.isEmpty()) entry->info.error = tr("插件初始化失败。");
        }
    } catch (const std::exception& e) { entry->info.error = QString::fromUtf8(e.what()); }
      catch (...) { entry->info.error = tr("插件初始化时抛出异常。"); }
    deactivate(*entry);
    persistEnabled(id, false);
    emit changed();
    return Error(error, entry->info.error);
}
bool PluginManager::install(const QString& source, QString* installedId, QString* error) {
    if (stopped_ || busy_) return Error(error, tr("插件正在执行，请稍后重试。"));
    if (!QLibrary::isLibrary(source)) return Error(error, tr("请选择插件动态库文件。"));
    const auto info = Inspect(source, true);
    if (!info.compatible) return Error(error, info.error);
    for (const auto& entry : entries_)
        if (entry->info.id == info.id) return Error(error, tr("该插件 ID 已存在。请先禁用并移除旧插件；已加载的 DLL 需要重启后移除。"));
    if (!QDir().mkpath(userDirectory())) return Error(error, tr("无法创建插件目录。"));
    const auto destination = userDirectory() + "/" + StorageKey(info.id) + "." + QFileInfo(source).suffix();
    if (QFileInfo::exists(destination) || !QFile::copy(source, destination)) return Error(error, tr("无法复制插件文件，或该文件已存在。"));
    discover();
    if (installedId) *installedId = info.id;
    return true;
}
bool PluginManager::remove(const QString& id, QString* error) {
    if (stopped_ || busy_) return Error(error, tr("插件正在执行，请稍后重试。"));
    auto* entry = find(id);
    for (const auto& candidate : entries_) if (candidate->info.path == id) entry = candidate.get();
    if (!entry || !entry->info.userInstalled) return Error(error, tr("只能移除用户安装的插件；内置示例可以禁用。"));
    if (entry->info.active || entry->info.loadedThisSession) return Error(error, tr("请先禁用插件并重启阅读器，再移除 DLL。"));
    // Single file in the dedicated user plugin directory; never recursively delete data.
    if (QFileInfo(entry->info.path).absolutePath() != QDir(userDirectory()).absolutePath() || !QFile::remove(entry->info.path))
        return Error(error, tr("无法移除插件文件。"));
    persistEnabled(id, false);
    for (auto it = entries_.begin(); it != entries_.end(); ++it) if (it->get() == entry) { entries_.erase(it); break; }
    emit changed();
    return true;
}
void PluginManager::failLater(const QString& id, const QString& message) {
    QTimer::singleShot(busy_ ? 50 : 0, this, [this, id, message] {
        if (stopped_) return;
        if (busy_) { failLater(id, message); return; }
        if (auto* entry = find(id)) {
            deactivate(*entry);
            entry->info.error = message;
            persistEnabled(id, false);
            window_->statusBar()->showMessage(tr("插件 %1 已停用：%2").arg(entry->info.name, message), 8000);
            emit changed();
        }
    });
}
void PluginManager::notifyDocumentChanged() { documentDirty_ = true; scheduleNotifications(); }
void PluginManager::notifyPageChanged() { pageDirty_ = true; scheduleNotifications(); }
void PluginManager::notifySelectionChanged() { selectionDirty_ = true; scheduleNotifications(); }
void PluginManager::scheduleNotifications() {
    if (notificationPending_ || stopped_) return;
    notificationPending_ = true;
    QTimer::singleShot(busy_ ? 50 : 0, this, [this] {
        notificationPending_ = false;
        if (stopped_) return;
        // Modal plugin dialogs run nested event loops. Never re-enter plugin
        // callbacks or tear down a host while a plugin command is still active.
        if (busy_) { scheduleNotifications(); return; }
        const bool doc = documentDirty_, page = pageDirty_, selection = selectionDirty_;
        documentDirty_ = pageDirty_ = selectionDirty_ = false;
        const auto snapshot = window_->pluginDocument();
        QScopedValueRollback<int> guard(busy_, busy_ + 1);
        for (const auto& entry : entries_) if (entry->info.active) {
            try {
                if (doc) entry->plugin->documentChanged(snapshot);
                else if (page) entry->plugin->pageChanged(snapshot.currentPage);
                if (selection || doc) entry->plugin->selectionChanged();
            } catch (const std::exception& e) { failLater(entry->info.id, QString::fromUtf8(e.what())); }
              catch (...) { failLater(entry->info.id, tr("插件事件回调抛出异常。")); }
        }
    });
}
void PluginManager::shutdown() {
    if (stopped_) return;
    stopped_ = true;
    for (auto& entry : entries_) deactivate(*entry);
}

void PluginManager::showDialog() {
    discover();
    QDialog dialog(window_);
    dialog.setObjectName("pluginManagerDialog");
    dialog.setWindowTitle(tr("插件管理"));
    dialog.resize(800, 500);
    auto* layout = new QVBoxLayout(&dialog);
    auto* tree = new QTreeWidget(&dialog);
    tree->setObjectName("pluginList");
    tree->setHeaderLabels({tr("插件"), tr("版本"), tr("状态"), tr("来源")});
    tree->setRootIsDecorated(false);
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(tree, 1);
    auto* detail = new QLabel(&dialog);
    detail->setWordWrap(true);
    detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detail->setTextFormat(Qt::PlainText);
    layout->addWidget(detail);
    auto* row = new QHBoxLayout;
    auto* installButton = new QPushButton(tr("安装插件…"), &dialog);
    auto* toggle = new QPushButton(tr("启用"), &dialog);
    auto* removeButton = new QPushButton(tr("移除"), &dialog);
    auto* refresh = new QPushButton(tr("重新扫描"), &dialog);
    for (auto* button : {installButton, toggle, removeButton, refresh}) row->addWidget(button);
    row->addStretch();
    layout->addLayout(row);
    auto* note = new QLabel(tr("原生插件与阅读器在同一进程运行，请只启用可信插件。新插件默认禁用。"), &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto* folder = buttons->addButton(tr("打开插件目录"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    auto updateSelection = [&] {
        const int index = tree->currentIndex().row();
        const auto list = plugins();
        if (index < 0 || index >= list.size()) { detail->setText(tr("选择插件查看说明。")); toggle->setEnabled(false); removeButton->setEnabled(false); return; }
        const auto& info = list[index];
        toggle->setEnabled(info.compatible);
        toggle->setText(info.active ? tr("禁用") : tr("启用"));
        removeButton->setEnabled(info.userInstalled);
        detail->setText(info.description + "\n" + info.path + (info.error.isEmpty() ? "" : "\n" + info.error));
    };
    auto rebuild = [&] {
        const QString selected = tree->currentItem() ? tree->currentItem()->data(0, Qt::UserRole).toString() : QString();
        tree->clear();
        for (const auto& info : plugins()) {
            auto* item = new QTreeWidgetItem(tree, {info.name, info.version,
                !info.error.isEmpty() ? tr("错误") : info.active ? tr("已启用") : tr("已禁用"),
                info.userInstalled ? tr("用户安装") : tr("内置")});
            item->setData(0, Qt::UserRole, info.id);
            item->setData(0, Qt::UserRole + 1, info.path);
            if (info.id == selected) tree->setCurrentItem(item);
        }
        updateSelection();
    };
    connect(tree, &QTreeWidget::itemSelectionChanged, &dialog, updateSelection);
    connect(this, &PluginManager::changed, &dialog, rebuild);
    connect(refresh, &QPushButton::clicked, &dialog, [this] { discover(); });
    connect(folder, &QPushButton::clicked, &dialog, [this] { QDir().mkpath(userDirectory()); QDesktopServices::openUrl(QUrl::fromLocalFile(userDirectory())); });
    connect(installButton, &QPushButton::clicked, &dialog, [&] {
        const auto file = QFileDialog::getOpenFileName(&dialog, tr("安装 PDF Reader 插件"), {}, tr("插件动态库 (*.dll *.so *.dylib)"));
        if (file.isEmpty()) return;
        QString id, error;
        if (!install(file, &id, &error)) QMessageBox::warning(&dialog, tr("安装插件失败"), error);
        else for (int i = 0; i < tree->topLevelItemCount(); ++i)
            if (tree->topLevelItem(i)->data(0, Qt::UserRole).toString() == id) tree->setCurrentItem(tree->topLevelItem(i));
    });
    connect(toggle, &QPushButton::clicked, &dialog, [&] {
        if (!tree->currentItem()) return;
        const auto id = tree->currentItem()->data(0, Qt::UserRole).toString();
        auto* entry = find(id);
        QString error;
        if (entry && !setEnabled(id, !entry->info.active, &error)) QMessageBox::warning(&dialog, tr("插件操作失败"), error);
    });
    connect(removeButton, &QPushButton::clicked, &dialog, [&] {
        if (!tree->currentItem()) return;
        QString error;
        if (!remove(tree->currentItem()->data(0, Qt::UserRole + 1).toString(), &error)) QMessageBox::information(&dialog, tr("移除插件"), error);
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    rebuild();
    dialog.exec();
}
} // namespace app
