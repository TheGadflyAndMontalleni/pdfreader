#include <QtTest>
#include <QTemporaryDir>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QTreeWidget>
#include <QAction>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScreen>
#include <QStackedWidget>
#include <QVBoxLayout>
#include "pluginmanager.h"
#include "tocworker.h"
#include "epubview.h"
#include "epubserver.h"
#include "mainwindow.h"

class EpubRegressions : public QObject {
    Q_OBJECT
    QTemporaryDir settings_;
    QString fixtures_;
    void prepare(EpubView& view, int width = 900, int height = 720) {
        // A hidden top-level HWND is still constrained by Windows' maximum
        // tracking size. Keep fixtures inside the logical work area at 4x DPI.
        const auto available = QGuiApplication::primaryScreen()->availableGeometry();
        view.resize(std::min(width, available.width() - 40), std::min(height, available.height() - 50));
        view.setAttribute(Qt::WA_DontShowOnScreen); view.show();
    }
    QJsonObject query(EpubView& view, const QString& expression) {
        QSignalSpy result(&view, &EpubView::testResult);
        view.evaluate("Promise.resolve((async()=>{" + expression + "})()).then(value=>readerEvent({type:'test',value})).catch(e=>readerEvent({type:'test',error:String(e)}))");
        if (result.isEmpty()) result.wait(15000);
        return result.isEmpty() ? QJsonObject{{"error", "no browser response"}} : result.takeFirst()[0].toJsonObject();
    }
private slots:
    void initTestCase() {
        QVERIFY(settings_.isValid());
        qputenv("PDFREADER_SETTINGS", (settings_.path() + "/settings.ini").toUtf8());
        qputenv("WEBVIEW2_USER_DATA_FOLDER", (settings_.path() + "/webview2").toUtf8());
        fixtures_ = QString::fromLocal8Bit(qgetenv("EPUB_TEST_FIXTURES"));
        QVERIFY(QFileInfo::exists(fixtures_ + "/阅读试用.epub"));
    }
    void serverRangesAndOrigin() {
        EpubServer server;
        QString error;
        QVERIFY2(server.openBook(fixtures_ + "/阅读试用.epub", &error), qPrintable(error));
        QNetworkAccessManager network;
        auto request = QNetworkRequest(server.url().resolved(QUrl("book.epub")));
        request.setRawHeader("Range", "bytes=0-3");
        auto* reply = network.get(request);
        QSignalSpy finished(reply, &QNetworkReply::finished);
        QVERIFY(finished.wait());
        QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 206);
        QCOMPARE(reply->readAll(), QByteArray("PK\003\004", 4)); reply->deleteLater();
        request.setRawHeader("Origin", "https://example.com");
        reply = network.get(request); QSignalSpy rejected(reply, &QNetworkReply::finished); QVERIFY(rejected.wait());
        QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 403); reply->deleteLater();
        request.setRawHeader("Origin", {}); request.setRawHeader("Range", "bytes=999999999-9999999999");
        reply = network.get(request); QSignalSpy invalid(reply, &QNetworkReply::finished); QVERIFY(invalid.wait());
        QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 416); reply->deleteLater();
    }
    void realRenderingAndSelection() {
        EpubView view; prepare(view);
        QString error; QElapsedTimer timer; timer.start();
        QVERIFY2(view.open(fixtures_ + "/阅读试用.epub", &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(view.ready() || !view.error().isEmpty(), 20000);
        QVERIFY2(view.ready(), qPrintable(view.error()));
        QTRY_VERIFY_WITH_TIMEOUT(!view.locator().isEmpty(), 10000);
        qInfo() << "EPUB first layout ms" << timer.elapsed() << "bytes served" << view.bytesServed();
        QCOMPARE(view.sectionCount(), 2); QVERIFY(view.toc().size() >= 2);
        QVERIFY(view.bytesServed() < QFileInfo(fixtures_ + "/阅读试用.epub").size() / 2);
        auto result = query(view, "const doc=reader.view.renderer.getContents()[0].doc;return {text:doc.body.textContent, scripts:doc.querySelectorAll('script').length,pwned:!!doc.defaultView.__epubPwned,image:doc.querySelector('img').complete};");
        QVERIFY2(!result.contains("error"), qPrintable(QJsonDocument(result).toJson()));
        const auto values = result["value"].toObject();
        QVERIFY(values["text"].toString().contains("serendipity"));
        QCOMPARE(values["scripts"].toInt(), 0); QVERIFY(!values["pwned"].toBool());
        result = query(view, "return reader.view.renderer.getContents()[0].doc.defaultView.innerWidth;");
        QVERIFY2(result["value"].toDouble() > view.width() * .8, "The chapter must fill the reading area rather than half of it");
        result = query(view, "return {width:innerWidth,dpr:devicePixelRatio};");
        QVERIFY2(std::abs(result["value"].toObject()["width"].toDouble() - view.width()) < 2,
            qPrintable(QString("Browser %1; Qt width %2 DPR %3").arg(QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact))).arg(view.width()).arg(view.devicePixelRatioF())));
        result = query(view, "const d=reader.view.renderer.getContents()[0].doc,n=d.querySelector('#word').firstChild,r=d.createRange();r.setStart(n,9);r.setEnd(n,20);d.getSelection().removeAllRanges();d.getSelection().addRange(r);return true;");
        QTRY_COMPARE_WITH_TIMEOUT(view.selectedText(), QString("serendipity"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!view.selectionScreenRect().isEmpty(), 5000);
        const QString cfi = view.locator();
        view.setFontScale(1.5); QTest::qWait(500);
        QVERIFY2(view.ready(), qPrintable(view.error()));
        QVERIFY(!view.locator().isEmpty()); QCOMPARE(view.section(), 0);
        view.goToSection(1); QTRY_COMPARE_WITH_TIMEOUT(view.section(), 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(view.sectionText(1).contains("中文测试正文"), 5000);
        view.setDark(true); QTest::qWait(300);
        QVERIFY2(view.ready(), qPrintable(view.error()));
        result = query(view, "const d=reader.view.renderer.getContents()[0].doc;return getComputedStyle(d.body).backgroundColor;");
        QCOMPARE(result["value"].toString(), QString("rgb(32, 38, 43)"));
        view.goTo(cfi); QTRY_COMPARE_WITH_TIMEOUT(view.section(), 0, 5000);
        QSignalSpy found(&view, &EpubView::findChanged);
        view.find("中文测试正文", false);
        if (found.isEmpty()) QVERIFY(found.wait(5000));
        QCOMPARE(found.takeLast()[1].toInt(), 1);
        QTRY_COMPARE(view.section(), 1);
        view.find({}, false); view.goTo(cfi); QTRY_COMPARE(view.section(), 0);
        view.setDark(false); view.setFontScale(1); QTest::qWait(400);
        qInfo().noquote() << QJsonDocument(query(view, "const r=reader.view.renderer;return {viewport:[innerWidth,innerHeight,devicePixelRatio],view:[reader.view.clientWidth,reader.view.clientHeight],renderer:[r.clientWidth,r.clientHeight],iframe:r.getContents().map(({doc})=>({size:[doc.defaultView.innerWidth,doc.defaultView.innerHeight],body:doc.body.getBoundingClientRect().toJSON()}))};")).toJson(QJsonDocument::Compact);
        QSignalSpy capture(&view, &EpubView::captured);
        view.capture(fixtures_ + "/preview.png"); QVERIFY(capture.wait(5000)); QVERIFY(capture.takeFirst()[0].toBool());
        view.saveState();
        const auto state = app::Session::LoadDocState(fixtures_ + "/阅读试用.epub");
        QCOMPARE(state.locator, view.locator());
    }
    void formats_data() {
        QTest::addColumn<QString>("name");
        QTest::newRow("EPUB 2 NCX") << QString("epub2.epub");
        QTest::newRow("EPUB 3 fixed layout") << QString("fixed.epub");
        QTest::newRow("UTF-16 chapter") << QString("utf16.epub");
    }
    void formats() {
        QFETCH(QString, name);
        EpubView view; prepare(view);
        QString error; QVERIFY2(view.open(fixtures_ + "/" + name, &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(view.ready() || !view.error().isEmpty(), 20000);
        QVERIFY2(view.ready(), qPrintable(view.error()));
        QTRY_VERIFY_WITH_TIMEOUT(!view.locator().isEmpty(), 10000);
        QCOMPARE(view.sectionCount(), 2); QVERIFY(!view.toc().isEmpty());
        if (name == "utf16.epub") {
            view.goToSection(1); QTRY_COMPARE(view.section(), 1);
            QTRY_VERIFY(view.sectionText(1).contains("中文测试正文"));
        }
    }
    void corruptArchive() {
        EpubView view; prepare(view, 800, 700);
        QString error; QVERIFY(view.open(fixtures_ + "/broken.epub", &error));
        QTRY_VERIFY_WITH_TIMEOUT(!view.error().isEmpty(), 15000);
        QVERIFY(!view.ready());
    }
    void embeddedViewportFollowsLayout() {
        QWidget host;
        host.setAttribute(Qt::WA_ShowWithoutActivating);
        host.setWindowOpacity(0);
        auto* layout = new QVBoxLayout(&host);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* stack = new QStackedWidget;
        layout->addWidget(stack);
        stack->addWidget(new QWidget);
        auto* book = new EpubView;
        stack->addWidget(book);
        QString error;
        // Open while this tab and the top-level window still have their
        // initial geometry, exactly as session restoration can do.
        QVERIFY2(book->open(fixtures_ + "/阅读试用.epub", &error), qPrintable(error));
        const QSize area = QGuiApplication::primaryScreen()->availableGeometry().size();
        host.resize(std::min(900, area.width() - 60), std::min(700, area.height() - 70));
        stack->setCurrentWidget(book); host.show();
        QTRY_VERIFY_WITH_TIMEOUT(book->ready(), 15000);
        auto checkViewport = [&] {
            const auto result = query(*book, "return {width:innerWidth,height:innerHeight,dpr:devicePixelRatio};")["value"].toObject();
            return std::abs(result["width"].toDouble() - book->width()) < 2 &&
                   std::abs(result["height"].toDouble() - book->height()) < 2 &&
                   std::abs(result["dpr"].toDouble() - book->devicePixelRatioF()) < .01;
        };
        QTRY_VERIFY_WITH_TIMEOUT(checkViewport(), 5000);
        host.resize(host.size() * .72);
        QTRY_VERIFY_WITH_TIMEOUT(checkViewport(), 5000);
        stack->setCurrentIndex(0); host.resize(host.size() * 1.15);
        stack->setCurrentWidget(book);
        QTRY_VERIFY_WITH_TIMEOUT(checkViewport(), 5000);
        host.hide(); host.show();
        QTRY_VERIFY_WITH_TIMEOUT(checkViewport(), 5000);
        QSignalSpy capture(book, &EpubView::captured);
        book->capture(fixtures_ + "/embedded-resize.png");
        if (capture.isEmpty()) QVERIFY(capture.wait(5000));
        QVERIFY(capture.takeFirst()[0].toBool());
    }
    void fixedPageUsesBookViewport() {
        EpubView book; prepare(book);
        QString error;
        QVERIFY2(book.open(fixtures_ + "/fixed-book-viewport.epub", &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(book.ready(), 15000);
        const auto result = query(book, "const d=reader.view.renderer.getContents()[0].doc,f=d.defaultView.frameElement,b=f.getBoundingClientRect();return {canvas:[f.clientWidth,f.clientHeight],frame:[b.x,b.y,b.width,b.height],viewport:[innerWidth,innerHeight]};");
        qInfo().noquote() << QJsonDocument(result).toJson(QJsonDocument::Compact);
        const auto value = result["value"].toObject();
        const auto canvas = value["canvas"].toArray(), frame = value["frame"].toArray(), viewport = value["viewport"].toArray();
        QCOMPARE(canvas[0].toInt(), 800); QCOMPARE(canvas[1].toInt(), 1100);
        QVERIFY(frame[2].toDouble() > 100); QVERIFY(frame[3].toDouble() > 100);
        QVERIFY(std::abs(frame[0].toDouble() + frame[2].toDouble() / 2 - viewport[0].toDouble() / 2) < 3);
        QVERIFY(book.fixedLayout());
        book.setFitMode(1);
        QTRY_VERIFY_WITH_TIMEOUT(book.zoom() > .1 && book.zoom() > frame[2].toDouble() / 800 + .05, 5000);
        book.setFontScale(.4);
        QTRY_VERIFY_WITH_TIMEOUT(std::abs(book.zoom() - .4) < .001, 5000);
        book.setFontScale(2);
        QTRY_VERIFY_WITH_TIMEOUT(std::abs(book.zoom() - 2) < .001, 5000);
        const auto enlarged = query(book, "const f=reader.view.renderer.getContents()[0].doc.defaultView.frameElement;return f.getBoundingClientRect().toJSON();")["value"].toObject();
        QVERIFY(enlarged["x"].toDouble() >= -.5);
        book.setFontScale(.4); QTRY_VERIFY(std::abs(book.zoom() - .4) < .001);
        book.saveState();
        QSignalSpy capture(&book, &EpubView::captured);
        book.setFitMode(2);
        QTRY_VERIFY(book.zoom() > .1 && std::abs(book.zoom() - .4) > .01);
        book.capture(fixtures_ + "/fixed-fit-page.png");
        if (capture.isEmpty()) QVERIFY(capture.wait(5000));
        QVERIFY(capture.takeFirst()[0].toBool());
    }
    void fixedPageZoomControlsAndRestore() {
        const QString path = fixtures_ + "/fixed-book-viewport.epub";
        {
            EpubView book; prepare(book);
            QString error; QVERIFY(book.open(path, &error));
            QTRY_VERIFY_WITH_TIMEOUT(book.ready(), 15000);
            book.setFitMode(1); QCOMPARE(book.fitMode(), 1);
            book.setFitMode(2); QCOMPARE(book.fitMode(), 2);
            book.setFontScale(.3);
            QTRY_VERIFY(std::abs(book.zoom() - .3) < .001);
            book.saveState();
        }
        EpubView restored; prepare(restored);
        QString error; QVERIFY(restored.open(path, &error));
        QTRY_VERIFY_WITH_TIMEOUT(restored.ready(), 15000);
        QCOMPARE(restored.fitMode(), 0);
        QTRY_VERIFY(std::abs(restored.zoom() - .3) < .001);
    }
    void restoresReflowPosition() {
        const QString path = fixtures_ + "/阅读试用.epub";
        QString saved;
        {
            EpubView view; prepare(view);
            QString error; QVERIFY(view.open(path, &error)); QTRY_VERIFY_WITH_TIMEOUT(view.ready(), 15000);
            const auto result = query(view, "const d=reader.view.renderer.getContents()[0].doc,r=d.createRange();r.selectNodeContents(d.querySelector('#p50'));return reader.view.getCFI(0,r);");
            const QString target = result["value"].toString(); QVERIFY(!target.isEmpty());
            view.goTo(target);
            QTest::qWait(400);
            view.setFontScale(1.25); QTest::qWait(400);
            view.saveState(); saved = view.locator();
        }
        EpubView restored; prepare(restored, 760, 650);
        QString error; QVERIFY(restored.open(path, &error)); QTRY_VERIFY_WITH_TIMEOUT(restored.ready(), 15000);
        QCOMPARE(restored.fontScale(), 1.25);
        const auto result = query(restored, "const range=reader.view.lastLocation.range;return range.startContainer.parentElement.closest('p')?.id;");
        QVERIFY2(result["value"].toString() == "p50" || result["value"].toString() == "p49", qPrintable(QJsonDocument(result).toJson()));
        QVERIFY(!saved.isEmpty());
        // The following test starts at the beginning, independent of this one.
        restored.goToSection(0); QTest::qWait(300); restored.setFontScale(1);
    }
    void tabsBookmarksAndTranslationHost() {
        MainWindow window(nullptr, false); window.setAttribute(Qt::WA_DontShowOnScreen); window.show();
        QVERIFY(window.openDocument(fixtures_ + "/阅读试用.epub"));
        auto* book = window.findChild<EpubView*>(); QVERIFY(book);
        QTRY_VERIFY_WITH_TIMEOUT(book->ready(), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(!book->locator().isEmpty(), 5000);
        QCOMPARE(window.pluginDocument().pageCount, 2);
        QVERIFY(window.addBookmark("EPUB bookmark"));
        QCOMPARE(window.bookmarkCount(), 1);
        QVERIFY(window.renameBookmark(0, "Renamed"));
        book->goToSection(1); QTRY_COMPARE(book->section(), 1);
        QVERIFY(window.jumpToBookmark(0)); QTRY_COMPARE(book->section(), 0);
        QVERIFY(window.removeBookmark(0)); QCOMPARE(window.bookmarkCount(), 0);
        query(*book, "const d=reader.view.renderer.getContents()[0].doc,n=d.querySelector('#word').firstChild,r=d.createRange();r.setStart(n,9);r.setEnd(n,20);d.getSelection().removeAllRanges();d.getSelection().addRange(r);return true;");
        QTRY_COMPARE(window.pluginSelectedText(), QString("serendipity"));
        QVERIFY(!window.pluginSelectionScreenRect().isEmpty());
        QCOMPARE(window.pluginSelectedText(), book->selectedText());
        const QString plugin = QString::fromLocal8Bit(qgetenv("EPUB_TEST_PLUGIN"));
        if (!plugin.isEmpty()) {
            QString id, error;
            QVERIFY2(window.pluginManager()->install(plugin, &id, &error), qPrintable(error));
            QVERIFY2(window.pluginManager()->setEnabled(id, true, &error), qPrintable(error));
            auto* translate = window.findChild<QAction*>("plugin:org.pdfreader.deepseek-translate/translate"); QVERIFY(translate);
            query(*book, "reader.view.renderer.getContents()[0].doc.dispatchEvent(new KeyboardEvent('keydown',{key:'t',altKey:true,bubbles:true,cancelable:true}));return true;");
            auto* popup = window.findChild<QWidget*>("translationGlassPopup");
            QTRY_VERIFY_WITH_TIMEOUT(popup && popup->isVisible(), 5000);
            auto* result = window.findChild<QPlainTextEdit*>("translationResult");
            QVERIFY(result && result->toPlainText().size() > 10);
            QVERIFY(!popup->frameGeometry().intersects(window.pluginSelectionScreenRect()));
            QSignalSpy capture(book, &EpubView::captured);
            book->capture(fixtures_ + "/native.png");
            if (capture.isEmpty()) QVERIFY(capture.wait(5000));
            QVERIFY(capture.takeFirst()[0].toBool());
            const qreal dpr = window.devicePixelRatioF();
            QImage image(window.size() * dpr, QImage::Format_ARGB32_Premultiplied); image.setDevicePixelRatio(dpr); image.fill(Qt::white);
            QImage native(fixtures_ + "/native.png");
            { QPainter painter(&image); painter.drawPixmap(0, 0, window.grab());
                painter.drawImage(QRect(book->mapTo(&window, QPoint()), book->size()), native);
                painter.drawPixmap(window.mapFromGlobal(popup->pos()), popup->grab()); }
            QVERIFY(image.save(fixtures_ + "/reader-preview.png"));
            QVERIFY(window.pluginManager()->setEnabled(id, false, &error));
        }
        const QString pdf = QString::fromLocal8Bit(qgetenv("EPUB_TEST_PDF"));
        QVERIFY2(QFileInfo::exists(pdf), "Set EPUB_TEST_PDF to a PDF fixture");
        QVERIFY(window.openDocument(pdf)); QCOMPARE(window.docTabCount(), 2);
        QVERIFY(window.activePageCount() > 0);
        QVERIFY(window.activateDocTab(0)); QCOMPARE(window.pluginDocument().pageCount, 2);
        QVERIFY(window.setActivePage(1)); QTRY_COMPARE(window.tabPage(0), 1);
        QVERIFY(window.closeDocTab(0)); QCOMPARE(window.docTabCount(), 1);
        QVERIFY(window.activePageCount() > 0);
        QVERIFY(window.waitForTocs());
        QVERIFY(window.tabTocNodes(0) > 0);
        QVERIFY(window.openDocument(fixtures_ + "/fixed-book-viewport.epub"));
        // Exercise toolbar routing in this same window: PDFium's V8 runtime
        // cannot be torn down and initialized a second time in one test process.
        EpubView* fixed = nullptr;
        for (auto* candidate : window.findChildren<EpubView*>())
            if (candidate->isVisible()) fixed = candidate;
        QVERIFY(fixed);
        QTRY_VERIFY_WITH_TIMEOUT(fixed->ready(), 15000);
        QVERIFY(window.setActiveFitMode(1)); QCOMPARE(window.activeFitMode(), 1);
        QVERIFY(window.setActiveFitMode(2)); QCOMPARE(window.activeFitMode(), 2);
        window.close();
    }
};
int main(int argc, char** argv) {
    QApplication application(argc, argv);
    if (application.arguments().size() == 3 && application.arguments()[1] == "--toc-worker")
        return app::RunTocWorker(application.arguments()[2]);
    EpubRegressions tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "epub_regressions.moc"
