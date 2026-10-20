// PDF Reader — a PDF viewer whose sidebar table of contents is reconstructed
// automatically, including for documents that ship no /Outlines.
#include <QApplication>
#include <QAction>
#include <QClipboard>
#include <QCommandLineParser>
#include <QIcon>
#include <QTimer>

#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#endif

#include "mainwindow.h"
#include "pluginmanager.h"

// For --render-png: the raw PDFium output, without any Qt compositing.
#include <QImage>

#include "doc.h"
#include "render.h"
#include "tocworker.h"

namespace {

// A GUI-subsystem binary has no console of its own, so --selftest output would
// vanish when the program is started without a redirection. Borrow the parent
// console in that case; when output *is* redirected the handle is already
// valid and must be left alone.
void PrepareTestOutput() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    if (GetStdHandle(STD_OUTPUT_HANDLE) == nullptr &&
        AttachConsole(ATTACH_PARENT_PROCESS)) {
        (void)freopen("CONOUT$", "w", stdout);
    }
#endif
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PDF Reader"));
    QApplication::setApplicationVersion(QStringLiteral("0.4.0"));

    // Same artwork as the .ico in the PE resource table. Windows draws the
    // taskbar button from the executable's icon, but the title bar, the
    // window list and dialog headers come from Qt's own icon.
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icon.png")));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("PDF reader with automatic table-of-contents reconstruction."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"),
                                 QStringLiteral("PDF file to open."));
    QCommandLineOption selfTest(
        QStringLiteral("selftest"),
        QStringLiteral("Open FILE, verify the reconstructed TOC and that clicking an "
                       "entry moves the page view, print the result and exit."),
        QStringLiteral("file"));
    parser.addOption(selfTest);

    QCommandLineOption selectLineOpt(
        QStringLiteral("select-line"),
        QStringLiteral("Select PAGE:LINE (page is 1-based) after opening, so the "
                       "selection highlight can be inspected. Verification aid."),
        QStringLiteral("page:line"));
    parser.addOption(selectLineOpt);

    QCommandLineOption selectAllOpt(
        QStringLiteral("select-all"),
        QStringLiteral("Select the whole document after opening. Verification aid."));
    parser.addOption(selectAllOpt);

    QCommandLineOption findOpt(
        QStringLiteral("find"),
        QStringLiteral("Open the find bar pre-filled with TEXT. Verification aid."),
        QStringLiteral("text"));
    parser.addOption(findOpt);

    QCommandLineOption sessionWriteOpt(
        QStringLiteral("session-write"),
        QStringLiteral("Open FILE, store the reading position from --page/--zoom and "
                       "exit. Verification aid for session persistence."),
        QStringLiteral("file"));
    parser.addOption(sessionWriteOpt);

    QCommandLineOption sessionReadOpt(
        QStringLiteral("session-read"),
        QStringLiteral("Open FILE and assert it restored to --page/--zoom, then exit."),
        QStringLiteral("file"));
    parser.addOption(sessionReadOpt);

    QCommandLineOption pageOpt(QStringLiteral("page"),
                               QStringLiteral("Page for the session modes."),
                               QStringLiteral("n"));
    parser.addOption(pageOpt);
    QCommandLineOption zoomOpt(QStringLiteral("zoom"),
                               QStringLiteral("Zoom for the session modes."),
                               QStringLiteral("z"));
    parser.addOption(zoomOpt);

    QCommandLineOption tabsTestOpt(
        QStringLiteral("selftest-tabs"),
        QStringLiteral("Open FILES (';'-separated) as tabs and verify per-tab "
                       "isolation, then exit."),
        QStringLiteral("files"));
    parser.addOption(tabsTestOpt);

    QCommandLineOption printTabsOpt(
        QStringLiteral("print-tabs"),
        QStringLiteral("Print the open tab set and exit. With no file argument this "
                       "verifies that the previous session was restored."));
    parser.addOption(printTabsOpt);

    QCommandLineOption viewOpt(
        QStringLiteral("view"),
        QStringLiteral("Set the view mode after opening: continuous | single."),
        QStringLiteral("mode"));
    parser.addOption(viewOpt);
    QCommandLineOption fitOpt(
        QStringLiteral("fit"),
        QStringLiteral("Set the fit mode after opening: width | page | none."),
        QStringLiteral("mode"));
    parser.addOption(fitOpt);

    QCommandLineOption printTestOpt(
        QStringLiteral("print-test"),
        QStringLiteral("Print FILE to --print-out (a PDF) and verify the result by "
                       "reopening it, then exit."),
        QStringLiteral("file"));
    parser.addOption(printTestOpt);
    QCommandLineOption printOutOpt(
        QStringLiteral("print-out"),
        QStringLiteral("Output PDF path for --print-test."),
        QStringLiteral("path"));
    parser.addOption(printOutOpt);
    QCommandLineOption pageToOpt(QStringLiteral("page-to"),
                                 QStringLiteral("Last page (1-based) for --print-test."),
                                 QStringLiteral("n"));
    parser.addOption(pageToOpt);

    QCommandLineOption sideOpt(
        QStringLiteral("side"),
        QStringLiteral("Select the sidebar page after opening: toc | bookmarks."),
        QStringLiteral("which"));
    parser.addOption(sideOpt);
    QCommandLineOption invertOpt(QStringLiteral("invert"),
                                 QStringLiteral("Turn inverted colours on after opening."));
    parser.addOption(invertOpt);

    QCommandLineOption markupOpt(
        QStringLiteral("markup"),
        QStringLiteral("Mark up the selection made by --select-line: "
                       "highlight | underline | strikeout."),
        QStringLiteral("kind"));
    parser.addOption(markupOpt);

    QCommandLineOption benchOpt(
        QStringLiteral("bench"),
        QStringLiteral("Open FILE, time rendering/scrolling/zooming, print a report "
                       "and exit."),
        QStringLiteral("file"));
    parser.addOption(benchOpt);

    QCommandLineOption renderPngOpt(
        QStringLiteral("render-png"),
        QStringLiteral("Render FILE's page straight to --png-out and exit. Used to "
                       "tell a soft render apart from soft compositing."),
        QStringLiteral("file"));
    parser.addOption(renderPngOpt);
    QCommandLineOption pngOutOpt(QStringLiteral("png-out"),
                                 QStringLiteral("Output PNG path for --render-png."),
                                 QStringLiteral("path"));
    parser.addOption(pngOutOpt);
    QCommandLineOption noLcdOpt(QStringLiteral("no-lcd"),
        QStringLiteral("Disable subpixel (LCD) text for --render-png."));
    parser.addOption(noLcdOpt);
    QCommandLineOption scaleOpt(QStringLiteral("scale"),
                                QStringLiteral("Render scale for --render-png "
                                               "(default 2.0)."),
                                QStringLiteral("x"));
    parser.addOption(scaleOpt);
    QCommandLineOption tocWorkerOpt(QStringLiteral("toc-worker"),
        QStringLiteral("Generate the automatic TOC in an isolated worker process."),
        QStringLiteral("file"));
    parser.addOption(tocWorkerOpt);
    QCommandLineOption noPluginsOpt(QStringLiteral("no-plugins"),
        QStringLiteral("Skip automatic plugin loading for this launch; saved preferences are retained."));
    parser.addOption(noPluginsOpt);
    QCommandLineOption pluginSelfTestOpt(QStringLiteral("selftest-plugins"),
        QStringLiteral("Verify the bundled plugin, commands and UI cleanup using FILE."), QStringLiteral("file"));
    parser.addOption(pluginSelfTestOpt);
    parser.process(app);

    if (parser.isSet(tocWorkerOpt)) {
        PrepareTestOutput();
        return app::RunTocWorker(parser.value(tocWorkerOpt));
    }
    MainWindow window(nullptr, !parser.isSet(noPluginsOpt) && !parser.isSet(pluginSelfTestOpt));
    if (parser.isSet(pluginSelfTestOpt)) {
        PrepareTestOutput();
        window.setAttribute(Qt::WA_DontShowOnScreen);
        window.show();
        int exitCode = 1;
        const bool opened = window.openDocument(parser.value(pluginSelfTestOpt));
        QTimer::singleShot(180, &app, [&] {
            auto* manager = window.pluginManager();
            manager->discover();
            QString error;
            const bool enabled = manager->setEnabled("org.pdfreader.reading-tools", true, &error);
            auto* copy = window.findChild<QAction*>("plugin:org.pdfreader.reading-tools/copy-page");
            const auto text = window.pluginPageText(window.pluginDocument().currentPage);
            if (copy) copy->trigger();
            const bool copied = copy && !text.isEmpty() && QApplication::clipboard()->text() == text;
            const bool disabled = manager->setEnabled("org.pdfreader.reading-tools", false, &error) &&
                !window.findChild<QAction*>("plugin:org.pdfreader.reading-tools/copy-page");
            exitCode = opened && enabled && copied && disabled ? 0 : 1;
            std::printf("PLUGIN SELFTEST %s (open=%d load=%d text=%d cleanup=%d) %s\n",
                exitCode == 0 ? "PASS" : "FAIL", opened, enabled, copied, disabled, error.toUtf8().constData());
            std::fflush(stdout);
            app.quit();
        });
        app.exec();
        return exitCode;
    }

    // Pure PDFium render, bypassing the view entirely: this is what the page
    // looks like before Qt composites it onto the screen.
    if (parser.isSet(renderPngOpt)) {
        PrepareTestOutput();
        const QString src = parser.value(renderPngOpt);
        const QString dst = parser.value(pngOutOpt);
        const int     page = std::max(0, parser.value(pageOpt).toInt() - 1);
        const double  scale = parser.value(scaleOpt).toDouble() > 0
                                  ? parser.value(scaleOpt).toDouble()
                                  : 2.0;

        pre::Library  lib;
        pre::Document doc;
        std::string   err;
        if (!doc.Open(src.toUtf8().constData(), &err)) {
            std::printf("RENDER FAIL: %s\n", err.c_str());
            return 1;
        }
        // Only override when asked: an unconditional SetLcdText() here would
        // silently undo the library default on every --render-png run.
        if (parser.isSet(noLcdOpt)) pre::SetLcdText(false);
        pre::RenderedPage pageData;
        if (!pre::RenderPage(doc.handle(), page, scale, &pageData)) {
            std::printf("RENDER FAIL: cannot render page %d\n", page + 1);
            return 1;
        }
        QImage img(pageData.pixels.data(), pageData.width, pageData.height,
                   pageData.stride, QImage::Format_RGB32);
        if (!img.save(dst)) {
            std::printf("RENDER FAIL: cannot write %s\n", dst.toUtf8().constData());
            return 1;
        }
        std::printf("rendered page %d at scale %g -> %dx%d px -> %s\n", page + 1,
                    scale, pageData.width, pageData.height,
                    dst.toUtf8().constData());
        std::fflush(stdout);
        return 0;
    }

    if (parser.isSet(benchOpt)) {
        PrepareTestOutput();
        const QString src = parser.value(benchOpt);
        const bool    opened = window.openDocument(src);
        // A hidden window has a ~33x16 viewport, which makes every measurement
        // meaningless. Size and show it before timing anything.
        window.resize(1400, 900);
        window.show();
        QTimer::singleShot(1800, &app, [&] {
            if (!opened) {
                std::printf("BENCH FAIL: cannot open %s\n", src.toUtf8().constData());
            } else {
                std::printf("%s\n", window.runBenchmark().toUtf8().constData());
                std::printf("%s\n", window.benchmarkResize().toUtf8().constData());
                std::fflush(stdout);
            }
            QApplication::quit();
        });
        app.exec();
        return opened ? 0 : 1;
    }

    if (parser.isSet(printTestOpt)) {
        PrepareTestOutput();
        const QString src    = parser.value(printTestOpt);
        const QString outPdf = parser.value(printOutOpt);
        const int     from   = std::max(1, parser.value(pageOpt).toInt());
        const int     to     = std::max(from, parser.value(pageToOpt).toInt());

        const bool opened = window.openDocument(src);
        int        exitCode = 1;
        QTimer::singleShot(600, &app, [&] {
            if (!opened) {
                std::printf("PRINT FAIL: cannot open %s\n", src.toUtf8().constData());
            } else if (outPdf.isEmpty()) {
                std::printf("PRINT FAIL: --print-out is required\n");
            } else {
                QString detail;
                const int printed =
                    window.printToPdf(outPdf, from - 1, to - 1, &detail);
                std::printf("%s\n", detail.toUtf8().constData());
                std::printf("%s\n", printed > 0 ? "PRINT PASS" : "PRINT FAIL");
                exitCode = printed > 0 ? 0 : 1;
            }
            std::fflush(stdout);
            QApplication::quit();
        });
        app.exec();
        return exitCode;
    }

    if (parser.isSet(tabsTestOpt)) {
        PrepareTestOutput();
        window.show();
        int exitCode = 1;
        const QStringList files = parser.value(tabsTestOpt).split(QLatin1Char(';'),
                                                                  Qt::SkipEmptyParts);
        QTimer::singleShot(400, &app, [&] {
            for (const QString& f : files) window.openDocument(f);
            window.waitForTocs();

            QStringList out;
            bool ok = true;
            auto check = [&](bool cond, const QString& what) {
                out << QString("%1  %2").arg(cond ? "PASS" : "FAIL").arg(what);
                if (!cond) ok = false;
            };

            check(window.docTabCount() == files.size(),
                  QString("opened %1 tab(s)").arg(window.docTabCount()));

            if (window.docTabCount() >= 2) {
                const int pc0 = window.tabPageCount(0);
                const int pc1 = window.tabPageCount(1);
                check(pc0 > 0 && pc1 > 0 && pc0 != pc1,
                      QString("tabs hold different documents (%1 vs %2 pages)")
                          .arg(pc0).arg(pc1));
                out << QString("INFO  toc nodes: [0]=%1 (%2)  [1]=%3 (%4)")
                           .arg(window.tabTocNodes(0)).arg(window.tabTocSource(0))
                           .arg(window.tabTocNodes(1)).arg(window.tabTocSource(1));
                check(window.tabTocNodes(0) > 0 && window.tabTocNodes(1) > 0,
                      "each tab built its own table of contents");

                // Reading position must be per-tab.
                window.activateDocTab(0);
                const int wantPage = std::min(10, window.tabPageCount(0) - 1);
                check(window.setActivePage(wantPage),
                      QString("tab 0 moved to page %1").arg(wantPage + 1));
                window.activateDocTab(1);
                check(window.tabPage(1) == 0,
                      QString("tab 1 kept its own position (page %1)")
                          .arg(window.tabPage(1) + 1));
                window.activateDocTab(0);
                check(window.tabPage(0) == wantPage,
                      QString("returning to tab 0 restored page %1 (got %2)")
                          .arg(wantPage + 1).arg(window.tabPage(0) + 1));

                // View configuration must be per-tab too.
                window.activateDocTab(0);
                check(window.setActiveViewMode(1) && window.setActiveFitMode(2),
                      "tab 0 switched to single-page + fit-page");
                window.activateDocTab(1);
                check(window.activeViewMode() == 0 && window.activeFitMode() == 1,
                      QString("tab 1 kept its own view mode (view=%1 fit=%2)")
                          .arg(window.activeViewMode()).arg(window.activeFitMode()));
                window.activateDocTab(0);
                check(window.activeViewMode() == 1 && window.activeFitMode() == 2,
                      QString("returning to tab 0 restored its view mode (view=%1 fit=%2)")
                          .arg(window.activeViewMode()).arg(window.activeFitMode()));

                // Closing one tab must leave the other intact.
                const int pc0again = window.tabPageCount(0);
                window.closeDocTab(1);
                check(window.docTabCount() == 1, "closing a tab removes exactly one");
                check(window.tabPageCount(0) == pc0again,
                      "the surviving tab still has its document");
                check(window.tabPage(0) == wantPage,
                      "the surviving tab kept its reading position");
            }

            std::printf("%s\n", out.join('\n').toUtf8().constData());
            std::printf("%s\n", ok ? "TABS PASS" : "TABS FAIL");
            std::fflush(stdout);
            exitCode = ok ? 0 : 1;
            QApplication::quit();
        });
        app.exec();
        return exitCode;
    }

    if (parser.isSet(sessionWriteOpt) || parser.isSet(sessionReadOpt)) {
        PrepareTestOutput();
        const bool writing = parser.isSet(sessionWriteOpt);
        const QString file = parser.value(writing ? sessionWriteOpt : sessionReadOpt);
        const int    page  = parser.value(pageOpt).toInt();
        const double zoom  = parser.value(zoomOpt).toDouble();

        window.show();
        if (!window.openDocument(file)) {
            std::printf("SESSION FAIL: cannot open %s\n", file.toUtf8().constData());
            return 1;
        }

        int exitCode = 1;
        QTimer::singleShot(700, &app, [&] {
            if (writing) {
                window.writeSessionState(page, zoom);
                std::printf("stored page=%d zoom=%.3f\n", page, zoom);
                exitCode = 0;
            } else {
                QString detail;
                const bool ok = window.verifySessionRestore(page, zoom, &detail);
                std::printf("observed: %s\n", detail.toUtf8().constData());
                std::printf("%s\n", ok ? "SESSION PASS" : "SESSION FAIL");
                exitCode = ok ? 0 : 1;
            }
            std::fflush(stdout);
            QApplication::quit();
        });
        app.exec();
        return exitCode;
    }

    if (parser.isSet(selfTest)) {
        PrepareTestOutput();
        // Show the window and let layout settle first: the fit-to-width check
        // is only meaningful against the geometry the user actually sees.
        window.show();
        const bool opened = window.openDocument(parser.value(selfTest));

        int exitCode = 1;
        QTimer::singleShot(600, &app, [&] {
            QString report;
            const bool ok = opened && window.runSelfTest(&report);
            // runSelfTest already streams each PASS/FAIL as it happens, so only
            // the INFO lines still need printing here.
            for (const QString& line : report.split(QLatin1Char('\n'))) {
                if (line.startsWith(QLatin1String("INFO"))) {
                    std::printf("%s\n", line.toUtf8().constData());
                }
            }
            std::printf("%s\n", ok ? "SELFTEST PASS" : "SELFTEST FAIL");
            std::fflush(stdout);
            exitCode = ok ? 0 : 1;
            QApplication::quit();
        });
        app.exec();
        return exitCode;
    }

    window.show();

    const QStringList args = parser.positionalArguments();
    if (args.isEmpty()) {
        // Nothing on the command line: bring back what was open last time.
        QTimer::singleShot(0, &app, [&window] { window.restoreSessionTabs(); });
    } else {
        for (const QString& a : args) window.openDocument(a);
    }

    if (parser.isSet(printTabsOpt)) {
        PrepareTestOutput();
        int exitCode = 1;
        QTimer::singleShot(1600, &app, [&] {
            const int n = window.docTabCount();
            std::printf("tabs=%d\n", n);
            for (int i = 0; i < n; ++i) {
                std::printf("  [%d] %-22s pages=%-4d toc=%-4d at page %d\n", i,
                            window.tabTitle(i).toUtf8().constData(),
                            window.tabPageCount(i), window.tabTocNodes(i),
                            window.tabPage(i) + 1);
            }
            std::fflush(stdout);
            exitCode = n > 0 ? 0 : 1;
            QApplication::quit();
        });
        app.exec();
        return exitCode;
    }

    if (parser.isSet(sideOpt) || parser.isSet(invertOpt)) {
        const QString which = parser.value(sideOpt);
        const bool    inv   = parser.isSet(invertOpt);
        QTimer::singleShot(800, &app, [&window, which, inv] {
            if (!which.isEmpty()) window.showSidebarPage(which == QLatin1String("bookmarks") ? 1 : 0);
            if (inv) window.toggleInvert();
        });
    }

    if (parser.isSet(viewOpt) || parser.isSet(fitOpt)) {
        const QString v = parser.value(viewOpt);
        const QString f = parser.value(fitOpt);
        QTimer::singleShot(700, &app, [&window, v, f] {
            if (!v.isEmpty()) {
                window.setActiveViewMode(v.startsWith(QLatin1Char('s')) ? 1 : 0);
            }
            if (!f.isEmpty()) {
                if (f.startsWith(QLatin1Char('p'))) window.setActiveFitMode(2);
                else if (f.startsWith(QLatin1Char('w'))) window.setActiveFitMode(1);
                else window.setActiveFitMode(0);
            }
        });
    }

    if (parser.isSet(findOpt)) {
        const QString needle = parser.value(findOpt);
        QTimer::singleShot(500, &app, [&window, needle] { window.startFind(needle); });
    }

    if (parser.isSet(selectAllOpt)) {
        QTimer::singleShot(500, &app, [&window] { window.selectAllText(); });
    }

    if (parser.isSet(selectLineOpt)) {
        const QStringList parts = parser.value(selectLineOpt).split(QLatin1Char(':'));
        if (parts.size() == 2) {
            // Deferred so the view has been laid out and rendered first.
            const int page = parts[0].toInt() - 1;
            const int line = parts[1].toInt();
            QTimer::singleShot(500, &app, [&window, page, line] {
                window.selectLine(page, line);
            });
        }
    }

    if (parser.isSet(markupOpt)) {
        const QString kind = parser.value(markupOpt);
        QTimer::singleShot(900, &app, [&window, kind] {
            const int k = kind.startsWith(QLatin1Char('u'))   ? 1
                          : kind.startsWith(QLatin1Char('s')) ? 2
                                                              : 0;
            window.applyMarkup(k);
        });
    }

    return app.exec();
}
