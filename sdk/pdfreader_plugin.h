// PDF Reader Plugin SDK 1.1. HostV1 and PluginV1 remain frozen; new capabilities are optional.
#pragma once

#include <QtPlugin>
#include <QString>
#include <QVariant>
#include <QRect>
#include <functional>

class QAction;
class QDockWidget;
class QObject;
class QWidget;

namespace pdfreader {

inline constexpr int PluginApiVersion = 1;

struct DocumentInfo {
    QString path;
    QString title;
    int pageCount = 0;
    int currentPage = 0; // One-based; zero means no document.
};

// All calls and callbacks run on the GUI thread. Do expensive work separately;
// never retain document pointers or call this host from a background thread.
class HostV1 {
public:
    virtual ~HostV1() = default;
    virtual QWidget* window() const = 0;
    virtual QObject* context() const = 0; // Parent timers/connections to this scope.
    virtual DocumentInfo document() const = 0;
    virtual QString selectedText() const = 0;
    virtual QString pageText(int pageNumber) const = 0;
    virtual bool openDocument(const QString& path) = 0;
    virtual bool goToPage(int pageNumber) = 0;
    virtual bool addBookmark(const QString& title = {}) = 0;
    // IDs are unique within one plugin. The reader owns returned UI objects.
    virtual QAction* addCommand(const QString& id, const QString& title,
                                std::function<void()> callback) = 0;
    // Ownership of content transfers only on success.
    virtual QDockWidget* addPanel(const QString& id, const QString& title, QWidget* content) = 0;
    virtual QString dataDirectory() const = 0;
    virtual QVariant setting(const QString& key, const QVariant& fallback = {}) const = 0;
    virtual void setSetting(const QString& key, const QVariant& value) = 0;
    virtual void showMessage(const QString& message, int timeoutMs = 5000) = 0;
};

class PluginV1 {
public:
    virtual ~PluginV1() = default;
    // A false result or C++ exception rolls back registered UI and connections.
    virtual bool initialize(HostV1* host, QString* error) = 0;
    // Called on disable, failed initialization and application exit. Stop workers
    // and release resources here. It must also handle partial initialization.
    virtual void shutdown() = 0;
    virtual void documentChanged(const DocumentInfo&) {}
    virtual void pageChanged(int /* one-based pageNumber */) {}
    virtual void selectionChanged() {}
};

// Optional capability, discovered with qobject_cast on HostV1::context().
// Added without changing the frozen HostV1/PluginV1 vtables. Coordinates are
// global logical pixels, clipped to the visible PDF viewport; empty = offscreen.
class SelectionGeometryV1 {
public:
    virtual ~SelectionGeometryV1() = default;
    virtual QRect selectionScreenRect() const = 0;
};

} // namespace pdfreader

#define PDFREADER_PLUGIN_IID "org.pdfreader.Plugin/1.0"
Q_DECLARE_INTERFACE(pdfreader::PluginV1, PDFREADER_PLUGIN_IID)
Q_DECLARE_INTERFACE(pdfreader::SelectionGeometryV1, "org.pdfreader.SelectionGeometry/1.0")
