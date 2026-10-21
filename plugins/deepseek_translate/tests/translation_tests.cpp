#include "translation_client.h"
#include "translation_controller.h"
#include "credentials.h"
#include "tls_runtime.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPdfWriter>
#include <QPainter>
#include <QPluginLoader>
#include <QPushButton>
#include <QSignalSpy>
#include <QSslSocket>
#include <QScreen>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>
#include <deque>
#include <cstdio>
#include <memory>

using namespace translate;

QByteArray Completion(const QString& language = QStringLiteral("英语"), const QString& meaning = QStringLiteral("n. 假设；假说")) {
    const auto content = QString::fromUtf8(QJsonDocument(QJsonObject{{"source_language", language}, {"translation", meaning}}).toJson(QJsonDocument::Compact));
    return QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{{"finish_reason", "stop"},
        {"message", QJsonObject{{"content", content}}}}}}}).toJson(QJsonDocument::Compact);
}

class Server : public QObject {
public:
    struct Response { int status = 200; QByteArray body = Completion(); int delay = 0; bool hold = false; };
    QTcpServer listener;
    std::deque<Response> responses;
    QList<QByteArray> bodies, headers;
    Server() {
        if (!listener.listen(QHostAddress::LocalHost)) qFatal("Mock server cannot listen");
        connect(&listener, &QTcpServer::newConnection, this, [this] {
            while (auto* socket = listener.nextPendingConnection()) {
                socket->setParent(this);
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    auto data = socket->property("received").toByteArray() + socket->readAll();
                    socket->setProperty("received", data);
                    if (socket->property("responded").toBool()) return;
                    const int split = data.indexOf("\r\n\r\n");
                    if (split < 0) return;
                    int length = 0;
                    for (const auto& line : data.left(split).split('\n'))
                        if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
                    if (data.size() - split - 4 < length) return;
                    socket->setProperty("responded", true);
                    headers.append(data.left(split)); bodies.append(data.mid(split + 4, length));
                    Response response;
                    if (!responses.empty()) { response = responses.front(); responses.pop_front(); }
                    if (response.hold) return;
                    QTimer::singleShot(response.delay, socket, [socket, response] {
                        if (socket->state() != QAbstractSocket::ConnectedState) return;
                        socket->write("HTTP/1.1 " + QByteArray::number(response.status) + " Result\r\nContent-Type: application/json\r\nContent-Length: " +
                            QByteArray::number(response.body.size()) + "\r\nConnection: close\r\n\r\n" + response.body);
                        socket->disconnectFromHost();
                    });
                });
            }
        });
    }
    QUrl url() const { return QUrl(QString("http://127.0.0.1:%1/chat/completions").arg(listener.serverPort())); }
};

class HostScope : public QObject, public pdfreader::SelectionGeometryV1 {
    Q_OBJECT
    Q_INTERFACES(pdfreader::SelectionGeometryV1)
public:
    QRect bounds{200, 100, 80, 20};
    QRect selectionScreenRect() const override { return bounds; }
};

class Host : public pdfreader::HostV1 {
public:
    QMainWindow main;
    HostScope scope;
    QTemporaryDir temp;
    QVariantMap settings;
    QString selection;
    QList<QDockWidget*> docks;
    Host() { main.setAttribute(Qt::WA_DontShowOnScreen); main.resize(1000, 700); main.show(); }
    ~Host() override { for (auto* dock : docks) delete dock; }
    QWidget* window() const override { return const_cast<QMainWindow*>(&main); }
    QObject* context() const override { return const_cast<HostScope*>(&scope); }
    pdfreader::DocumentInfo document() const override { return {"synthetic.pdf", "synthetic.pdf", 2, 1}; }
    QString selectedText() const override { return selection; }
    QString pageText(int) const override { return "THIS FULL PAGE MUST NEVER BE SENT"; }
    bool openDocument(const QString&) override { return false; }
    bool goToPage(int) override { return false; }
    bool addBookmark(const QString&) override { return false; }
    QAction* addCommand(const QString& id, const QString& title, std::function<void()> callback) override {
        auto* action = new QAction(title, &scope); action->setObjectName(id);
        QObject::connect(action, &QAction::triggered, &scope, [callback] { callback(); }); return action;
    }
    QDockWidget* addPanel(const QString& id, const QString& title, QWidget* content) override {
        auto* dock = new QDockWidget(title, &main); dock->setObjectName(id); dock->setWidget(content);
        main.addDockWidget(Qt::RightDockWidgetArea, dock); docks.append(dock); return dock;
    }
    QString dataDirectory() const override { return temp.path(); }
    QVariant setting(const QString& key, const QVariant& fallback = {}) const override { return settings.value(key, fallback); }
    void setSetting(const QString& key, const QVariant& value) override { settings.insert(key, value); }
    void showMessage(const QString&, int) override {}
    void setKey(const QString& key = "synthetic-test-key") {
        settings["apiKeyProtected"] = ProtectKey(key).toBase64();
        settings["provider"] = "deepseek";
    }
};

class Tests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qunsetenv("DEEPSEEK_API_KEY");
        QCoreApplication::setLibraryPaths({});
        const auto directory = QCoreApplication::applicationDirPath() + "/test-tls-runtime";
        QString error;
        QVERIFY2(EnsureTls(directory, &error), qPrintable(error));
        QVERIFY(QSslSocket::supportsSsl());
        QCOMPARE(QSslSocket::activeBackend(), "schannel");
        QVERIFY(QFile::exists(directory + "/qt-tls-6.8.3/tls/qschannelbackend.dll"));
        QPdfWriter pdf(QCoreApplication::applicationDirPath() + "/synthetic-translation-fixture.pdf");
        pdf.setResolution(72);
        QPainter painter(&pdf); painter.setFont(QFont("Arial", 12));
        for (int page = 0; page < 1; ++page) {
            painter.drawText(45, 65, QString("Chapter %1: Synthetic reading test").arg(page + 1));
            painter.drawText(45, 85, QStringLiteral("生词翻译测试文档支持中文检索"));
            for (int line = 0; line < 12; ++line)
                painter.drawText(45, 100 + line * 24, QString("The hypothesis explains a simple observation. Example sentence %1.").arg(line + 1));
        }
        painter.end();
    }
    void credentials() {
        const QString key = "synthetic-key-never-print";
        const auto encrypted = ProtectKey(key);
        QVERIFY(!encrypted.isEmpty()); QVERIFY(!encrypted.contains(key.toUtf8()));
        QCOMPARE(UnprotectKey(encrypted), key);
        QVERIFY(UnprotectKey("invalid cipher").isEmpty());
        QVERIFY(ProtectKey({}).isEmpty());
    }
    void requestAndAutoLanguage() {
        Server server;
        server.responses.push_back({200, Completion(QStringLiteral("法语"), QStringLiteral("谢谢"))});
        TranslationClient client(nullptr, server.url());
        QSignalSpy ok(&client, &TranslationClient::succeeded);
        client.start("merci", "synthetic-test-key", DefaultModel);
        QTRY_COMPARE(ok.count(), 1);
        QCOMPARE(ok[0][0].toString(), QStringLiteral("法语"));
        QCOMPARE(ok[0][1].toString(), QStringLiteral("谢谢"));
        QVERIFY(server.headers[0].toLower().contains("authorization: bearer synthetic-test-key"));
        const auto payload = QJsonDocument::fromJson(server.bodies[0]).object();
        QCOMPARE(payload["model"].toString(), QString(DefaultModel));
        QCOMPARE(payload["thinking"].toObject()["type"].toString(), "disabled");
        QCOMPARE(payload["response_format"].toObject()["type"].toString(), "json_object");
        const auto messages = payload["messages"].toArray();
        QCOMPARE(messages.size(), 2);
        QCOMPARE(messages[1].toObject()["content"].toString(), "merci");
        QVERIFY(messages[0].toObject()["content"].toString().contains("Automatically detect"));
        QVERIFY(messages[0].toObject()["content"].toString().contains("Simplified Chinese"));
        QVERIFY(!server.bodies[0].contains("FULL PAGE"));
    }
    void errors_data() {
        QTest::addColumn<int>("status"); QTest::addColumn<QByteArray>("body"); QTest::addColumn<QString>("expected");
        QTest::newRow("key") << 401 << QByteArray("echo-a-key-is-not-safe") << QStringLiteral("API Key");
        QTest::newRow("balance") << 402 << QByteArray("{}") << QStringLiteral("余额");
        QTest::newRow("limit") << 429 << QByteArray("{}") << QStringLiteral("频繁");
        QTest::newRow("model") << 404 << QByteArray("{}") << QStringLiteral("模型");
        QTest::newRow("server") << 503 << QByteArray("{}") << QStringLiteral("暂时");
        QTest::newRow("bad-json") << 200 << QByteArray("garbage") << QStringLiteral("格式");
        QTest::newRow("empty-content") << 200 << QByteArray("{\"choices\":[]}") << QStringLiteral("格式");
        QTest::newRow("redirect") << 302 << QByteArray("{}") << QStringLiteral("重定向");
        QTest::newRow("large-body") << 200 << QByteArray(70000, 'x') << QStringLiteral("过大");
        QTest::newRow("truncated") << 200 << QByteArray("{\"choices\":[{\"finish_reason\":\"length\",\"message\":{\"content\":\"{}\"}}]}") << QStringLiteral("不完整");
    }
    void errors() {
        QFETCH(int, status); QFETCH(QByteArray, body); QFETCH(QString, expected);
        Server server; server.responses.push_back({status, body});
        TranslationClient client(nullptr, server.url()); QSignalSpy fail(&client, &TranslationClient::failed);
        client.start("hypothesis", "synthetic-test-key", DefaultModel);
        QTRY_COMPARE(fail.count(), 1);
        QVERIFY(fail[0][0].toString().contains(expected));
        QVERIFY(!fail[0][0].toString().contains("echo-a-key"));
    }
    void timeoutAndCancellation() {
        Server server; server.responses.push_back({200, {}, 0, true});
        TranslationClient client(nullptr, server.url(), 100); QSignalSpy fail(&client, &TranslationClient::failed);
        client.start("word", "synthetic-test-key", DefaultModel);
        QTRY_COMPARE(fail.count(), 1); QVERIFY(!client.pending());
        server.responses.push_back({200, Completion("英语", "obsolete"), 250});
        QSignalSpy success(&client, &TranslationClient::succeeded);
        client.start("old", "synthetic-test-key", DefaultModel); client.cancel();
        QTest::qWait(300); QCOMPARE(success.count(), 0);
    }
    void limits() {
        QVERIFY(ShortSelection(QString(201, 'a')).isEmpty());
        QVERIFY(ShortSelection(QString("word ").repeated(31)).isEmpty());
        QCOMPARE(ShortSelection("  bonjour\n monde  "), "bonjour monde");
        Server server; TranslationClient client(nullptr, server.url()); QSignalSpy fail(&client, &TranslationClient::failed);
        QVERIFY(ReadingSelection(QString(8001, 'a')).isEmpty());
        QVERIFY(!ReadingSelection(QString(3000, 'a')).isEmpty());
        client.start(QString(8001, 'a'), "synthetic-test-key", DefaultModel);
        QCOMPARE(fail.count(), 1); QVERIFY(server.bodies.isEmpty());
        client.start("word", "", DefaultModel); QCOMPARE(fail.count(), 2); QVERIFY(server.bodies.isEmpty());
    }
    void manualCachingAndSettings() {
        Host host; host.setKey(); host.selection = "hypothesis";
        Server server; TranslationController controller(&host, server.url()); QString error;
        QVERIFY2(controller.initialize(&error), qPrintable(error));
        auto* pane = host.main.findChild<QWidget*>("translationGlassPopup");
        auto* result = pane->findChild<QPlainTextEdit*>("translationResult");
        auto* autoCheck = pane->findChild<QCheckBox*>("translationAuto");
        QVERIFY(!autoCheck->isChecked());
        controller.selectionChanged(); QTest::qWait(600); QCOMPARE(server.bodies.size(), 0);
        controller.translateSelection();
        QTRY_VERIFY(!result->toPlainText().isEmpty()); QCOMPARE(server.bodies.size(), 1);
        controller.translateSelection(); QCOMPARE(server.bodies.size(), 1);
        QTest::mouseClick(pane->findChild<QPushButton*>("translationCopy"), Qt::LeftButton);
        QCOMPARE(QApplication::clipboard()->text(), result->toPlainText());
        controller.showSettings();
        auto* dialog = host.main.findChild<QDialog*>("deepseekSettingsDialog"); QVERIFY(dialog);
        auto* field = dialog->findChild<QLineEdit*>("deepseekApiKey");
        QCOMPARE(field->echoMode(), QLineEdit::Password);
        field->setText("another-synthetic-key");
        QTest::mouseClick(dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save), Qt::LeftButton);
        const auto cipher = QByteArray::fromBase64(host.settings["apiKeyProtected"].toByteArray());
        QCOMPARE(UnprotectKey(cipher), "another-synthetic-key");
        QVERIFY(!host.settings["apiKeyProtected"].toByteArray().contains("another-synthetic-key"));
        controller.documentChanged(); QVERIFY(!pane->isVisible());
        const auto artifacts = qEnvironmentVariable("PDFREADER_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) {
            QDir().mkpath(artifacts);
            controller.translateSelection(); QTest::qWait(100);
            pane->grab().save(artifacts + "/deepseek-translation.png");
        }
    }
    void automaticDebounceAndStaleResults() {
        Host host; host.setKey(); Server server;
        TranslationController controller(&host, server.url()); QString error;
        QVERIFY2(controller.initialize(&error), qPrintable(error));
        auto* pane = host.main.findChild<QWidget*>("translationGlassPopup");
        pane->findChild<QCheckBox*>("translationAuto")->setChecked(true);
        host.selection = "hyp"; controller.selectionChanged(); QTest::qWait(180);
        host.selection = "hypothesis"; // SDK 1 reports selection presence, not each drag position.
        QTRY_COMPARE_WITH_TIMEOUT(server.bodies.size(), 1, 2000);
        QCOMPARE(QJsonDocument::fromJson(server.bodies[0]).object()["messages"].toArray()[1].toObject()["content"].toString(), "hypothesis");
        server.responses.push_back({200, Completion("英语", "OLD"), 500});
        host.selection = "old"; controller.translateSelection();
        QTRY_COMPARE(server.bodies.size(), 2);
        host.selection = "new"; controller.selectionChanged();
        QTRY_COMPARE_WITH_TIMEOUT(server.bodies.size(), 3, 2000);
        auto* result = pane->findChild<QPlainTextEdit*>("translationResult");
        QTRY_VERIFY(!result->toPlainText().isEmpty()); QVERIFY(result->toPlainText() != "OLD");
        controller.stop(); const auto requests = server.bodies.size();
        host.selection = "after shutdown"; QTest::qWait(800); QCOMPARE(server.bodies.size(), requests);
    }
    void nativePluginLoadAndCleanup() {
        Host host;
        QPluginLoader loader(QCoreApplication::applicationDirPath() + "/reader-plugins/DeepSeekTranslate.dll");
        auto* object = loader.instance(); QVERIFY2(object, qPrintable(loader.errorString()));
        auto* plugin = qobject_cast<pdfreader::PluginV1*>(object); QVERIFY(plugin);
        QString error; QVERIFY2(plugin->initialize(&host, &error), qPrintable(error));
        plugin->documentChanged(host.document()); plugin->selectionChanged(); plugin->shutdown();
        QVERIFY(loader.unload());
    }
    void manualDuringAutomaticPolling() {
        Host host; host.setKey(); Server server;
        server.responses.push_back({200, Completion(), 1200});
        TranslationController controller(&host, server.url()); QString error;
        QVERIFY2(controller.initialize(&error), qPrintable(error));
        host.main.findChild<QCheckBox*>("translationAuto")->setChecked(true);
        host.selection = "hypothesis";
        // Manual lookup may precede the next poll and SDK selection callback.
        controller.translateSelection();
        QTRY_COMPARE(server.bodies.size(), 1);
        QTest::qWait(850);
        QCOMPARE(server.bodies.size(), 1);
    }
    void shutdownDuringRequest() {
        Host host; host.setKey(); host.selection = "pending";
        Server server; server.responses.push_back({200, Completion("英语", "LATE RESULT"), 250});
        auto controller = std::make_unique<TranslationController>(&host, server.url());
        QString error; QVERIFY2(controller->initialize(&error), qPrintable(error));
        controller->translateSelection(); QTRY_COMPARE(server.bodies.size(), 1);
        QPointer<QWidget> popup = host.main.findChild<QWidget*>("translationGlassPopup");
        controller.reset(); QTest::qWait(350);
        QVERIFY(popup.isNull());
    }
    void changedSelectionWithoutSdkNotification() {
        Host host; host.setKey(); host.selection = "old";
        Server server; server.responses.push_back({200, Completion("英语", "STALE"), 200});
        TranslationController controller(&host, server.url()); QString error;
        QVERIFY2(controller.initialize(&error), qPrintable(error));
        controller.translateSelection(); QTRY_COMPARE(server.bodies.size(), 1);
        host.selection = "new";
        QTest::qWait(300);
        QVERIFY(host.main.findChild<QPlainTextEdit*>("translationResult")->toPlainText().isEmpty());
        QVERIFY(host.main.findChild<QLabel*>("translationStatus")->text().contains(QStringLiteral("选区已改变")));
    }
    void completeOfflineDictionary() {
        Dictionary dictionary;
        const auto entry = dictionary.lookup("hypothesis");
        QVERIFY(entry.valid()); QVERIFY(entry.chinese.contains(QStringLiteral("假")));
        QVERIFY(!entry.phonetic.isEmpty()); QVERIFY(!entry.english.isEmpty());
        QVERIFY(entry.text().contains(QStringLiteral("英英释义")));
        QCOMPARE(dictionary.lookup("HYPOTHESIS").chinese, entry.chinese);
        QCOMPARE(dictionary.lookup(QStringLiteral("“hypothesis,”")).chinese, entry.chinese);
        const auto verb = dictionary.lookup("run");
        QVERIFY(verb.text().contains(QStringLiteral("词形变化")));
        QVERIFY(dictionary.lookup("microarchitecture").valid());
        QVERIFY(!dictionary.lookup("pdfreaderxyznotaword").valid());
        QVERIFY(!dictionary.lookup("hello world").valid());
        QVERIFY(IsDictionaryWord("don't")); QVERIFY(IsDictionaryWord(QStringLiteral("don’t")));
        QVERIFY(!IsDictionaryWord(QStringLiteral("你好")));
    }
    void hybridRoutingAndNoKeyDictionary() {
        Host host; host.selection = "hypothesis"; Server server;
        TranslationController controller(&host, server.url()); QString error;
        QVERIFY2(controller.initialize(&error), qPrintable(error));
        controller.translateSelection();
        auto* result = host.main.findChild<QPlainTextEdit*>("translationResult");
        QVERIFY(result->toPlainText().contains(QStringLiteral("英英释义")));
        QVERIFY(host.main.findChild<QLabel*>("translationLanguage")->text().contains("ECDICT"));
        QCOMPARE(server.bodies.size(), 0);
        QVERIFY(!host.main.findChild<QDialog*>("deepseekSettingsDialog"));
        host.selection = "pdfreaderxyznotaword"; controller.translateSelection();
        QVERIFY(host.main.findChild<QLabel*>("translationStatus")->text().contains(QStringLiteral("未收录")));
        QCOMPARE(server.bodies.size(), 0); // Never silently charge for a dictionary miss.
        controller.showSettings();
        auto* settings = host.main.findChild<QDialog*>("deepseekSettingsDialog");
        settings->findChild<QLineEdit*>("deepseekApiKey")->setText("synthetic-test-key");
        QTest::mouseClick(settings->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save), Qt::LeftButton);
        host.selection = QString("The hypothesis is consistent with the observation. ").repeated(20);
        controller.translateSelection(); QTRY_COMPARE(server.bodies.size(), 1);
        const auto payload = QJsonDocument::fromJson(server.bodies[0]).object();
        QCOMPARE(payload["messages"].toArray()[1].toObject()["content"].toString(), host.selection.simplified());
        QCOMPARE(payload["max_tokens"].toInt(), 4096);
        QVERIFY(payload["messages"].toArray()[0].toObject()["content"].toString().contains("contextual explanation"));
        QTRY_VERIFY(host.main.findChild<QLabel*>("translationLanguage")->text().contains("DeepSeek"));
    }
    void placementAvoidsSelection() {
        for (const QRect screen : {QRect(0, 0, 1920, 1080), QRect(-1920, -300, 1920, 1080), QRect(0, 0, 800, 600)}) {
            for (int x = 0; x < screen.width(); x += 90) for (int y = 0; y < screen.height(); y += 70) {
                const QRect anchor(screen.topLeft() + QPoint(x, y), QSize(90, 24));
                const auto popup = PlacePopup(anchor, QSize(374, 352), screen);
                if (popup.isEmpty()) continue;
                QVERIFY(screen.adjusted(8, 8, -8, -8).contains(popup));
                QVERIFY(!popup.intersects(anchor.adjusted(-8, -8, 8, 8)));
            }
        }
        const QRect anchor(200, 100, 80, 20);
        const auto placement = PlacePopup(anchor, QSize(374, 352), QRect(0, 0, 1920, 1080));
        QVERIFY(placement.left() > anchor.right()); QVERIFY(placement.top() > anchor.bottom());
        QVERIFY(PlacePopup(QRect(0, 0, 1920, 1080), QSize(374, 352), QRect(0, 0, 1920, 1080)).isEmpty());
    }
    void followsSelectionAndClose() {
        Host host; host.selection = "hypothesis"; Server server;
        TranslationController controller(&host, server.url()); QString error;
        QVERIFY2(controller.initialize(&error), qPrintable(error)); controller.translateSelection();
        auto* popup = host.main.findChild<GlassPopup*>("translationGlassPopup");
        QVERIFY(popup->isVisible()); QVERIFY(!popup->geometry().intersects(host.scope.bounds));
        const auto original = popup->pos(); host.scope.bounds.translate(50, 40);
        QTRY_VERIFY(popup->pos() != original); QVERIFY(!popup->geometry().intersects(host.scope.bounds));
        host.scope.bounds = {}; QTRY_VERIFY(!popup->isVisible());
        host.scope.bounds = QRect(200, 100, 80, 20); controller.translateSelection();
        QTest::mouseClick(popup->findChild<QPushButton*>("translationClose"), Qt::LeftButton);
        QVERIFY(!popup->isVisible());
    }
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/arial.ttf");
    const auto font = QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/msyh.ttc");
    const auto families = QFontDatabase::applicationFontFamilies(font);
    if (!families.isEmpty()) app.setFont(QFont(families[0], 9));
    if (app.arguments().contains("--probe-deepseek")) {
        QCoreApplication::setLibraryPaths({});
        QString error;
        if (!EnsureTls(QCoreApplication::applicationDirPath() + "/https-probe-runtime", &error)) {
            std::printf("HTTPS PROBE FAIL: %s\n", error.toUtf8().constData()); return 1;
        }
        TranslationClient client(nullptr, QUrl("https://api.deepseek.com/chat/completions"), 15000);
        QEventLoop loop;
        bool rejectedSyntheticKey = false;
        QObject::connect(&client, &TranslationClient::failed, &loop, [&](const QString& message) {
            rejectedSyntheticKey = message.contains("API Key"); error = message; loop.quit();
        });
        QObject::connect(&client, &TranslationClient::succeeded, &loop, &QEventLoop::quit);
        client.start("test", "integration-check-not-a-real-api-key", DefaultModel);
        loop.exec();
        std::printf("HTTPS PROBE %s: %s\n", rejectedSyntheticKey ? "PASS" : "FAIL",
            rejectedSyntheticKey ? "Server rejected the synthetic key; no real credentials or paid translation used." : error.toUtf8().constData());
        return rejectedSyntheticKey ? 0 : 1;
    }
    Tests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "translation_tests.moc"
