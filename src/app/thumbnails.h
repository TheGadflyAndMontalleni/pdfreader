// Page-thumbnail strip: a vertical list of small page previews.
//
// Rendering every page of a long document up front would freeze the window, so
// thumbnails are produced in small batches from a zero-interval timer: the
// event loop stays responsive and the strip fills in as the user looks at it.
#pragma once

#include <QImage>
#include <QListWidget>

#include <vector>

#include "render.h"

namespace pre {
class PageIndex;
}

class ThumbnailStrip : public QListWidget {
    Q_OBJECT

public:
    explicit ThumbnailStrip(QWidget* parent = nullptr);

    // The strip never owns the document; it must outlive the strip.
    void setDocument(FPDF_DOCUMENT doc, pre::PageIndex* index, int pageCount);
    void clearDocument();

    int  pageCount() const { return pageCount_; }
    int  generatedCount() const { return generated_; }
    bool isComplete() const { return pageCount_ > 0 && generated_ >= pageCount_; }

    // Render everything right now, bypassing the batching. Used by tests and
    // by anything that needs the finished strip synchronously.
    void generateAllNow();

    // The preview for one page, or a null image when it has not been made yet.
    QImage thumbnail(int pageIndex) const;

    // Behave as if the user clicked a preview. Goes through the same signal the
    // click handler uses, so tests exercise the real path rather than a copy.
    void activatePage(int pageIndex);

public slots:
    void setCurrentPage(int pageIndex);

signals:
    void pageRequested(int pageIndex);

private slots:
    void generateBatch();

private:
    void generateOne(int pageIndex);

    FPDF_DOCUMENT  doc_ = nullptr;
    pre::PageIndex* index_ = nullptr;
    int            pageCount_ = 0;
    int            generated_ = 0;
    int            thumbWidth_ = 132;
    QTimer*        timer_ = nullptr;
    pre::RenderedPage buffer_;
};
