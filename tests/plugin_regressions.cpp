#include "mainwindow.h"
#include "pdfview.h"
#include "pluginmanager.h"
#include "tocworker.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <cstdio>

namespace {
QString Group(const QString& id) {
    return "plugin-settings/p-" + QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex());
}
QVariant Value(const QString& id, const QString& key) {
    QSettings settings(app::Session::SettingsPath(), QSettings::IniFormat);
    return settings.value(Group(id) + "/" + key);
}
} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("PDF Reader");
    // The offscreen platform has no Windows font discovery. Load an installed
    // font explicitly so UI artifacts can be inspected with Chinese labels.
    const auto fontFile = qEnvironmentVariable("WINDIR") + "/Fonts/msyh.ttc";
    if (QFile::exists(fontFile)) {
        const int fontId = QFontDatabase::addApplicationFont(fontFile);
        const auto families = QFontDatabase::applicationFontFamilies(fontId);
        if (!families.isEmpty()) app.setFont(QFont(families.first(), 9));
    }
    const auto args = app.arguments();
    if (args.size() == 3 && args[1] == "--toc-worker") return app::RunTocWorker(args[2]);
    if (args.size() == 3 && args[1] == "--check-plugin-enabled") {
        MainWindow window;
        window.pluginManager()->loadEnabled();
        for (const auto& info : window.pluginManager()->plugins())
            if (info.id == args[2] && info.active) return 0;
        return 1;
    }
    if (args.size() == 3 && args[1] == "--remove-plugin") {
        MainWindow window;
        window.pluginManager()->discover();
        QString error;
        return window.pluginManager()->remove(args[2], &error) ? 0 : 1;
    }
    if (args.size() == 3 && args[1] == "--check-no-autoload") {
        MainWindow window(nullptr, false);
        QTest::qWait(350);
        window.pluginManager()->discover();
        for (const auto& info : window.pluginManager()->plugins())
            if (info.id == args[2] && info.active) return 1;
        QSettings settings(app::Session::SettingsPath(), QSettings::IniFormat);
        return settings.value("plugins/enabled").toStringList().contains(args[2]) ? 0 : 1;
    }
    if (args.size() < 3) return 2;
    QTemporaryDir temp;
    if (!temp.isValid()) return 2;
    qputenv("PDFREADER_SETTINGS", (temp.path() + "/settings.ini").toUtf8());
    int failures = 0, checks = 0;
    auto check = [&](bool ok, const char* message) {
        ++checks; if (!ok) ++failures;
        std::printf("%s  %s\n", ok ? "PASS" : "FAIL", message); std::fflush(stdout);
    };
    const QString probeId = "org.pdfreader.tests.probe";
    const QString exampleId = "org.pdfreader.reading-tools";
    auto window = std::make_unique<MainWindow>();
    window->setAttribute(Qt::WA_DontShowOnScreen);
    window->show(); app.processEvents();
    auto* manager = window->pluginManager();
    QDir().mkpath(manager->userDirectory());
    const auto fixtures = QCoreApplication::applicationDirPath() + "/plugin-fixtures/";
    for (const auto& file : QDir(fixtures).entryInfoList(QDir::Files))
        QFile::copy(file.absoluteFilePath(), manager->userDirectory() + "/" + file.fileName());
    QFile corrupt(manager->userDirectory() + "/broken.dll"); corrupt.open(QIODevice::WriteOnly); corrupt.write("broken plugin"); corrupt.close();
    manager->discover();
    const auto catalog = manager->plugins();
    check(catalog.size() >= 7 && qApp->property("pluginConstructors").toInt() == 0,
          "discovery reads real DLL metadata without constructing disabled plugins");
    check(window->findChild<QAction*>("managePlugins") != nullptr, "the Plugins menu exposes plugin management");
    QString error;
    check(!manager->setEnabled("org.pdfreader.tests.incompatible", true, &error) && qApp->property("pluginConstructors").toInt() == 0,
          "an unsupported API version is rejected before executing its code");
    check(!manager->setEnabled("org.pdfreader.tests.wrong-interface", true, &error), "a DLL with a false interface declaration is rejected");
    check(manager->remove(corrupt.fileName(), &error) && !QFile::exists(corrupt.fileName()), "a corrupt user DLL can be removed without recursive file deletion");
    check(manager->setEnabled(probeId, true, &error), "a real C++ plugin initializes through the public SDK");
    check(Value(probeId, "pageCount").toInt() == 0 && Value(probeId, "page").toInt() == 0,
          "initial notification reports an empty reader without invalid PDF access");
    check(QDir(Value(probeId, "dataDirectory").toString()).exists(), "each plugin receives a persistent data directory");
    auto action = [&](const QString& name) { return window->findChild<QAction*>("plugin:" + probeId + "/" + name); };
    QPointer<QAction> command = action("navigate");
    QPointer<QDockWidget> dock = window->findChild<QDockWidget*>("plugin:" + probeId + "/probe-panel");
    check(command && dock, "plugin commands and dock panels appear in the real reader UI");
    check(window->openDocument(args[1]), "opens a PDF while the plugin is active");
    QTest::qWait(180);
    check(Value(probeId, "document").toString() == args[1] && Value(probeId, "pageCount").toInt() > 1,
          "document-open notifications expose the current file and page count");
    action("navigate")->trigger(); QTest::qWait(180);
    check(Value(probeId, "navigationResult").toBool() && window->pluginDocument().currentPage == 2 && Value(probeId, "page").toInt() == 2,
          "one-based SDK navigation updates both the reader and plugin notifications");
    action("modal")->trigger(); QTest::qWait(100);
    check(!Value(probeId, "callbackReentered").toBool() && Value(probeId, "page").toInt() == window->pluginDocument().currentPage,
          "a nested plugin dialog defers page callbacks until the active command returns");
    action("bookmark")->trigger();
    check(window->bookmarkCount() == 1 && app::Session::LoadBookmarks(args[1])[0].title == QString::fromUtf8("插件书签"),
          "plugins add real persistent personal bookmarks");
    window->selectAllText(); action("inspect")->trigger(); app.processEvents();
    check(!Value(probeId, "text").toString().isEmpty() && !Value(probeId, "selection").toString().isEmpty() &&
          Value(probeId, "invalidPagesRejected").toBool(), "SDK text and selection access work and invalid page numbers are rejected");
    {
        QSettings settings(app::Session::SettingsPath(), QSettings::IniFormat);
        settings.setValue(Group(probeId) + "/nextDocument", args[2]); settings.sync();
    }
    action("open")->trigger(); QTest::qWait(180);
    check(window->docTabCount() == 2 && Value(probeId, "document").toString() == args[2], "plugins open a second document and receive its snapshot");
    window->activateDocTab(0); app.processEvents();
    check(Value(probeId, "document").toString() == args[1], "switching document tabs notifies the plugin");
    window->closeDocTab(1); window->closeDocTab(0); app.processEvents();
    check(Value(probeId, "pageCount").toInt() == 0 && Value(probeId, "document").toString().isEmpty(),
          "closing the last document sends a valid empty-document notification");
    check(manager->setEnabled(probeId, false, &error), "an enabled plugin can be disabled during the session");
    const int ticks = qApp->property("pluginTicks").toInt(); QTest::qWait(40);
    check(!command && !dock && qApp->property("pluginTicks").toInt() == ticks && Value(probeId, "shutdowns").toInt() == 1,
          "disable removes commands, panels, timers and plugin-owned connections before releasing the DLL object");
    check(!manager->remove(probeId, &error) && error.contains(QString::fromUtf8("重启")), "loaded DLL removal requires a restart instead of unsafe file replacement");
    check(manager->setEnabled(probeId, true, &error) && Value(probeId, "initializations").toInt() == 2,
          "a disabled plugin can be initialized again without duplicate commands");
    QProcess persistence;
    persistence.start(QCoreApplication::applicationFilePath(), {"--check-plugin-enabled", probeId});
    check(persistence.waitForFinished(10000) && persistence.exitCode() == 0, "a separate process restores the persisted enabled state");
    persistence.start(QCoreApplication::applicationFilePath(), {"--check-no-autoload", probeId});
    check(persistence.waitForFinished(10000) && persistence.exitCode() == 0, "recovery startup skips enabled plugins without erasing the saved preference");
    action("throw")->trigger();
    check(QTest::qWaitFor([&] { return !action("navigate"); }, 1000),
          "a C++ exception in a command disables the plugin and cleans up its UI");
    for (const auto& id : {QString("org.pdfreader.tests.failing"), QString("org.pdfreader.tests.throwing")}) {
        check(!manager->setEnabled(id, true, &error) && !window->findChild<QDockWidget*>("plugin:" + id + "/probe-panel") &&
              !window->findChild<QAction*>("plugin:" + id + "/navigate"), "failed and throwing initialization roll back partially registered UI");
    }
    QString installedId;
    check(!manager->install(fixtures + "plugin_fixture_probe.dll", &installedId, &error), "install refuses duplicate plugin IDs");
    check(!manager->install(fixtures + "plugin_fixture_incompatible.dll", &installedId, &error), "install rejects an incompatible plugin without copying it");
    check(manager->setEnabled(exampleId, true, &error), "the bundled reading-tools example loads through the same public interface");
    window->openDocument(args[1]); QTest::qWait(180);
    auto* copy = window->findChild<QAction*>("plugin:" + exampleId + "/copy-page");
    if (copy) copy->trigger();
    check(copy && QApplication::clipboard()->text() == window->pluginPageText(window->pluginDocument().currentPage),
          "the example plugin copies the actual current-page text");
    const auto artifacts = QString::fromLocal8Bit(qgetenv("PDFREADER_TEST_ARTIFACTS"));
    if (!artifacts.isEmpty()) { QDir().mkpath(artifacts); window->grab().save(artifacts + "/plugin-example.png"); }
    manager->setEnabled(exampleId, false);
    bool dialogSeen = false;
    QTimer::singleShot(0, window.get(), [&] {
        auto* dialog = window->findChild<QDialog*>("pluginManagerDialog");
        dialogSeen = dialog && dialog->findChild<QTreeWidget*>("pluginList");
        if (dialog && !artifacts.isEmpty()) dialog->grab().save(artifacts + "/plugin-manager.png");
        if (dialog) dialog->reject();
    });
    manager->showDialog();
    check(dialogSeen, "the real plugin-management dialog opens and lists discovered plugins");
    if (args.size() >= 4) {
        check(manager->install(args[3], &installedId, &error), "a plugin built outside the reader installs through the public installation API");
        check(manager->setEnabled(installedId, true, &error) && window->findChild<QAction*>("plugin:" + installedId + "/copy-page"),
              "an independently compiled SDK plugin runs without linking the reader source");
        manager->setEnabled(installedId, false);
    }
    window.reset();
    check(qApp->property("pluginConstructors").toInt() == qApp->property("pluginDestructors").toInt(),
          "application destruction shuts down and releases every plugin root object");
    // PDFium/V8 has one lifetime per process. Test a genuine restart using an
    // independent plugin copy, not a second MainWindow after destroying PDFium.
    const auto restartRoot = temp.path() + "/restart";
    QDir().mkpath(restartRoot + "/plugins");
    QFile::copy(fixtures + "plugin_fixture_probe.dll", restartRoot + "/plugins/probe.dll");
    const auto restartSettings = restartRoot + "/settings.ini";
    { QSettings settings(restartSettings, QSettings::IniFormat); settings.setValue("plugins/enabled", QStringList{probeId}); settings.sync(); }
    auto childEnvironment = QProcessEnvironment::systemEnvironment();
    childEnvironment.insert("PDFREADER_SETTINGS", restartSettings);
    persistence.setProcessEnvironment(childEnvironment);
    persistence.start(QCoreApplication::applicationFilePath(), {"--check-plugin-enabled", probeId});
    const bool restarted = persistence.waitForFinished(10000) && persistence.exitCode() == 0;
    persistence.start(QCoreApplication::applicationFilePath(), {"--remove-plugin", probeId});
    check(restarted && persistence.waitForFinished(10000) && persistence.exitCode() == 0 && !QFile::exists(restartRoot + "/plugins/probe.dll"),
          "a user plugin loaded in a previous process can be removed after a genuine restart");
    check(Value(probeId, "initializations").toInt() >= 2 && QDir(Value(probeId, "dataDirectory").toString()).exists(),
          "plugin removal preserves its settings and data directory");
    std::printf("PLUGIN REGRESSIONS %s (%d checks, %d failures)\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
