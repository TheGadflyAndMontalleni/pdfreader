// Continuous-scroll document view.
//
// SumatraPDF's defining reading experience is a single vertical strip of pages
// you scroll through seamlessly, rather than one page at a time. This view
// implements that: it owns the scrollbars, lays every page out at the current
// zoom, renders only what is visible, and keeps a bounded cache of rendered
// pages so scrolling back up is instant.
#pragma once

#include <QAbstractScrollArea>
#include <QImage>
#include <QSizeF>
#include <QString>

#include <map>
#include <vector>

#include "find.h"
#include "annot.h"
#include "render.h"
#include "comments.h"

namespace pre {
class PageIndex;
}
class QToolButton;

// A position in the document's text, addressed the way a reader moves through
// it: which page, which reconstructed line, and how far along that line.
struct TextPos {
    int page = -1;
    int line = -1;
    int pos  = 0;   // index into LineInfo::charIndices

    bool valid() const { return page >= 0 && line >= 0; }
    // Reading order comparison; invalid positions sort first.
    bool operator<(const TextPos& o) const {
        if (page != o.page) return page < o.page;
        if (line != o.line) return line < o.line;
        return pos < o.pos;
    }
    bool operator==(const TextPos& o) const {
        return page == o.page && line == o.line && pos == o.pos;
    }
};

class PdfView : public QAbstractScrollArea {
    Q_OBJECT

public:
    // How the zoom tracks the window. None means the user set it explicitly.
    enum class FitMode { None = 0, Width = 1, Page = 2 };
    // Continuous is the scrolling strip; Single shows one page at a time, the
    // way SumatraPDF's single-page view does.
    enum class ViewMode { Continuous = 0, SinglePage = 1 };

    explicit PdfView(QWidget* parent = nullptr);

    // The view never owns the document or the index; both must outlive it.
    void setDocument(FPDF_DOCUMENT doc, int pageCount, pre::PageIndex* index);
    void clearDocument();

    int    page() const { return currentPage_; }   // topmost visible page
    int    pageCount() const { return pageCount_; }
    double zoom() const { return zoom_; }
    bool   hasImage() const { return !cache_.empty(); }
    QImage cachedPageImage(int page) const;
    qint64 cachedBytes() const;

    // Page size in points at 1:1 zoom.
    QSizeF pageSizePoints(int pageIndex) const;

    // Total height of the laid-out content, in device pixels. In continuous
    // mode this is one tall strip for the whole document, which is what makes
    // scrolling seamless; in single-page mode it is just the current page.
    int contentHeight() const;

    // In a fit mode the zoom is recomputed by the view itself whenever its
    // viewport resizes. Doing it here rather than in the window matters: the
    // window's resizeEvent runs before the splitter has resized this widget, so
    // a zoom computed there uses a stale width and clips the page.
    void     setFitMode(FitMode mode);
    FitMode  fitMode() const { return fitMode_; }
    void     setViewMode(ViewMode mode);
    ViewMode viewMode() const { return viewMode_; }

    void setFitWidthMode(bool on);   // convenience: on == FitMode::Width
    bool fitWidthMode() const { return fitMode_ == FitMode::Width; }
    void fitPage();

    // Invert the rendered page (SumatraPDF's "i"). Applied when a page is
    // rasterised, so the cached image already carries it.
    void setInvertColors(bool on);
    bool invertColors() const { return invert_; }

    // ---- text selection ----
    bool    hasSelection() const { return hasSelection_; }
    QString selectedText();
    QRect selectionScreenRect();
    // The selection as one rectangle per line, in PDF page space, grouped by
    // page. This is what a markup mark needs: a highlight follows the lines,
    // not the bounding box of the whole selection.
    std::map<int, std::vector<pre::MarkupRect>> selectionRectsByPage();
    // Drop cached rasters so the next paint reflects a changed document.
    void invalidateRenderedPages();
    void    clearSelection();
    void    selectAll();
    // Whole line, used by tests and by the double-click path.
    bool    selectLine(int pageIndex, int lineIndex);
    void    copySelectionToClipboard();
    // Viewport coordinates of a text position (for tests and future carets).
    QPoint  posToViewport(const TextPos& p);
    // Rendered viewport pixels, so tests can assert on what is actually drawn.
    QImage  grabViewport();

    void setComments(const QVector<app::Comment>& comments);
    void setActiveComment(const QString& id);
    bool goToComment(const QString& id);
    QRectF commentRect(int page, const pre::MarkupRect& rect) const;

    // ---- find ----
    void setFindResults(const pre::FindResults& results);
    void clearFindResults();
    int  matchCount() const { return static_cast<int>(find_.matches.size()); }
    int  currentMatchIndex() const { return currentMatch_; }   // -1 when none
    bool goToNextMatch();
    bool goToPrevMatch();
    // Select exactly the characters a match covers (also used to verify that
    // the reported position really holds the searched text).
    bool    selectMatch(int index);
    QString matchText(int index);

public slots:
    void setPage(int index);          // scroll so `index` starts at the top
    void setZoom(double zoom);        // keeps the current page anchored
    void fitWidth();
    void zoomIn();
    void zoomOut();
    void goToNextPage();
    void goToPrevPage();
    void goToFirstPage();
    void goToLastPage();

signals:
    void openRequested();
    void configurationChanged();
    void pageChanged(int page);
    void zoomChanged(double zoom);
    void selectionChanged(bool hasSelection);
    void findMatchChanged(int index, int total);
    void newCommentRequested();
    void commentActivated(const QString& id);

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    struct PageGeom {
        int    width  = 0;   // device px at the current zoom
        int    height = 0;
        int    top    = 0;   // y of the page's top edge in document coordinates
        double ptWidth = 0.0;   // page size in points, for text mapping
        double ptHeight = 0.0;
    };

    void relayout();
    void updateScrollBars();
    int  pageAtY(int y) const;
    int  viewportWidth() const;
    int  viewportHeight() const;
    // Vertical offset of a page's top edge. In continuous mode this is its
    // place in the strip; in single-page mode every page sits at the top.
    int  pageTop(int pageIndex) const;
    // Read every page's size from PDFium once. Called when a document is bound.
    void loadPageSizes();
    struct CachedPage {
        QImage image;
        QRect region;       // physical pixels within the full page
        QSize fullSize;
    };
    const CachedPage* imageFor(int pageIndex);
    void trimCache(int protectedPage);
    void invalidateAll();
    void syncCurrentPage();

    // Horizontal offset of a page's left edge, in document coordinates.
    int pageLeft(int pageIndex) const;
    // Map a viewport point to a text position (nearest position when the point
    // falls between lines).
    TextPos hitTest(const QPoint& viewportPos);
    // Viewport rectangle of one character, or a null rect when unavailable.
    QRectF  charRect(int pageIndex, int lineIndex, int pos);
    // Same, in document coordinates (independent of the scroll position).
    QRectF  charRectDoc(int pageIndex, int lineIndex, int pos);
    // Document-coordinate rectangle covering a whole match.
    QRectF  matchRectDoc(const pre::FindMatch& m);
    void    setSelection(const TextPos& anchor, const TextPos& caret);
    // Normalised selection bounds (start <= end).
    void    selectionBounds(TextPos* start, TextPos* end) const;
    QString commentAt(const QPoint& position) const;
    QRectF commentMarker(int page, const pre::MarkupRect& rect) const;
    void showSelectionCommentButton(const QPoint& position);

    QVector<app::Comment> comments_;
    QString activeComment_;
    QToolButton* selectionCommentButton_ = nullptr;

    FPDF_DOCUMENT doc_ = nullptr;
    int           pageCount_ = 0;
    double        zoom_ = 1.0;
    int           currentPage_ = 0;

    std::vector<PageGeom> geom_;
    // Page sizes in points, filled once per document.
    //
    // These used to be fetched from PDFium on every call, and relayout() asks
    // for every page -- so each zoom step or window resize cost two full passes
    // of FPDF_GetPageSizeByIndex over the document (fitWidth scans once, then
    // relayout scans again). A page's size never changes, so it is read once.
    std::vector<QSizeF> pageSizes_;
    int totalHeight_ = 0;
    int margin_ = 12;        // gap between pages and around the strip

    // Bounded by bytes as well as count. Large pages cache the visible region.
    std::map<int, CachedPage> cache_;
    double cacheDpr_ = 0;
    pre::RenderedPage     buffer_;   // scratch for the renderer
    FitMode               fitMode_ = FitMode::Width;
    ViewMode              viewMode_ = ViewMode::Continuous;
    bool                  invert_ = false;

    // Middle-button drag panning.
    bool   panning_ = false;
    QPoint panOrigin_;
    int    panStartX_ = 0;
    int    panStartY_ = 0;

    // While a fit-mode resize is in progress the cached rasters are stale but
    // still usable: they are drawn scaled into the new page rectangles, and one
    // deferred timer re-renders them crisply once the resize settles. Without
    // the deferral, every resize event dropped the cache and re-rendered the
    // visible pages, which made dragging a window edge visibly stutter.
    bool    deferRerender_ = false;
    QTimer* rerenderTimer_ = nullptr;
    QWidget* welcome_ = nullptr;

    pre::PageIndex* index_ = nullptr;   // text data; not owned
    TextPos         anchor_;
    TextPos         caret_;
    bool            hasSelection_ = false;
    bool            dragging_ = false;

    pre::FindResults find_;
    int              currentMatch_ = -1;   // index into find_.matches
};
