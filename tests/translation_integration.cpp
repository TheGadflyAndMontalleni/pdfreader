#include "mainwindow.h"
#include "pdfview.h"
#include "pluginmanager.h"
#include "tocworker.h"
#include "annot.h"
#include "fpdf_edit.h"
#include <QAction>
#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTest>
#include <cstdio>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (app.arguments().size() != 3) return 2;
    if (app.arguments()[1] == "--toc-worker") return app::RunTocWorker(app.arguments()[2]);
    QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/arial.ttf");
    const auto families = QFontDatabase::applicationFontFamilies(QFontDatabase::addApplicationFont(
        qEnvironmentVariable("WINDIR") + "/Fonts/msyh.ttc"));
    if (!families.isEmpty()) app.setFont(QFont(families[0], 9));
    QTemporaryDir temp;
    qputenv("PDFREADER_SETTINGS", (temp.path() + "/settings.ini").toUtf8());
    qunsetenv("DEEPSEEK_API_KEY");
    const QString fixture = temp.path() + "/reading.pdf";
    int failures = 0;
    auto check = [&](bool condition, const char* description) {
        std::printf("%s %s\n", condition ? "PASS" : "FAIL", description); std::fflush(stdout);
        if (!condition) ++failures;
    };
    MainWindow window(nullptr, false);
    window.setAttribute(Qt::WA_DontShowOnScreen); window.resize(1120, 780); window.show();
    {
        // Use PDFium's standard font so fixture extraction is independent of
        // the platform's QPdfWriter font encoding and native font engine.
        FPDF_DOCUMENT pdf = FPDF_CreateNewDocument();
        for (int page = 0; page < 3; ++page) {
            FPDF_PAGE sheet = FPDFPage_New(pdf, page, 600, 800);
            for (int line = 0; line < 19; ++line) {
                const QString text = line == 0 ? "hypothesis" : "Reading makes ideas clearer. A hypothesis can be tested.";
                FPDF_PAGEOBJECT object = FPDFPageObj_NewTextObj(pdf, "Helvetica", 14);
                FPDFText_SetText(object, reinterpret_cast<FPDF_WIDESTRING>(text.utf16()));
                FPDFPageObj_Transform(object, 1, 0, 0, 1, 50, line == 0 ? 710 : 660 - (line - 1) * 24);
                FPDFPage_InsertObject(sheet, object);
            }
            FPDFPage_GenerateContent(sheet); FPDF_ClosePage(sheet);
        }
        check(pre::SaveDocumentToFile(pdf, fixture.toUtf8().constData()), "creates a platform-independent synthetic text PDF");
        FPDF_CloseDocument(pdf);
    }
    QString error, id;
    auto* manager = window.pluginManager();
    check(manager->install(app.arguments()[1], &id, &error), "installs the actual single-file plugin DLL");
    check(manager->setEnabled(id, true, &error), "initializes plugin in the actual reader");
    check(window.openDocument(fixture), "opens a synthetic document without a user key");
    window.waitForTocs(); QTest::qWait(250);
    auto* view = window.findChild<PdfView*>();
    check(view && view->selectLine(0, 0), "selects the word on a rendered page");
    auto* action = window.findChild<QAction*>("plugin:org.pdfreader.deepseek-translate/translate");
    check(action != nullptr, "registers the translation command");
    if (!view || !action) return 1;
    auto* geometry = qobject_cast<pdfreader::SelectionGeometryV1*>(action->parent());
    check(geometry && !geometry->selectionScreenRect().isEmpty(), "discovers the optional geometry capability across the DLL boundary");
    action->trigger(); QTest::qWait(250);
    auto* popup = window.findChild<QWidget*>("translationGlassPopup");
    auto* result = window.findChild<QPlainTextEdit*>("translationResult");
    check(popup && popup->isVisible() && result && result->toPlainText().contains(QStringLiteral("假")),
          "looks up the real dictionary entry offline inside the reader");
    if (!popup || !result || !geometry) return 1;
    check(!popup->frameGeometry().intersects(geometry->selectionScreenRect()), "popup does not obscure the actual selected glyphs");
    const auto artifact = app.arguments()[2];
    QImage composite(window.size(), QImage::Format_ARGB32_Premultiplied); composite.fill(Qt::white);
    {
        QPainter painter(&composite); painter.drawPixmap(0, 0, window.grab());
        painter.drawPixmap(window.mapFromGlobal(popup->pos()), popup->grab());
    }
    check(composite.save(artifact), "renders a composite screenshot of the reader and glass popup");
    const auto before = popup->pos(); const auto anchorBefore = geometry->selectionScreenRect();
    window.move(window.pos() + QPoint(30, 25)); QTest::qWait(600);
    // A never-shown native Windows HWND keeps its native screen position.
    // In that headless mode exercise a real layout change instead.
    if (geometry->selectionScreenRect() == anchorBefore) {
        view->setFitMode(PdfView::FitMode::None); view->setZoom(view->zoom() * 1.1); QTest::qWait(400);
    }
    std::printf("INFO popup %d,%d -> %d,%d; anchor %d,%d -> %d,%d; visible=%d\n", before.x(), before.y(), popup->pos().x(), popup->pos().y(),
        anchorBefore.x(), anchorBefore.y(), geometry->selectionScreenRect().x(), geometry->selectionScreenRect().y(), popup->isVisible());
    check(!popup->frameGeometry().intersects(geometry->selectionScreenRect()) && popup->pos() != before,
          "popup follows changed reader geometry without covering the selection");
    view->setPage(2); QTest::qWait(300);
    check(geometry->selectionScreenRect().isEmpty() && !popup->isVisible(), "scrolling the selection offscreen hides the popup");
    check(manager->setEnabled(id, false, &error) && !window.findChild<QWidget*>("translationGlassPopup"),
          "disabling removes the popup and its follow timers");
    std::printf("TRANSLATION INTEGRATION %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
