#include "pdfview.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

#include "toc.h"

namespace {

// How many rendered pages to keep. Enough that scrolling a screenful up or
// down never re-renders, small enough that memory stays bounded even at high
// zoom (a page at 400% is tens of megabytes).
constexpr int kMaxCachedPages = 10;

constexpr double kMinZoom = 0.10;
constexpr double kMaxZoom = 8.0;

}  // namespace

PdfView::PdfView(QWidget* parent) : QAbstractScrollArea(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0x3a, 0x3a, 0x3a));
    setPalette(pal);
    viewport()->setAutoFillBackground(false);
    verticalScrollBar()->setSingleStep(40);
    horizontalScrollBar()->setSingleStep(40);

    // One deferred re-render after a burst of resize events.
    rerenderTimer_ = new QTimer(this);
    rerenderTimer_->setSingleShot(true);
    rerenderTimer_->setInterval(140);
    connect(rerenderTimer_, &QTimer::timeout, this, [this] {
        if (!deferRerender_) {
            invalidateAll();
            viewport()->update();
        }
    });
}

// ---------------------------------------------------------------------------
// document lifetime
// ---------------------------------------------------------------------------

void PdfView::setDocument(FPDF_DOCUMENT doc, int pageCount, pre::PageIndex* index) {
    doc_ = doc;
    pageCount_ = pageCount;
    index_ = index;
    currentPage_ = 0;
    clearSelection();
    clearFindResults();
    invalidateAll();
    loadPageSizes();
    relayout();
    verticalScrollBar()->setValue(0);
    horizontalScrollBar()->setValue(0);
    viewport()->update();
    emit pageChanged(currentPage_);
}

void PdfView::clearDocument() {
    doc_ = nullptr;
    index_ = nullptr;
    pageCount_ = 0;
    currentPage_ = 0;
    pageSizes_.clear();
    clearSelection();
    invalidateAll();
    geom_.clear();
    totalHeight_ = 0;
    updateScrollBars();
    viewport()->update();
}

void PdfView::invalidateAll() {
    cache_.clear();
    buffer_ = pre::RenderedPage();
}

QSizeF PdfView::pageSizePoints(int pageIndex) const {
    if (pageIndex < 0 || pageIndex >= static_cast<int>(pageSizes_.size())) {
        return QSizeF(0, 0);
    }
    return pageSizes_[static_cast<size_t>(pageIndex)];
}

void PdfView::loadPageSizes() {
    pageSizes_.clear();
    pageSizes_.reserve(static_cast<size_t>(std::max(0, pageCount_)));
    for (int i = 0; i < pageCount_; ++i) {
        double w = 0, h = 0;
        if (doc_) pre::PageSize(doc_, i, &w, &h);
        pageSizes_.emplace_back(w, h);
    }
}

// ---------------------------------------------------------------------------
// layout
// ---------------------------------------------------------------------------

int PdfView::viewportWidth() const { return viewport()->width(); }
int PdfView::viewportHeight() const { return viewport()->height(); }

int PdfView::pageTop(int pageIndex) const {
    // Single-page mode shows one page at the top; every page maps to margin_.
    if (viewMode_ == ViewMode::SinglePage) return margin_;
    if (pageIndex < 0 || pageIndex >= static_cast<int>(geom_.size())) return margin_;
    return geom_[static_cast<size_t>(pageIndex)].top;
}

int PdfView::contentHeight() const {
    if (viewMode_ == ViewMode::SinglePage) {
        if (currentPage_ < 0 || currentPage_ >= static_cast<int>(geom_.size())) return 0;
        return geom_[static_cast<size_t>(currentPage_)].height + 2 * margin_;
    }
    return totalHeight_ + margin_;
}

void PdfView::relayout() {
    geom_.clear();
    geom_.reserve(static_cast<size_t>(std::max(0, pageCount_)));

    int y = margin_;
    for (int i = 0; i < pageCount_; ++i) {
        const QSizeF pt = pageSizePoints(i);
        PageGeom g;
        g.ptWidth  = pt.width();
        g.ptHeight = pt.height();
        g.width  = std::max(1, static_cast<int>(std::lround(pt.width() * zoom_)));
        g.height = std::max(1, static_cast<int>(std::lround(pt.height() * zoom_)));
        g.top    = y;
        geom_.push_back(g);
        y += g.height + margin_;
    }
    totalHeight_ = y;
    updateScrollBars();
}

void PdfView::updateScrollBars() {
    const int contentH = contentHeight();
    int widest = 0;
    for (const auto& g : geom_) widest = std::max(widest, g.width);
    const int contentW = widest + 2 * margin_;

    verticalScrollBar()->setRange(0, std::max(0, contentH - viewportHeight()));
    verticalScrollBar()->setPageStep(std::max(1, viewportHeight()));
    horizontalScrollBar()->setRange(0, std::max(0, contentW - viewportWidth()));
    horizontalScrollBar()->setPageStep(std::max(1, viewportWidth()));
}

int PdfView::pageAtY(int y) const {
    if (geom_.empty()) return 0;
    // In single-page mode the visible page is the current one by definition.
    if (viewMode_ == ViewMode::SinglePage) return currentPage_;
    // Pages are laid out in increasing `top` order, so binary search.
    int lo = 0, hi = static_cast<int>(geom_.size()) - 1, best = 0;
    while (lo <= hi) {
        const int mid = lo + (hi - lo) / 2;
        if (geom_[static_cast<size_t>(mid)].top <= y) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return best;
}

void PdfView::syncCurrentPage() {
    if (geom_.empty()) return;
    if (viewMode_ == ViewMode::SinglePage) return;   // set by setPage()
    const int p = pageAtY(verticalScrollBar()->value() + margin_ / 2);
    if (p != currentPage_) {
        currentPage_ = p;
        emit pageChanged(currentPage_);
    }
}

// ---------------------------------------------------------------------------
// rendering
// ---------------------------------------------------------------------------

const QImage* PdfView::imageFor(int pageIndex) {
    auto it = cache_.find(pageIndex);
    if (it != cache_.end()) return &it->second;
    if (!doc_ || pageIndex < 0 || pageIndex >= pageCount_) return nullptr;

    if (!pre::RenderPage(doc_, pageIndex, zoom_, &buffer_)) return nullptr;

    // PDFium writes BGRx, whose byte order matches Format_RGB32 on
    // little-endian hosts. RGB32 has no alpha channel, so drawImage() takes the
    // opaque fast path instead of blending every pixel. copy() detaches from
    // the scratch buffer.
    QImage img = QImage(buffer_.pixels.data(), buffer_.width, buffer_.height,
                        buffer_.stride, QImage::Format_RGB32)
                     .copy();
    if (invert_) img.invertPixels(QImage::InvertRgb);
    auto inserted = cache_.emplace(pageIndex, std::move(img));
    trimCache();
    return &inserted.first->second;
}

void PdfView::setInvertColors(bool on) {
    if (on == invert_) return;
    invert_ = on;
    // The cached rasters carry the old polarity, so they all have to go.
    invalidateAll();
    viewport()->update();
}

void PdfView::trimCache() {
    while (static_cast<int>(cache_.size()) > kMaxCachedPages) {
        // Evict the cached page furthest from the one being read.
        auto worst = cache_.begin();
        int  worstDist = -1;
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            const int dist = std::abs(it->first - currentPage_);
            if (dist > worstDist) {
                worstDist = dist;
                worst = it;
            }
        }
        if (worst == cache_.end()) break;
        cache_.erase(worst);
    }
}

// ---------------------------------------------------------------------------
// painting
// ---------------------------------------------------------------------------

void PdfView::paintEvent(QPaintEvent*) {
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), QColor(0x3a, 0x3a, 0x3a));

    if (!doc_ || geom_.empty()) {
        painter.setPen(QColor(0xcc, 0xcc, 0xcc));
        painter.drawText(viewport()->rect(), Qt::AlignCenter,
                         doc_ ? tr("正在渲染…") : tr("未打开文档"));
        return;
    }

    const int top    = verticalScrollBar()->value();
    const int left   = horizontalScrollBar()->value();
    const int bottom = top + viewportHeight();

    const int firstPage = (viewMode_ == ViewMode::SinglePage) ? currentPage_
                                                              : pageAtY(top);
    for (int i = firstPage; i < pageCount_; ++i) {
        if (viewMode_ == ViewMode::SinglePage && i != currentPage_) break;
        const PageGeom& g = geom_[static_cast<size_t>(i)];
        const int ptop = pageTop(i);
        if (ptop > bottom) break;                      // below the viewport
        if (ptop + g.height < top) continue;           // above the viewport

        const QImage* img = imageFor(i);
        const int x = margin_ + std::max(0, (viewportWidth() - 2 * margin_ - g.width) / 2);
        if (img && !img->isNull()) {
            // Drawing into the target rectangle rather than at 1:1 means a
            // raster rendered for an older zoom still lands correctly while a
            // resize is in progress. When the sizes match this is still a plain
            // blit.
            painter.drawImage(QRect(x - left, ptop - top, g.width, g.height), *img);
        } else {
            painter.fillRect(QRect(x - left, ptop - top, g.width, g.height),
                             QColor(0xff, 0xff, 0xff));
        }
        painter.setPen(QColor(0x60, 0x60, 0x60));
        painter.drawRect(QRect(x - left, ptop - top, g.width, g.height));
    }

    // Find highlights, under the selection so a selected match stays readable.
    if (!find_.matches.empty()) {
        const QRect clip = viewport()->rect();
        painter.setPen(Qt::NoPen);
        const int top2 = verticalScrollBar()->value();
        const int bot2 = top2 + viewportHeight();
        for (size_t i = 0; i < find_.matches.size(); ++i) {
            const pre::FindMatch& m = find_.matches[i];
            const int pageTop = (m.page >= 0 && m.page < static_cast<int>(geom_.size()))
                                    ? geom_[static_cast<size_t>(m.page)].top
                                    : 0;
            if (pageTop > bot2) break;        // matches are in document order
            if (pageTop + 4000 < top2) continue;
            const QRectF r = matchRectDoc(m);
            if (r.isNull()) continue;
            const QRectF v = r.translated(-horizontalScrollBar()->value(), -top2);
            if (!v.intersects(clip)) continue;
            const bool current = (static_cast<int>(i) == currentMatch_);
            painter.setBrush(current ? QColor(0xff, 0x96, 0x32, 0xcc)    // current: orange
                                     : QColor(0xff, 0xe0, 0x40, 0x99));  // others: yellow
            painter.drawRect(v);
        }
        painter.setBrush(Qt::NoBrush);
    }

    // Selection highlight, on top of the pages.
    if (hasSelection_ && index_) {
        TextPos s, e;
        selectionBounds(&s, &e);
        const QRect clip = viewport()->rect();
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0x33, 0x99, 0xff, 0x70));
        for (int p = s.page; p <= e.page && p < pageCount_; ++p) {
            const auto& lines = index_->Lines(p);
            const int firstLine = (p == s.page) ? s.line : 0;
            const int lastLine  = (p == e.page) ? e.line
                                                : static_cast<int>(lines.size()) - 1;
            for (int li = std::max(0, firstLine);
                 li <= lastLine && li < static_cast<int>(lines.size()); ++li) {
                const auto& L = lines[static_cast<size_t>(li)];
                const int from = (p == s.page && li == s.line) ? s.pos : 0;
                const int to   = (p == e.page && li == e.line)
                                     ? e.pos
                                     : static_cast<int>(L.charIndices.size());
                for (int k = std::max(0, from);
                     k < to && k < static_cast<int>(L.charIndices.size()); ++k) {
                    const QRectF r = charRect(p, li, k);
                    if (r.isNull() || !r.intersects(clip)) continue;
                    painter.drawRect(r);
                }
            }
        }
        painter.setBrush(Qt::NoBrush);
    }
}

// ---------------------------------------------------------------------------
// navigation
// ---------------------------------------------------------------------------

void PdfView::setPage(int index) {
    if (geom_.empty()) return;
    index = std::max(0, std::min(index, pageCount_ - 1));

    if (viewMode_ == ViewMode::SinglePage) {
        currentPage_ = index;
        verticalScrollBar()->setValue(0);
        viewport()->update();
        emit pageChanged(currentPage_);
        return;
    }

    const int y = pageTop(index) - margin_;
    verticalScrollBar()->setValue(std::max(0, y));
    currentPage_ = index;
    viewport()->update();
    emit pageChanged(currentPage_);
}

void PdfView::goToNextPage() {
    if (currentPage_ + 1 < pageCount_) setPage(currentPage_ + 1);
}

void PdfView::goToPrevPage() {
    if (currentPage_ > 0) setPage(currentPage_ - 1);
}

void PdfView::goToFirstPage() { setPage(0); }

void PdfView::goToLastPage() { setPage(pageCount_ - 1); }

void PdfView::setZoom(double zoom) {
    zoom = std::max(kMinZoom, std::min(zoom, kMaxZoom));
    if (std::fabs(zoom - zoom_) < 1e-6) return;

    // Keep the reader's place: remember where the current page starts relative
    // to the viewport, then restore that offset after relayout. Single-page
    // mode has nothing to preserve -- the page is pinned to the top.
    const int anchorPage = currentPage_;
    int anchorOffset = 0;
    if (viewMode_ == ViewMode::Continuous && !geom_.empty() &&
        anchorPage < static_cast<int>(geom_.size())) {
        anchorOffset = verticalScrollBar()->value() - pageTop(anchorPage);
    }

    zoom_ = zoom;
    // A fit-driven resize keeps the old rasters for the moment; they are drawn
    // scaled until the deferred timer replaces them.
    if (!deferRerender_) invalidateAll();
    relayout();

    if (viewMode_ == ViewMode::Continuous && !geom_.empty()) {
        const int y = pageTop(anchorPage) + anchorOffset;
        verticalScrollBar()->setValue(std::max(0, y));
    }
    viewport()->update();
    emit zoomChanged(zoom_);
}

void PdfView::zoomIn()  { fitMode_ = FitMode::None; setZoom(zoom_ * 1.25); }
void PdfView::zoomOut() { fitMode_ = FitMode::None; setZoom(zoom_ / 1.25); }

void PdfView::fitWidth() {
    fitMode_ = FitMode::Width;
    if (!doc_ || pageCount_ <= 0) return;
    // Fit the widest page so no page ever needs horizontal scrolling.
    double widest = 0;
    for (int i = 0; i < pageCount_; ++i) {
        widest = std::max(widest, pageSizePoints(i).width());
    }
    if (widest <= 0) return;
    const int avail = viewportWidth() - 2 * margin_;
    if (avail <= 0) return;
    setZoom(static_cast<double>(avail) / widest);
    horizontalScrollBar()->setValue(0);
}

void PdfView::fitPage() {
    fitMode_ = FitMode::Page;
    if (!doc_ || pageCount_ <= 0) return;
    // Fit the whole page: both dimensions must be satisfied, so the smaller of
    // the two scales wins. Uses the largest page so nothing is ever cropped.
    double widest = 0, tallest = 0;
    for (int i = 0; i < pageCount_; ++i) {
        const QSizeF s = pageSizePoints(i);
        widest = std::max(widest, s.width());
        tallest = std::max(tallest, s.height());
    }
    if (widest <= 0 || tallest <= 0) return;
    const int availW = viewportWidth() - 2 * margin_;
    const int availH = viewportHeight() - 2 * margin_;
    if (availW <= 0 || availH <= 0) return;
    setZoom(std::min(availW / widest, availH / tallest));
    horizontalScrollBar()->setValue(0);
    verticalScrollBar()->setValue(0);
}

void PdfView::setFitMode(FitMode mode) {
    if (mode == FitMode::Width) {
        fitWidth();
    } else if (mode == FitMode::Page) {
        fitPage();
    } else {
        fitMode_ = FitMode::None;
    }
}

void PdfView::setViewMode(ViewMode mode) {
    if (mode == viewMode_) return;
    viewMode_ = mode;
    if (viewMode_ == ViewMode::SinglePage) {
        verticalScrollBar()->setValue(0);
        horizontalScrollBar()->setValue(0);
    } else {
        // Re-anchor on the page being read so switching back does not jump.
        verticalScrollBar()->setValue(std::max(0, pageTop(currentPage_) - margin_));
    }
    updateScrollBars();
    viewport()->update();
}

void PdfView::setFitWidthMode(bool on) {
    setFitMode(on ? FitMode::Width : FitMode::None);
}

// ---------------------------------------------------------------------------
// text selection
//
// Characters arrive in PDF page space (origin bottom-left, y up); the view is a
// continuous strip in device pixels with y down, so every mapping flips y and
// scales by the zoom, then offsets by the page's place in the strip.
// ---------------------------------------------------------------------------

int PdfView::pageLeft(int pageIndex) const {
    if (pageIndex < 0 || pageIndex >= static_cast<int>(geom_.size())) return margin_;
    const PageGeom& g = geom_[static_cast<size_t>(pageIndex)];
    return margin_ + std::max(0, (viewportWidth() - 2 * margin_ - g.width) / 2);
}

TextPos PdfView::hitTest(const QPoint& vp) {
    TextPos none;
    if (!index_ || geom_.empty()) return none;

    const int docY = vp.y() + verticalScrollBar()->value();
    const int docX = vp.x() + horizontalScrollBar()->value();
    const int page = pageAtY(docY);
    if (page < 0 || page >= pageCount_) return none;

    const PageGeom& g = geom_[static_cast<size_t>(page)];
    if (g.ptHeight <= 0 || zoom_ <= 0) return none;

    const double xPt = (docX - pageLeft(page)) / zoom_;
    const double yPt = g.ptHeight - (docY - g.top) / zoom_;

    const auto& lines = index_->Lines(page);
    if (lines.empty()) return none;

    // Prefer the line whose vertical span contains the point; in the gap
    // between lines take the closest one so a drag stays continuous.
    int    best     = 0;
    double bestDist = std::numeric_limits<double>::max();
    for (size_t i = 0; i < lines.size(); ++i) {
        const auto& L = lines[i];
        if (yPt >= L.bottom && yPt <= L.top) {
            best = static_cast<int>(i);
            bestDist = 0;
            break;
        }
        const double d = std::min(std::fabs(yPt - L.top), std::fabs(yPt - L.bottom));
        if (d < bestDist) {
            bestDist = d;
            best = static_cast<int>(i);
        }
    }

    const auto& chars = index_->Chars(page);
    const auto& L = lines[static_cast<size_t>(best)];
    int pos = static_cast<int>(L.charIndices.size());
    for (size_t k = 0; k < L.charIndices.size(); ++k) {
        const int ci = L.charIndices[k];
        if (ci < 0 || ci >= static_cast<int>(chars.size())) continue;
        const auto& c = chars[static_cast<size_t>(ci)];
        if (xPt < (c.left + c.right) / 2.0) {
            pos = static_cast<int>(k);
            break;
        }
    }
    return TextPos{page, best, pos};
}

QRectF PdfView::charRectDoc(int pageIndex, int lineIndex, int pos) {
    if (!index_ || pageIndex < 0 || pageIndex >= static_cast<int>(geom_.size())) {
        return QRectF();
    }
    const auto& lines = index_->Lines(pageIndex);
    if (lineIndex < 0 || lineIndex >= static_cast<int>(lines.size())) return QRectF();
    const auto& L = lines[static_cast<size_t>(lineIndex)];
    if (pos < 0 || pos >= static_cast<int>(L.charIndices.size())) return QRectF();

    const auto& chars = index_->Chars(pageIndex);
    const int ci = L.charIndices[static_cast<size_t>(pos)];
    if (ci < 0 || ci >= static_cast<int>(chars.size())) return QRectF();
    const auto& c = chars[static_cast<size_t>(ci)];
    const PageGeom& g = geom_[static_cast<size_t>(pageIndex)];

    const double x  = pageLeft(pageIndex) + c.left * zoom_;
    const double w  = std::max(1.0, (c.right - c.left) * zoom_);
    const double yT = pageTop(pageIndex) + (g.ptHeight - c.top) * zoom_;
    const double h  = std::max(1.0, (c.top - c.bottom) * zoom_);
    return QRectF(x, yT, w, h);
}

QRectF PdfView::charRect(int pageIndex, int lineIndex, int pos) {
    const QRectF r = charRectDoc(pageIndex, lineIndex, pos);
    if (r.isNull()) return r;
    return r.translated(-horizontalScrollBar()->value(),
                        -verticalScrollBar()->value());
}

QRectF PdfView::matchRectDoc(const pre::FindMatch& m) {
    if (m.length <= 0) return QRectF();
    QRectF r = charRectDoc(m.page, m.line, m.start);
    if (r.isNull()) return r;
    // Union across the matched characters so the highlight is one block even
    // when the glyph boxes do not quite touch.
    for (int k = 1; k < m.length; ++k) {
        const QRectF c = charRectDoc(m.page, m.line, m.start + k);
        if (!c.isNull()) r = r.united(c);
    }
    return r;
}

void PdfView::selectionBounds(TextPos* start, TextPos* end) const {
    if (caret_ < anchor_) {
        *start = caret_;
        *end   = anchor_;
    } else {
        *start = anchor_;
        *end   = caret_;
    }
}

void PdfView::setSelection(const TextPos& anchor, const TextPos& caret) {
    anchor_ = anchor;
    caret_  = caret;
    const bool had = hasSelection_;
    hasSelection_ = anchor.valid() && caret.valid() && !(anchor == caret);
    viewport()->update();
    if (had != hasSelection_) emit selectionChanged(hasSelection_);
}

std::map<int, std::vector<pre::MarkupRect>> PdfView::selectionRectsByPage() {
    std::map<int, std::vector<pre::MarkupRect>> out;
    if (!hasSelection_ || !index_) return out;

    TextPos s, e;
    selectionBounds(&s, &e);

    for (int p = s.page; p <= e.page && p < pageCount_; ++p) {
        const auto& lines = index_->Lines(p);
        const auto& chars = index_->Chars(p);
        const int firstLine = (p == s.page) ? s.line : 0;
        const int lastLine  = (p == e.page) ? e.line
                                            : static_cast<int>(lines.size()) - 1;

        for (int li = std::max(0, firstLine);
             li <= lastLine && li < static_cast<int>(lines.size()); ++li) {
            const auto& L = lines[static_cast<size_t>(li)];
            const int from = (p == s.page && li == s.line) ? s.pos : 0;
            const int to   = (p == e.page && li == e.line)
                                 ? e.pos
                                 : static_cast<int>(L.charIndices.size());

            pre::MarkupRect r;
            bool any = false;
            for (int k = std::max(0, from);
                 k < to && k < static_cast<int>(L.charIndices.size()); ++k) {
                const int ci = L.charIndices[static_cast<size_t>(k)];
                if (ci < 0 || ci >= static_cast<int>(chars.size())) continue;
                const auto& c = chars[static_cast<size_t>(ci)];
                if (!any) {
                    r.left = c.left; r.right = c.right;
                    r.bottom = c.bottom; r.top = c.top;
                    any = true;
                } else {
                    r.left   = std::min(r.left, c.left);
                    r.right  = std::max(r.right, c.right);
                    r.bottom = std::min(r.bottom, c.bottom);
                    r.top    = std::max(r.top, c.top);
                }
            }
            if (any) out[p].push_back(r);
        }
    }
    return out;
}

void PdfView::invalidateRenderedPages() {
    invalidateAll();
    viewport()->update();
}

void PdfView::clearSelection() {
    anchor_ = TextPos();
    caret_  = TextPos();
    if (hasSelection_) {
        hasSelection_ = false;
        emit selectionChanged(false);
    }
    viewport()->update();
}

bool PdfView::selectLine(int pageIndex, int lineIndex) {
    if (!index_ || pageIndex < 0 || pageIndex >= pageCount_) return false;
    const auto& lines = index_->Lines(pageIndex);
    if (lineIndex < 0 || lineIndex >= static_cast<int>(lines.size())) return false;
    const int n = static_cast<int>(lines[static_cast<size_t>(lineIndex)].charIndices.size());
    if (n <= 0) return false;
    setSelection(TextPos{pageIndex, lineIndex, 0}, TextPos{pageIndex, lineIndex, n});
    return true;
}

void PdfView::selectAll() {
    if (!index_ || pageCount_ <= 0) return;
    const int lastPage = pageCount_ - 1;
    const auto& lines = index_->Lines(lastPage);
    if (lines.empty()) return;
    const int lastLine = static_cast<int>(lines.size()) - 1;
    const int n = static_cast<int>(lines[static_cast<size_t>(lastLine)].charIndices.size());
    setSelection(TextPos{0, 0, 0}, TextPos{lastPage, lastLine, n});
}

QString PdfView::selectedText() {
    if (!hasSelection_ || !index_) return QString();

    TextPos s, e;
    selectionBounds(&s, &e);

    std::string out;
    for (int p = s.page; p <= e.page && p < pageCount_; ++p) {
        const auto& lines = index_->Lines(p);
        const auto& chars = index_->Chars(p);
        const int firstLine = (p == s.page) ? s.line : 0;
        const int lastLine  = (p == e.page) ? e.line : static_cast<int>(lines.size()) - 1;

        for (int li = std::max(0, firstLine);
             li <= lastLine && li < static_cast<int>(lines.size()); ++li) {
            const auto& L = lines[static_cast<size_t>(li)];
            const int from = (p == s.page && li == s.line) ? s.pos : 0;
            const int to   = (p == e.page && li == e.line)
                                 ? e.pos
                                 : static_cast<int>(L.charIndices.size());
            if (!out.empty()) out += '\n';
            for (int k = std::max(0, from);
                 k < to && k < static_cast<int>(L.charIndices.size()); ++k) {
                const int ci = L.charIndices[static_cast<size_t>(k)];
                if (ci < 0 || ci >= static_cast<int>(chars.size())) continue;
                const uint32_t cp = chars[static_cast<size_t>(ci)].codepoint;
                // Match how line text is built: whitespace controls become a
                // space, other controls are dropped, everything else is UTF-8.
                if (cp == '\n' || cp == '\r' || cp == '\t') out += ' ';
                else if (cp >= 0x20) out += pre::Utf8Encode(cp);
            }
        }
    }
    return QString::fromUtf8(out.c_str());
}

void PdfView::copySelectionToClipboard() {
    const QString text = selectedText();
    if (!text.isEmpty()) QApplication::clipboard()->setText(text);
}

QPoint PdfView::posToViewport(const TextPos& p) {
    const QRectF r = charRect(p.page, p.line, p.pos);
    return r.isNull() ? QPoint() : r.topLeft().toPoint();
}

QImage PdfView::grabViewport() {
    return viewport()->grab().toImage();
}

// ---------------------------------------------------------------------------
// find
// ---------------------------------------------------------------------------

void PdfView::setFindResults(const pre::FindResults& results) {
    find_ = results;
    currentMatch_ = find_.matches.empty() ? -1 : 0;
    if (currentMatch_ >= 0) {
        const int page = find_.matches[0].page;
        setPage(page);
        // Put the match a third of the way down rather than at the very top,
        // so the surrounding text is visible.
        const QRectF r = matchRectDoc(find_.matches[0]);
        if (!r.isNull()) {
            verticalScrollBar()->setValue(
                static_cast<int>(r.top() - viewportHeight() / 3.0));
        }
    }
    viewport()->update();
    emit findMatchChanged(currentMatch_, matchCount());
}

void PdfView::clearFindResults() {
    find_ = pre::FindResults();
    currentMatch_ = -1;
    viewport()->update();
    emit findMatchChanged(-1, 0);
}

bool PdfView::goToNextMatch() {
    if (find_.matches.empty()) return false;
    currentMatch_ = (currentMatch_ + 1) % static_cast<int>(find_.matches.size());
    const pre::FindMatch& m = find_.matches[static_cast<size_t>(currentMatch_)];
    setPage(m.page);
    const QRectF r = matchRectDoc(m);
    if (!r.isNull()) {
        verticalScrollBar()->setValue(static_cast<int>(r.top() - viewportHeight() / 3.0));
    }
    viewport()->update();
    emit findMatchChanged(currentMatch_, matchCount());
    return true;
}

bool PdfView::goToPrevMatch() {
    if (find_.matches.empty()) return false;
    const int n = static_cast<int>(find_.matches.size());
    currentMatch_ = (currentMatch_ - 1 + n) % n;
    const pre::FindMatch& m = find_.matches[static_cast<size_t>(currentMatch_)];
    setPage(m.page);
    const QRectF r = matchRectDoc(m);
    if (!r.isNull()) {
        verticalScrollBar()->setValue(static_cast<int>(r.top() - viewportHeight() / 3.0));
    }
    viewport()->update();
    emit findMatchChanged(currentMatch_, matchCount());
    return true;
}

bool PdfView::selectMatch(int index) {
    if (index < 0 || index >= static_cast<int>(find_.matches.size())) return false;
    const pre::FindMatch& m = find_.matches[static_cast<size_t>(index)];
    setSelection(TextPos{m.page, m.line, m.start},
                 TextPos{m.page, m.line, m.start + m.length});
    return true;
}

QString PdfView::matchText(int index) {
    if (index < 0 || index >= static_cast<int>(find_.matches.size())) return QString();
    return selectMatch(index) ? selectedText() : QString();
}



void PdfView::resizeEvent(QResizeEvent* event) {
    QAbstractScrollArea::resizeEvent(event);
    // The viewport is final here, unlike in the owning window's resizeEvent,
    // so this is the only place a fit zoom can be computed safely.
    if (doc_ && pageCount_ > 0 && fitMode_ != FitMode::None) {
        // Recompute the zoom and lay out now, but leave the cache alone; the
        // paint below scales the existing rasters into the new rectangles.
        deferRerender_ = true;
        if (fitMode_ == FitMode::Width) fitWidth();
        else                            fitPage();
        deferRerender_ = false;
        if (rerenderTimer_) rerenderTimer_->start();
    }
    updateScrollBars();
}

void PdfView::scrollContentsBy(int, int) {
    // A custom-painted viewport must be fully repainted: newly exposed areas
    // need pages rendered, so a blit would leave them blank.
    syncCurrentPage();
    viewport()->update();
}

void PdfView::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        setZoom(event->angleDelta().y() > 0 ? zoom_ * 1.15 : zoom_ / 1.15);
        event->accept();
        return;
    }

    int dy = event->angleDelta().y();
    if (dy == 0 && !event->pixelDelta().isNull()) dy = event->pixelDelta().y() * 2;
    if (dy != 0) {
        verticalScrollBar()->setValue(verticalScrollBar()->value() - dy);
        event->accept();
        return;
    }
    QAbstractScrollArea::wheelEvent(event);
}

void PdfView::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_PageDown:
        case Qt::Key_Space:
            verticalScrollBar()->setValue(verticalScrollBar()->value() +
                                          viewportHeight() * 9 / 10);
            event->accept();
            return;
        case Qt::Key_PageUp:
            verticalScrollBar()->setValue(verticalScrollBar()->value() -
                                          viewportHeight() * 9 / 10);
            event->accept();
            return;
        case Qt::Key_Down:
            verticalScrollBar()->setValue(verticalScrollBar()->value() + 60);
            event->accept();
            return;
        case Qt::Key_Up:
            verticalScrollBar()->setValue(verticalScrollBar()->value() - 60);
            event->accept();
            return;
        case Qt::Key_Home:
            if (event->modifiers() & Qt::ControlModifier) goToFirstPage();
            else verticalScrollBar()->setValue(0);
            event->accept();
            return;
        case Qt::Key_End:
            if (event->modifiers() & Qt::ControlModifier) goToLastPage();
            else verticalScrollBar()->setValue(verticalScrollBar()->maximum());
            event->accept();
            return;
        default:
            break;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

// ---------------------------------------------------------------------------
// mouse selection
// ---------------------------------------------------------------------------

void PdfView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        // Middle-drag pans, the way most PDF readers behave.
        panning_    = true;
        panOrigin_  = event->pos();
        panStartX_  = horizontalScrollBar()->value();
        panStartY_  = verticalScrollBar()->value();
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() != Qt::LeftButton || !index_) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }
    setFocus(Qt::MouseFocusReason);
    const TextPos p = hitTest(event->pos());
    if (!p.valid()) {
        clearSelection();
        return;
    }
    // A press starts a new selection anchored here.
    anchor_ = p;
    caret_  = p;
    if (hasSelection_) {
        hasSelection_ = false;
        emit selectionChanged(false);
    }
    dragging_ = true;
    viewport()->update();
    event->accept();
}

void PdfView::mouseMoveEvent(QMouseEvent* event) {
    if (panning_) {
        const QPoint d = event->pos() - panOrigin_;
        horizontalScrollBar()->setValue(panStartX_ - d.x());
        verticalScrollBar()->setValue(panStartY_ - d.y());
        event->accept();
        return;
    }
    if (!dragging_) {
        viewport()->setCursor(index_ ? Qt::IBeamCursor : Qt::ArrowCursor);
        QAbstractScrollArea::mouseMoveEvent(event);
        return;
    }
    const TextPos p = hitTest(event->pos());
    if (p.valid()) setSelection(anchor_, p);
    event->accept();
}

void PdfView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton && panning_) {
        panning_ = false;
        viewport()->setCursor(index_ ? Qt::IBeamCursor : Qt::ArrowCursor);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && dragging_) {
        dragging_ = false;
        event->accept();
        return;
    }
    QAbstractScrollArea::mouseReleaseEvent(event);
}

void PdfView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !index_) {
        QAbstractScrollArea::mouseDoubleClickEvent(event);
        return;
    }
    const TextPos p = hitTest(event->pos());
    if (p.valid()) selectLine(p.page, p.line);
    dragging_ = false;
    event->accept();
}
