#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <memory>
#include <vector>

class MainWindow;
class QMenu;
class PluginHost;

namespace app {

struct PluginInfo {
    QString id, name, version, description, path, error;
    bool compatible = false;
    bool active = false;
    bool userInstalled = false;
    bool loadedThisSession = false;
};

class PluginManager : public QObject {
    Q_OBJECT
public:
    explicit PluginManager(MainWindow* window, QMenu* menu);
    ~PluginManager() override;
    QString userDirectory() const;
    QVector<PluginInfo> plugins() const;
    void discover(); // Reads embedded metadata; does not execute plugin code.
    void loadEnabled();
    bool setEnabled(const QString& id, bool enabled, QString* error = nullptr);
    bool install(const QString& source, QString* installedId, QString* error);
    bool remove(const QString& id, QString* error);
    void showDialog();
    void notifyDocumentChanged();
    void notifyPageChanged();
    void notifySelectionChanged();
    void shutdown();

signals:
    void changed();

private:
    friend class ::PluginHost;
    struct Entry;
    Entry* find(const QString& id) const;
    void deactivate(Entry& entry);
    void failLater(const QString& id, const QString& message);
    void scheduleNotifications();
    void persistEnabled(const QString& id, bool enabled);
    MainWindow* window_;
    QMenu* menu_;
    std::vector<std::unique_ptr<Entry>> entries_;
    int busy_ = 0;
    bool stopped_ = false;
    bool notificationPending_ = false;
    bool documentDirty_ = false, pageDirty_ = false, selectionDirty_ = false;
};

} // namespace app
