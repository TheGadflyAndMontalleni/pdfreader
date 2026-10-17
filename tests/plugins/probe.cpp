#include "pdfreader_plugin.h"
#include <QAction>
#include <QCoreApplication>
#include <QEventLoop>
#include <QLabel>
#include <QObject>
#include <QTimer>
#include <stdexcept>

class Probe : public QObject, public pdfreader::PluginV1 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PDFREADER_PLUGIN_IID FILE FIXTURE_METADATA)
    Q_INTERFACES(pdfreader::PluginV1)
public:
    Probe() { qApp->setProperty("pluginConstructors", qApp->property("pluginConstructors").toInt() + 1); }
    ~Probe() override { qApp->setProperty("pluginDestructors", qApp->property("pluginDestructors").toInt() + 1); }
    bool initialize(pdfreader::HostV1* host, QString* error) override {
        host_ = host;
        host_->setSetting("initializations", host_->setting("initializations", 0).toInt() + 1);
        host_->setSetting("dataDirectory", host_->dataDirectory());
        auto command = [this](const QString& id, std::function<void()> callback) {
            return host_->addCommand(id, id, std::move(callback));
        };
        command("navigate", [this] { host_->setSetting("navigationResult", host_->goToPage(2)); });
        command("bookmark", [this] { host_->addBookmark(QString::fromUtf8("插件书签")); });
        command("inspect", [this] {
            host_->setSetting("text", host_->pageText(host_->document().currentPage));
            host_->setSetting("selection", host_->selectedText());
            host_->setSetting("invalidPagesRejected", !host_->goToPage(0) && !host_->goToPage(host_->document().pageCount + 1) &&
                              host_->pageText(0).isEmpty() && host_->pageText(host_->document().pageCount + 1).isEmpty());
        });
        command("open", [this] { host_->openDocument(host_->setting("nextDocument").toString()); });
        command("modal", [this] {
            inCommand_ = true;
            host_->goToPage(host_->document().currentPage == 1 ? 2 : 1);
            QEventLoop nested;
            QTimer::singleShot(100, &nested, &QEventLoop::quit);
            nested.exec();
            inCommand_ = false;
        });
        command("throw", [] { throw std::runtime_error("intentional plugin command failure"); });
        auto* label = new QLabel(QStringLiteral("probe"));
        host_->addPanel("probe-panel", QStringLiteral("probe-panel"), label);
        auto* timer = new QTimer(host_->context());
        timer->setInterval(5);
        connect(timer, &QTimer::timeout, host_->context(), [] { qApp->setProperty("pluginTicks", qApp->property("pluginTicks").toInt() + 1); });
        timer->start();
#if FIXTURE_MODE == 1
        *error = "intentional initialization failure";
        return false;
#elif FIXTURE_MODE == 2
        throw std::runtime_error("intentional initialization exception");
#else
        return true;
#endif
    }
    void shutdown() override {
        if (host_) host_->setSetting("shutdowns", host_->setting("shutdowns", 0).toInt() + 1);
        host_ = nullptr;
    }
    void documentChanged(const pdfreader::DocumentInfo& doc) override {
        host_->setSetting("document", doc.path);
        host_->setSetting("pageCount", doc.pageCount);
        host_->setSetting("page", doc.currentPage);
    }
    void pageChanged(int page) override {
        if (inCommand_) host_->setSetting("callbackReentered", true);
        host_->setSetting("page", page);
    }
    void selectionChanged() override { host_->setSetting("selection", host_->selectedText()); }
private:
    pdfreader::HostV1* host_ = nullptr;
    bool inCommand_ = false;
};
#include "probe.moc"
