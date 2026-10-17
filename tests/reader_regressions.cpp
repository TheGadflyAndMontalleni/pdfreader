#include <QApplication>
#include <QAction>
#include <QContextMenuEvent>
#include <QDir>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QProcess>
#include <QScrollBar>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QToolButton>
#include <QTest>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include "mainwindow.h"
#include "pdfview.h"
#include "tocworker.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("PDF Reader");
    const auto args = app.arguments();
    if (args.size() == 3 && args[1] == "--toc-worker") return app::RunTocWorker(args[2]);
    if (args.size() == 4 && args[1] == "--check-bookmark") {
        const auto saved = app::Session::LoadBookmarks(args[2]);
        return saved.size() == 1 && saved[0].title == args[3] ? 0 : 1;
    }
    if (args.size() < 3) return 2;
    QTemporaryDir temp;
    if (!temp.isValid()) return 2;
    qputenv("PDFREADER_SETTINGS", (temp.path() + "/settings.ini").toUtf8());
    int failures = 0;
    auto check = [&](bool ok, const char* message) {
        std::printf("%s  %s\n", ok ? "PASS" : "FAIL", message);
        std::fflush(stdout);
        if (!ok) ++failures;
    };
    auto settle = [&](int ms) {
        QEventLoop loop;
        QTimer::singleShot(ms, &loop, &QEventLoop::quit);
        loop.exec();
    };
    auto allExpanded = [](QTreeWidget* tree, bool expand) {
        for (QTreeWidgetItemIterator it(tree); *it; ++it)
            if ((expand && (*it)->childCount() > 0 && !(*it)->isExpanded()) ||
                (!expand && (*it)->isExpanded())) return false;
        return true;
    };
    MainWindow window;
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.resize(1280, 860); window.show(); app.processEvents();
    auto* view = window.findChild<PdfView*>();
    auto* tabs = window.findChild<QTabBar*>("documentTabs");
    auto* tree = window.findChild<QTreeWidget*>("tableOfContents");
    auto* navigation = window.findChild<QTabWidget*>("navigationTabs");
    QElapsedTimer timing; timing.start();
    check(window.openDocument(args[1]), "opens the test PDF");
    const double openMs = timing.nsecsElapsed() / 1e6;
    check(window.tabTocNodes(0) == 0, "opening an outline-less PDF does not wait for automatic TOC analysis");
    app.processEvents(); view->grabViewport();
    const double firstFrameMs = timing.nsecsElapsed() / 1e6;
    check(view->hasImage(), "the first page is available while the automatic TOC is pending");
    int heartbeats = 0;
    QTimer heartbeat; heartbeat.setInterval(5);
    QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++heartbeats; });
    heartbeat.start();
    check(window.waitForTocs(), "automatic TOC worker completes");
    heartbeat.stop();
    check(heartbeats > 0, "the UI event loop keeps running while the TOC is generated");
    check(window.tabTocNodes(0) > 0, "worker result becomes a navigable TOC");
    check(navigation->count() == 2 && navigation->tabText(1) == QString::fromUtf8("书签") &&
          window.findChildren<QListWidget*>().isEmpty(), "bookmarks replace thumbnails; no thumbnail widget exists");
    view->setViewMode(PdfView::ViewMode::SinglePage);
    check(!window.findChild<QAction*>("continuousView")->isChecked(), "toolbar reflects a reading mode changed outside the toolbar");
    view->setViewMode(PdfView::ViewMode::Continuous);
    settle(170);
    view->setPage(0);
    const QImage shot = view->grabViewport().convertToFormat(QImage::Format_RGB32);
    const QImage raster = view->cachedPageImage(0);
    const double dpr = view->viewport()->devicePixelRatioF();
    const auto points = view->pageSizePoints(0);
    const int logicalWidth = qRound(points.width() * view->zoom());
    const int logicalHeight = qRound(points.height() * view->zoom());
    check(raster.size() == QSize(qRound(logicalWidth * dpr), qRound(logicalHeight * dpr)) &&
          qFuzzyCompare(raster.devicePixelRatio(), dpr), "page raster uses physical screen resolution and DPR");
    const int pageLeft = 12 + std::max(0, (view->viewport()->width() - 24 - logicalWidth) / 2);
    const int visibleHeight = std::min(raster.height(), shot.height() - qRound(12 * dpr) - 20);
    QPoint ink;
    for (int y = 20; y < visibleHeight && ink.isNull(); ++y)
        for (int x = 20; x < raster.width() - 20; ++x)
            if (qGray(raster.pixel(x, y)) < 100) { ink = QPoint(x, y); break; }
    const int sw = std::min(400, raster.width() - 40);
    const int sh = std::min(200, visibleHeight - 40);
    const int sx = std::clamp(ink.x() - 30, 20, raster.width() - sw - 20);
    const int sy = std::clamp(ink.y() - 30, 20, visibleHeight - sh - 20);
    const QImage displayed = shot.copy(qRound(pageLeft * dpr) + sx, qRound(12 * dpr) + sy, sw, sh);
    const QImage source = raster.copy(sx, sy, sw, sh).convertToFormat(QImage::Format_RGB32);
    check(!ink.isNull() && displayed == source, "steady-state compositing preserves real glyph pixels without an upscale");
    std::printf("INFO  openDocument=%.2f ms first_frame=%.2f ms dpr=%.2f raster=%dx%d\n",
        openMs, firstFrameMs, dpr, raster.width(), raster.height());
    {
        pre::Document reference;
        pre::RenderedPage full, clipped;
        const bool rendered = reference.Open(args[1].toUtf8().constData()) &&
            pre::RenderPage(reference.handle(), 1, 1.5, &full) &&
            pre::RenderPageRegion(reference.handle(), 1, full.width, full.height,
                                  170, 190, 500, 400, &clipped);
        bool same = false;
        if (rendered) {
            const QImage fullImage(full.pixels.data(), full.width, full.height, full.stride, QImage::Format_RGB32);
            const QImage clipImage(clipped.pixels.data(), clipped.width, clipped.height, clipped.stride, QImage::Format_RGB32);
            const QImage expected = fullImage.copy(170, 190, 500, 400);
            int differing = 0;
            double totalDifference = 0;
            for (int y = 0; y < expected.height(); ++y) for (int x = 0; x < expected.width(); ++x) {
                const int delta = std::abs(qGray(expected.pixel(x, y)) - qGray(clipImage.pixel(x, y)));
                if (delta) ++differing;
                totalDifference += delta;
            }
            // PDFium's float transform and glyph hints can vary slightly with
            // a translated clip. Permit AA edge changes, while rejecting even
            // a one-pixel geometric shift or a rescaled low-resolution image.
            const int pixels = expected.width() * expected.height();
            same = differing < pixels / 100 && totalDifference / pixels < 0.5;
        }
        check(same, "clipped rendering matches full-resolution page geometry within antialiasing tolerance");
    }
    view->setFitMode(PdfView::FitMode::None); view->setZoom(8.0);
    view->setPage(std::min(3, view->pageCount() - 1)); view->grabViewport();
    check(!view->cachedPageImage(view->page()).isNull() && view->cachedBytes() <= 128 * 1024 * 1024,
          "800 percent zoom renders a visible tile within the memory budget");
    view->verticalScrollBar()->setValue(view->verticalScrollBar()->value() + 1300);
    view->horizontalScrollBar()->setValue(700); view->grabViewport();
    check(!view->cachedPageImage(view->page()).isNull(), "scrolling at high zoom refreshes the visible tile");
    view->setFitMode(PdfView::FitMode::Width);
    const int bookmarkedPage = std::min(3, view->pageCount() - 1);
    view->setPage(bookmarkedPage);
    check(window.addBookmark(QString::fromUtf8("阅读进度")) && window.bookmarkCount() == 1,
          "adds a named bookmark at the current page");
    window.addBookmark();
    check(window.bookmarkCount() == 1, "adding the same page again does not duplicate bookmarks");
    check(window.renameBookmark(0, QString::fromUtf8("重要章节")), "renames a bookmark");
    view->setPage(0);
    check(window.jumpToBookmark(0) && view->page() == bookmarkedPage, "bookmark activation jumps to its saved page");
    settle(180);
    check(view->page() == bookmarkedPage, "bookmark page remains correct after deferred resizing and painting");
    QProcess persistence;
    persistence.start(QCoreApplication::applicationFilePath(), {"--check-bookmark", args[1], QString::fromUtf8("重要章节")});
    check(persistence.waitForFinished(10000) && persistence.exitCode() == 0,
          "a separate process reads the saved bookmark and Unicode title");
    window.showSidebarPage(0);
    window.findChild<QAction*>("collapseToc")->trigger();
    check(allExpanded(tree, false), "one click collapses the entire TOC");
    window.findChild<QAction*>("expandToc")->trigger();
    check(allExpanded(tree, true), "one click expands the entire TOC");
    tree->collapseAll();
    auto* splitter = window.findChild<QSplitter*>();
    splitter->setSizes({350, 900});
    const int sidebarWidth = splitter->sizes()[0];
    check(window.openDocument(args[2]) && window.waitForTocs(), "opens a second document with its own TOC");
    check(window.bookmarkCount() == 0, "bookmarks belong to their document");
    window.activateDocTab(0);
    check(window.bookmarkCount() == 1, "switching back restores the document's bookmarks");
    check(allExpanded(tree, false) && splitter->sizes()[0] == sidebarWidth,
          "switching documents preserves TOC expansion and sidebar width");
    tabs->moveTab(0, 1); window.activateDocTab(0);
    check(window.tabTitle(0) == tabs->tabText(0) && window.windowTitle().contains(tabs->tabText(0)),
          "dragging tabs preserves the mapping between labels and documents");
    window.setActivePage(2); window.closeDocTab(1);
    check(window.docTabCount() == 1 && window.tabPage(0) == 2 && window.bookmarkCount() == 0,
          "closing an inactive reordered tab keeps the active document and page intact");
    window.closeDocTab(0);
    check(window.openDocument(args[1]) && window.tabTocNodes(0) > 0,
          "reopening uses the persisted TOC cache without a worker scan");
    check(window.bookmarkCount() == 1, "bookmarks survive closing and reopening a document");
    window.showSidebarPage(1);
    auto* bookmarkTree = window.findChild<QTreeWidget*>("personalBookmarks");
    bookmarkTree->setCurrentItem(bookmarkTree->topLevelItem(0));
    QMenu* bookmarkMenu = nullptr;
    for (auto* action : window.menuBar()->actions())
        if (action->text().contains(QString::fromUtf8("书签"))) bookmarkMenu = action->menu();
    auto* deleteBookmark = window.findChild<QAction*>("removeBookmark");
    check(bookmarkMenu && deleteBookmark && bookmarkMenu->actions().contains(deleteBookmark),
          "the top Bookmarks menu exposes deletion of the selected bookmark");
    auto* narrowSplitter = window.findChild<QSplitter*>();
    narrowSplitter->setSizes({220, 1060}); settle(180);
    QToolButton* deleteButton = nullptr;
    for (auto* button : navigation->widget(1)->findChildren<QToolButton*>())
        if (button->defaultAction() && button->defaultAction()->text().contains(QString::fromUtf8("删除")))
            deleteButton = button;
    check(deleteButton && deleteButton->isVisible() &&
          navigation->widget(1)->rect().contains(deleteButton->mapTo(navigation->widget(1), QPoint(0, 0))) &&
          navigation->widget(1)->rect().contains(deleteButton->mapTo(navigation->widget(1), deleteButton->rect().bottomRight())),
          "the delete button remains visible at the minimum 220-pixel sidebar width");
    if (bookmarkMenu && deleteBookmark) {
        bookmarkMenu->popup(window.mapToGlobal(QPoint(200, 60)));
        app.processEvents();
        QTest::mouseClick(bookmarkMenu, Qt::LeftButton, Qt::NoModifier,
                         bookmarkMenu->actionGeometry(deleteBookmark).center());
    }
    check(window.bookmarkCount() == 0 && app::Session::LoadBookmarks(args[1]).isEmpty(),
          "clicking Delete in the Bookmarks menu removes the bookmark and saves the change");
    view->setPage(0); window.addBookmark(QString::fromUtf8("保留的书签"));
    view->setPage(1); window.addBookmark(QString::fromUtf8("待删除的书签"));
    bookmarkTree->setCurrentItem(bookmarkTree->topLevelItem(1));
    if (deleteButton) QTest::mouseClick(deleteButton, Qt::LeftButton);
    check(window.bookmarkCount() == 1 && bookmarkTree->currentItem() == bookmarkTree->topLevelItem(0) &&
          deleteBookmark && deleteBookmark->isEnabled() && app::Session::LoadBookmarks(args[1]).size() == 1,
          "the visible Delete button removes only the selection and selects the remaining bookmark");
    QApplication::setActiveWindow(&window);
    bookmarkTree->setFocus(); app.processEvents();
    QTest::keyClick(bookmarkTree, Qt::Key_Delete);
    check(window.bookmarkCount() == 0 && app::Session::LoadBookmarks(args[1]).isEmpty(),
          "the Delete key removes a bookmark when the bookmark list has focus");
    check(deleteBookmark && !deleteBookmark->isEnabled(), "Delete is disabled after the last bookmark is removed");
    view->setPage(0); window.addBookmark(QString::fromUtf8("保留的书签"));
    view->setPage(1); window.addBookmark(QString::fromUtf8("右键删除"));
    bookmarkTree->setCurrentItem(bookmarkTree->topLevelItem(0));
    app.processEvents();
    const QPoint contextPosition = bookmarkTree->visualItemRect(bookmarkTree->topLevelItem(1)).center();
    bool contextSelectedCorrectly = false;
    QTimer::singleShot(0, &window, [&] {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        contextSelectedCorrectly = menu && deleteBookmark && menu->actions().contains(deleteBookmark) &&
            bookmarkTree->currentItem() == bookmarkTree->topLevelItem(1);
        if (contextSelectedCorrectly)
            QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(deleteBookmark).center());
        else if (menu) menu->close();
    });
    QContextMenuEvent contextEvent(QContextMenuEvent::Mouse, contextPosition,
                                   bookmarkTree->viewport()->mapToGlobal(contextPosition));
    QApplication::sendEvent(bookmarkTree->viewport(), &contextEvent);
    app.processEvents();
    check(contextSelectedCorrectly && window.bookmarkCount() == 1 &&
          app::Session::LoadBookmarks(args[1]).size() == 1 &&
          app::Session::LoadBookmarks(args[1])[0].title == QString::fromUtf8("保留的书签"),
          "the right-click menu deletes the clicked bookmark rather than the previously selected row");
    view->setFocus(); app.processEvents();
    QTest::keyClick(view, Qt::Key_Delete);
    check(window.bookmarkCount() == 1, "Delete outside the bookmark list leaves bookmarks intact");
    window.removeBookmark(0);
    window.closeDocTab(0);

    // Opening two files in one event-loop turn must not restore A's state to B.
    const QString copiedA = temp.path() + QString::fromUtf8("/中文文档 A.pdf");
    const QString copiedB = temp.path() + QString::fromUtf8("/中文文档 B.pdf");
    check(QFile::copy(args[1], copiedA) && QFile::copy(args[2], copiedB), "creates isolated Unicode-path fixtures");
    app::DocState saved;
    saved.page = 3; saved.zoom = 1.25; saved.fitMode = 0;
    saved.viewMode = 1; saved.invert = true; saved.valid = true;
    app::Session::SaveDocState(copiedA, saved);
    check(window.openDocument(copiedA) && window.openDocument(copiedB), "opens multiple Unicode files before queued events run");
    app.processEvents(); window.activateDocTab(0);
    check(window.tabPage(0) == saved.page && std::abs(view->zoom() - 1.25) < 0.001 &&
          window.activeViewMode() == 1 && window.activeInvert(), "each tab restores its own page, zoom, view mode and inversion");
    window.activateDocTab(1);
    check(window.tabPage(1) == 0 && window.activeViewMode() == 0 && !window.activeInvert(),
          "a new tab does not inherit another file's restored state");
    check(window.waitForTocs(), "workers handle Unicode paths and queued documents");
    window.closeDocTab(1); window.closeDocTab(0);
    const QString cachePath = temp.path() + "/toc-cache/" + app::TocCacheKey(copiedA) + ".json";
    QFile corrupt(cachePath);
    check(corrupt.open(QIODevice::WriteOnly), "opens the test TOC cache for a corruption test");
    corrupt.write("not valid json"); corrupt.close();
    check(window.openDocument(copiedA) && window.tabTocNodes(0) == 0 && window.waitForTocs() && window.tabTocNodes(0) > 0,
          "a corrupt TOC cache is regenerated safely");
    window.closeDocTab(0);
    const QString previousKey = app::TocCacheKey(copiedA);
    QFile modified(copiedA);
    const QDateTime changed = QFileInfo(copiedA).lastModified().addSecs(5);
    const bool touched = modified.open(QIODevice::ReadWrite) && modified.setFileTime(changed, QFileDevice::FileModificationTime);
    modified.close();
    check(touched && app::TocCacheKey(copiedA) != previousKey && window.openDocument(copiedA) &&
          window.tabTocNodes(0) == 0 && window.waitForTocs() && window.tabTocNodes(0) > 0,
          "modifying a PDF invalidates the cached TOC and starts fresh analysis");
    window.closeDocTab(0);
    const QString closingCopy = temp.path() + "/close-while-generating.pdf";
    QFile::copy(args[1], closingCopy); window.openDocument(closingCopy); app.processEvents();
    QPointer<QProcess> worker;
    for (auto* process : window.findChildren<QProcess*>())
        if (process->state() != QProcess::NotRunning) worker = process;
    check(worker != nullptr, "the cancellation fixture has a running TOC worker");
    window.closeDocTab(0); settle(150);
    check(window.docTabCount() == 0 && (!worker || worker->state() == QProcess::NotRunning),
          "closing the last tab cancels background work before document destruction");
    const QString screenshots = QString::fromLocal8Bit(qgetenv("PDFREADER_TEST_ARTIFACTS"));
    if (!screenshots.isEmpty()) {
        QDir().mkpath(screenshots);
        window.grab().save(screenshots + "/reader-welcome.png");
        window.openDocument(args[1]); window.waitForTocs(); window.setActivePage(3);
        window.addBookmark(QString::fromUtf8("重要章节")); settle(180);
        window.grab().save(screenshots + "/reader-bookmarks.png");
        window.showSidebarPage(0); window.grab().save(screenshots + "/reader-toc.png");
        view->grabViewport().save(screenshots + "/reader-page.png");
    }
    std::printf("REGRESSIONS %s (%d failures)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
