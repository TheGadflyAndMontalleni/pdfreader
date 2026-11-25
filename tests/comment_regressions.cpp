#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProcess>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <algorithm>
#include <cstdio>
#include "mainwindow.h"
#include "pdfview.h"
#include "commentpanel.h"
#include "tocworker.h"
#include "printing.h"
#include "fpdf_annot.h"
#include "fpdf_edit.h"

namespace {
bool MakeFixture(const QString& path) {
    FPDF_DOCUMENT doc = FPDF_CreateNewDocument();
    if (!doc) return false;
    for (int p = 0; p < 3; ++p) {
        FPDF_PAGE page = FPDFPage_New(doc, p, 600, 800);
        for (int line = 0; line < 3; ++line) {
            const QString text = QString("Page %1 line %2: Reading invites questions and ideas.").arg(p + 1).arg(line + 1);
            FPDF_PAGEOBJECT object = FPDFPageObj_NewTextObj(doc, "Helvetica", 14);
            FPDFText_SetText(object, reinterpret_cast<FPDF_WIDESTRING>(text.utf16()));
            FPDFPageObj_Transform(object, 1, 0, 0, 1, 50, 700 - line * 260);
            FPDFPage_InsertObject(page, object);
        }
        // An unrelated annotation must survive editing/deleting our comments.
        if (p == 0) {
            FPDF_ANNOTATION foreign = FPDFPage_CreateAnnot(page, FPDF_ANNOT_TEXT);
            const FS_RECTF r{540, 760, 560, 740};
            FPDFAnnot_SetRect(foreign, &r);
            const QString note = "An existing note from another application";
            FPDFAnnot_SetStringValue(foreign, "Contents", reinterpret_cast<FPDF_WIDESTRING>(note.utf16()));
            FPDFPage_CloseAnnot(foreign);
        }
        FPDFPage_GenerateContent(page);
        FPDF_ClosePage(page);
    }
    const bool ok = pre::SaveDocumentToFile(doc, path.toUtf8().constData());
    FPDF_CloseDocument(doc);
    return ok;
}

bool MakeColumnFixture(const QString& path) {
    FPDF_DOCUMENT doc = FPDF_CreateNewDocument();
    if (!doc) return false;
    for (int p = 0; p < 2; ++p) {
        FPDF_PAGE page = FPDFPage_New(doc, p, 600, 800);
        auto write = [&](int x, int y, const QString& text) {
            FPDF_PAGEOBJECT object = FPDFPageObj_NewTextObj(doc, "Courier", 12);
            FPDFText_SetText(object, reinterpret_cast<FPDF_WIDESTRING>(text.utf16()));
            FPDFPageObj_Transform(object, 1, 0, 0, 1, x, y);
            FPDFPage_InsertObject(page, object);
        };
        write(72, 740, "A heading across both columns of the page");
        const int right = p == 0 ? 230 : 213;
        // Interleave PDF objects. Page two also has staggered baselines and a
        // narrow gutter (the longest left line ends at x=204).
        for (int row = 0; row < 4; ++row) {
            const int y = 710 - row * 20;
            write(right, y - (p ? 7 : 0), QString("Right %1").arg(row + 1));
            write(row == 1 ? 52 : 45, y, row == 2 ? "Left 3 is a wider line" : QString("Left %1").arg(row + 1));
        }
        write(72, 625, "An interlude across both columns of the page");
        write(45, 603, "After left one"); write(right, 603, "After right one");
        write(45, 583, "After left two"); write(right, 583, "After right two");
        FPDFPage_GenerateContent(page);
        FPDF_ClosePage(page);
    }
    const bool ok = pre::SaveDocumentToFile(doc, path.toUtf8().constData());
    FPDF_CloseDocument(doc);
    return ok;
}

QByteArray Digest(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
}
} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("PDF Reader");
    const auto args = app.arguments();
    if (args.size() == 3 && args[1] == "--toc-worker") return app::RunTocWorker(args[2]);
    if (args.size() == 4 && args[1] == "--check-comment") {
        const auto saved = app::LoadLocalComments(args[2], 3);
        return saved && saved->size() == 1 && (*saved)[0].text == args[3] ? 0 : 1;
    }
    QTemporaryDir temp;
    if (!temp.isValid()) return 2;
    qputenv("PDFREADER_SETTINGS", (temp.path() + "/settings.ini").toUtf8());
    int failures = 0, checks = 0;
    auto check = [&](bool ok, const char* message) {
        ++checks;
        std::printf("%s  %s\n", ok ? "PASS" : "FAIL", message); std::fflush(stdout);
        if (!ok) ++failures;
    };
    MainWindow window(nullptr, false);
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.resize(1440, 1000); window.show(); app.processEvents();
    const QString source = temp.path() + "/source.pdf";
    const QString other = temp.path() + "/other.pdf";
    check(MakeFixture(source) && QFile::copy(source, other), "creates a three-page fixture with an unrelated existing annotation");
    const QByteArray original = Digest(source);
    check(window.openDocument(source), "opens the comment fixture");
    auto* view = window.findChild<PdfView*>();
    auto* panel = window.findChild<CommentPanel*>();
    auto* action = window.findChild<QAction*>("newComment");
    check(panel && panel->isHidden() && action && !action->isEnabled(), "comment pane starts collapsed and new-comment action requires a selection");
    check(!window.addComment("unanchored"), "rejects a comment without selected text");
    check(view->selectLine(0, 0) && action->isEnabled(), "selecting text enables the new-comment action");
    const QString quote = view->selectedText();
    action->trigger(); app.processEvents(); QTest::qWait(180);
    check(window.comments().size() == 1 && panel->isVisible() && !view->hasSelection(),
          "toolbar action creates an anchored comment, opens its margin card and clears the blue selection");
    if (window.comments().isEmpty()) return 1;
    const QString id = window.comments()[0].id;
    auto* editor = panel->findChild<QPlainTextEdit*>("commentEditor:" + id);
    check(editor && window.comments()[0].quote == quote && window.comments()[0].anchors.size() == 1,
          "card preserves the exact selected quotation and PDF coordinates");
    const QString note = QString::fromUtf8("这里的论据值得再核对。\nA note with 中文 and emoji 📝 <b>plain text</b>.");
    if (editor) editor->setPlainText(note);
    QTest::qWait(450);
    check(window.comments()[0].text == note && app::LoadLocalComments(source, 3)->at(0).text == note,
          "typing in a card autosaves Unicode, multiline and literal markup text");
    QProcess persistence;
    persistence.start(QCoreApplication::applicationFilePath(), {"--check-comment", source, note});
    check(persistence.waitForFinished(10000) && persistence.exitCode() == 0, "a separate process reads the autosaved comment");
    const auto first = window.comments()[0];
    const QRectF anchor = view->commentRect(0, first.anchors.begin()->second.front());
    check(view->viewport()->rect().contains(anchor.center().toPoint()), "new comment scrolls its selected passage into view after the sidebar opens");
    const auto shot = view->grabViewport().convertToFormat(QImage::Format_RGB32);
    const qreal dpr = shot.devicePixelRatio();
    const QRect area(qRound(anchor.left() * dpr), qRound(anchor.top() * dpr), qRound(anchor.width() * dpr), qRound(anchor.height() * dpr));
    int yellow = 0;
    const QRect sample = area.intersected(shot.rect());
    for (int y = sample.top(); y <= sample.bottom(); ++y) for (int x = sample.left(); x <= sample.right(); ++x) {
        const QRgb pixel = shot.pixel(x, y);
        if (qRed(pixel) > 190 && qGreen(pixel) > 150 && qBlue(pixel) < qGreen(pixel) - 20) ++yellow;
    }
    check(yellow > 100, "the comment highlight is visible at real screen resolution");
    window.findChild<QAction*>("showComments")->setChecked(false); app.processEvents(); QTest::qWait(180);
    view->goToComment(id);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier,
        view->commentRect(0, first.anchors.begin()->second.front()).center().toPoint());
    app.processEvents();
    check(panel->isVisible() && editor && window.focusWidget() == editor, "clicking the marked passage opens and focuses the matching card");
    // Editing and switching before the debounce fires must not lose text.
    const QString immediate = note + QString::fromUtf8("\n切换标签前的最后一句。");
    if (editor) editor->setPlainText(immediate);
    check(window.openDocument(other) && window.comments().isEmpty(), "another PDF has its own empty comment model");
    check(app::LoadLocalComments(source, 3)->at(0).text == immediate, "switching tabs flushes edits before the autosave delay expires");
    window.activateDocTab(0);
    check(window.comments().size() == 1 && window.comments()[0].text == immediate, "switching back restores the right document's cards");
    view->setViewMode(PdfView::ViewMode::SinglePage); view->selectLine(2, 2);
    check(window.addComment(QString::fromUtf8("页尾批注")), "adds a second comment on a different page in single-page mode");
    const QString second = window.comments().back().id;
    check(window.activateComment(second) && view->page() == 2 &&
          view->viewport()->rect().intersects(view->commentRect(2, window.comments().back().anchors.begin()->second.front()).toRect()),
          "card navigation reveals a passage near the bottom of a single page");
    view->setViewMode(PdfView::ViewMode::Continuous); view->selectAll();
    check(window.addComment(QString::fromUtf8("跨页批注")) && window.comments().size() == 3,
          "one comment can span multiple pages");
    QString cross;
    for (const auto& c : window.comments()) if (c.anchors.size() == 3) cross = c.id;
    check(!cross.isEmpty(), "cross-page comment retains line rectangles on all three pages");
    check(window.editComment(cross, QString::fromUtf8("更新跨页批注")), "edits a comment spanning several pages");
    const QString crossExport = temp.path() + "/cross-page.pdf";
    pre::Document crossPdf;
    bool crossSaved = window.saveDocumentToPath(crossExport) && crossPdf.Open(crossExport.toUtf8().constData());
    int crossPages = 0;
    if (crossSaved) for (const auto& c : app::ReadPdfComments(crossPdf.handle()))
        if (c.id == cross && c.text == QString::fromUtf8("更新跨页批注")) crossPages = int(c.anchors.size());
    check(crossPages == 3, "cross-page annotation text and geometry survive native PDF export");
    auto* deleteCross = panel->findChild<QToolButton*>("commentDelete:" + cross);
    if (deleteCross) QTest::mouseClick(deleteCross, Qt::LeftButton);
    check(deleteCross && window.comments().size() == 2, "the card Delete button removes a cross-page comment from every page");
    const QString exported = temp.path() + QString::fromUtf8("/带批注的文档.pdf");
    check(window.saveDocumentToPath(exported), "saves a PDF with comments to a Unicode path");
    pre::Document reopened;
    check(reopened.Open(exported.toUtf8().constData()), "reopens the exported PDF independently");
    const auto native = app::ReadPdfComments(reopened.handle());
    bool contents = false;
    for (const auto& c : native) if (c.id == id) contents = c.text == immediate && c.quote == quote;
    check(native.size() == 2 && contents, "saved PDF contains editable native highlights, comment text and quotations");
    FPDF_PAGE page = FPDF_LoadPage(reopened.handle(), 0);
    check(page && FPDFPage_GetAnnotCount(page) == 2, "export preserves an unrelated annotation without duplicating our highlight");
    if (page) FPDF_ClosePage(page);
    const QString range = temp.path() + "/range.pdf";
    check(app::SavePagesToPdf(reopened.handle(), 3, range, 0, 0), "exports an annotated page range");
    pre::Document subset;
    check(subset.Open(range.toUtf8().constData()) && app::ReadPdfComments(subset.handle()).size() == 1,
          "vector page-range export carries the native comment");
    window.closeDocTab(1); window.closeDocTab(0);
    check(window.openDocument(source) && window.comments().size() == 2,
          "closing and reopening the original restores autosaved comments");
    check(window.openDocument(exported) && window.comments().size() == 2,
          "a saved PDF recovers comments from its embedded annotations without local state");
    check(window.removeComment(id) && window.removeComment(second), "removes comments from a reopened annotated PDF");
    window.closeDocTab(1);
    check(window.openDocument(exported) && window.comments().isEmpty(), "an empty autosave snapshot keeps embedded comments deleted on reopen");
    const QString cleaned = temp.path() + "/cleaned.pdf";
    check(window.saveDocumentToPath(cleaned), "saves deletion of embedded comments");
    pre::Document clean;
    clean.Open(cleaned.toUtf8().constData());
    page = FPDF_LoadPage(clean.handle(), 0);
    check(page && FPDFPage_GetAnnotCount(page) == 1 && app::ReadPdfComments(clean.handle()).isEmpty(),
          "deletion leaves unrelated PDF annotations intact");
    if (page) FPDF_ClosePage(page);
    check(Digest(source) == original, "autosaving, editing and deleting never rewrites the original PDF");
    // Exercise real selection and its floating action, including the fact that
    // the mouse drag must keep working over an existing highlighted passage.
    window.closeDocTab(1); window.activateDocTab(0);
    window.activateComment(id);
    const QPoint dragStart = view->posToViewport(TextPos{0, 1, 0});
    const QPoint dragEnd = view->posToViewport(TextPos{0, 1, 14});
    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, dragStart);
    QTest::mouseMove(view->viewport(), dragEnd);
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, dragEnd);
    auto* floating = view->findChild<QToolButton*>("selectionCommentButton");
    check(view->hasSelection() && floating && floating->isVisible(), "drag selection shows the floating new-comment button");
    if (floating && floating->isVisible()) QTest::mouseClick(floating, Qt::LeftButton);
    check(window.comments().size() == 3 && panel->isVisible(), "clicking the floating button adds a comment for the selected passage");
    QString floatingId;
    for (const auto& c : window.comments()) if (c.id != id && c.id != second) floatingId = c.id;
    window.removeComment(floatingId);
    // Replacing a file invalidates local anchors even when the path is reused.
    QFile modified(source); modified.open(QIODevice::Append); modified.write("\n% replacement\n"); modified.close();
    check(!app::LoadLocalComments(source, 3), "a changed source file rejects stale local comment coordinates");
    window.activateDocTab(0);
    const QString screenshots = QString::fromLocal8Bit(qgetenv("PDFREADER_TEST_ARTIFACTS"));
    if (!screenshots.isEmpty()) {
        QDir().mkpath(screenshots);
        window.activateComment(id);
        QTest::qWait(200);
        window.grab().save(screenshots + "/reader-comments.png");
    }
    const QString columns = temp.path() + "/columns.pdf";
    check(MakeColumnFixture(columns) && window.openDocument(columns), "opens a mixed full-width and two-column PDF");
    view->setFitMode(PdfView::FitMode::Page);
    window.waitForTocs(); app.processEvents();
    // Mouse points come from the printed fixture, independent of inferred
    // line indices. Test the same drag in both directions and document modes.
    auto point = [&](int page, int x, int y) {
        return view->commentRect(page, {double(x), double(y), double(x), double(y)}).topLeft().toPoint();
    };
    auto drag = [&](QPoint from, QPoint to) {
        QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(view->viewport(), to);
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, to);
    };
    const QString left = "Left 1\nLeft 2\nLeft 3 is a wider line";
    for (int p = 0; p < 2; ++p) {
        view->setViewMode(p ? PdfView::ViewMode::SinglePage : PdfView::ViewMode::Continuous);
        view->setPage(p); app.processEvents();
        drag(point(p, 44, 713), point(p, 205, 673));
        check(view->selectedText() == left && window.pluginSelectedText() == left,
              "multiline left-column drag gives the translation host exactly the selected passage");
        view->copySelectionToClipboard();
        check(QApplication::clipboard()->text() == left, "copy uses the same column order as translation");
        const auto geometry = view->selectionRectsByPage();
        const auto found = geometry.find(p);
        check(geometry.size() == 1 && found != geometry.end() && found->second.size() == 3 &&
              std::all_of(found->second.begin(), found->second.end(), [](const auto& r) { return r.right < 207; }),
              "left-column annotation quads end at the ink on each selected line");
        const QRect screen = window.pluginSelectionScreenRect();
        check(!screen.isEmpty() && screen.right() < view->viewport()->mapToGlobal(point(p, 210, 673)).x(),
              "the translation popup anchor stays inside the selected column");
        const int right = p ? 213 : 230;
        const int dy = p ? -7 : 0;
        drag(point(p, 44, 713), point(p, right - 3, 693));
        check(view->selectedText() == "Left 1\nLeft 2",
              "dragging past a short line into the blank gutter keeps the starting column");
        drag(point(p, right + 60, 673 + dy), point(p, right - 1, 713 + dy));
        check(view->selectedText() == "Right 1\nRight 2\nRight 3",
              "backward right-column drag on aligned or staggered lines selects in reading order");
        QTest::mouseDClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, point(p, right + 14, 693 + dy));
        check(view->selectedText() == "Right 2", "double click selects only the line under the mouse");
        drag(point(p, 44, 653), point(p, right + 60, 713 + dy));
        check(view->selectedText() == "Left 4\nRight 1", "an intentional cross-column drag continues into the next column");
    }
    const QString expected = "A heading across both columns of the page\n" + left +
        "\nLeft 4\nRight 1\nRight 2\nRight 3\nRight 4\nAn interlude across both columns of the page\n"
        "After left one\nAfter left two\nAfter right one\nAfter right two";
    check(window.pluginPageText(1) == expected && window.pluginPageText(2) == expected,
          "whole-page translation context follows columns around full-width headings");
    view->selectAll();
    check(view->selectedText() == expected + "\n" + expected, "whole-document selection follows the same order across pages");
    window.startFind("Right 2");
    check(view->matchCount() == 2 && view->selectMatch(1) && view->selectedText() == "Right 2",
          "a search result resolves to its original characters on the second page");
    view->setZoom(view->zoom() * 1.35);
    view->setPage(1);
    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, point(1, 212, 706));
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, point(1, 274, 666));
    const QString columnQuote = "Right 1\nRight 2\nRight 3";
    check(view->selectedText() == columnQuote, "at manual zoom even a coalesced drag uses the final release position");
    const bool added = window.addComment("two-column note");
    const QString columnExport = temp.path() + "/column-note.pdf";
    pre::Document columnPdf;
    const bool saved = added && window.saveDocumentToPath(columnExport) && columnPdf.Open(columnExport.toUtf8().constData());
    const auto notes = saved ? app::ReadPdfComments(columnPdf.handle()) : QVector<app::Comment>();
    const auto quads = notes.size() == 1 && notes.front().anchors.count(1) ?
        notes.front().anchors.at(1) : std::vector<pre::MarkupRect>();
    check(notes.size() == 1 && notes.front().quote == columnQuote && quads.size() == 3 &&
          std::all_of(quads.begin(), quads.end(), [](const auto& r) { return r.left >= 212 && r.right < 275; }),
          "exported PDF reopens with the exact right-column quote and three separate right-column quads");
    // A justified paragraph can have large *generated* word spaces. PDFium
    // places some at the next glyph's pen origin, even if its ink overhangs.
    std::vector<pre::CharInfo> paragraph;
    for (double y : {100., 86., 72.}) {
        auto glyph = [&](uint32_t cp, double x, double l, double r, double b, double t) {
            pre::CharInfo c;
            c.codepoint = cp; c.originX = x; c.originY = y; c.hasOrigin = true;
            c.left = l; c.right = r; c.bottom = y + b; c.top = y + t;
            c.fontSize = 10; c.generated = cp == ' '; c.hyphen = cp == 2;
            paragraph.push_back(c);
        };
        glyph('A', 45, 45, 51, 0, 7);
        glyph(' ', 59, 59, 59, 0, 0);
        glyph('j', 59, 58, 62, -2, 5);
        glyph(',', 62, 62, 64, -2, 1);
        glyph(2, 65, 65, 68, 2, 3);
    }
    const auto justified = pre::BuildReadingPage(0, paragraph);
    check(justified.columns.size() == 1 && justified.lines.size() == 3 &&
          std::all_of(justified.lines.begin(), justified.lines.end(), [](const auto& l) { return l.text == "A j,-"; }),
          "justification, an overhanging letter, low punctuation and a visible wrap hyphen stay on one reading line");
    std::printf("COMMENT REGRESSIONS %s (%d checks, %d failures)\n", failures == 0 ? "PASS" : "FAIL", checks, failures);
    return failures == 0 ? 0 : 1;
}
