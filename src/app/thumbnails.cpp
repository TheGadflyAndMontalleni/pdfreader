#include "thumbnails.h"

#include <QTimer>

#include <algorithm>
#include <cmath>

namespace {

// How many previews to build per event-loop turn. Small enough that scrolling
// and typing stay smooth while a long document fills in.
constexpr int kBatchSize = 4;

}  // namespace

ThumbnailStrip::ThumbnailStrip(QWidget* parent) : QListWidget(parent) {
    setViewMode(QListView::IconMode);
    setFlow(QListView::TopToBottom);
    setMovement(QListView::Static);
    setResizeMode(QListView::Adjust);
    setUniformItemSizes(true);
    setSpacing(4);
    setIconSize(QSize(thumbWidth_, static_cast<int>(thumbWidth_ * 1.45)));
    setGridSize(QSize(thumbWidth_ + 28, static_cast<int>(thumbWidth_ * 1.45) + 34));
    setWordWrap(false);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    timer_ = new QTimer(this);
    timer_->setInterval(0);
    connect(timer_, &QTimer::timeout, this, &ThumbnailStrip::generateBatch);

    connect(this, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (item) emit pageRequested(item->data(Qt::UserRole).toInt());
    });
}

void ThumbnailStrip::setDocument(FPDF_DOCUMENT doc, pre::PageIndex* index,
                                 int pageCount) {
    clearDocument();
    doc_       = doc;
    index_     = index;
    pageCount_ = pageCount;

    for (int i = 0; i < pageCount_; ++i) {
        auto* item = new QListWidgetItem(QString::number(i + 1));
        item->setData(Qt::UserRole, i);
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
        item->setSizeHint(QSize(thumbWidth_ + 24,
                                static_cast<int>(thumbWidth_ * 1.45) + 30));
        // A placeholder keeps every row the same height, so the scrollbar does
        // not jump around while the previews are still being produced.
        item->setIcon(QIcon(QPixmap(thumbWidth_, static_cast<int>(thumbWidth_ * 1.45))));
        addItem(item);
    }

    if (pageCount_ > 0) timer_->start();
}

void ThumbnailStrip::clearDocument() {
    timer_->stop();
    clear();
    doc_ = nullptr;
    index_ = nullptr;
    pageCount_ = 0;
    generated_ = 0;
    buffer_ = pre::RenderedPage();
}

void ThumbnailStrip::generateBatch() {
    if (!doc_ || generated_ >= pageCount_) {
        timer_->stop();
        return;
    }
    for (int n = 0; n < kBatchSize && generated_ < pageCount_; ++n) {
        generateOne(generated_);
        ++generated_;
    }
    if (generated_ >= pageCount_) timer_->stop();
}

void ThumbnailStrip::generateAllNow() {
    if (!doc_) return;
    timer_->stop();
    while (generated_ < pageCount_) {
        generateOne(generated_);
        ++generated_;
    }
}

void ThumbnailStrip::generateOne(int pageIndex) {
    double ptW = 0, ptH = 0;
    if (!pre::PageSize(doc_, pageIndex, &ptW, &ptH) || ptW <= 0 || ptH <= 0) return;

    const double scale = thumbWidth_ / ptW;
    if (!pre::RenderPage(doc_, pageIndex, scale, &buffer_)) return;

    const QImage img = QImage(buffer_.pixels.data(), buffer_.width, buffer_.height,
                              buffer_.stride, QImage::Format_ARGB32)
                           .copy();

    QListWidgetItem* item = this->item(pageIndex);
    if (!item) return;
    item->setIcon(QIcon(QPixmap::fromImage(img)));
    item->setData(Qt::UserRole + 1, img);
}

QImage ThumbnailStrip::thumbnail(int pageIndex) const {
    if (pageIndex < 0 || pageIndex >= pageCount_) return QImage();
    QListWidgetItem* item = this->item(pageIndex);
    if (!item) return QImage();
    return item->data(Qt::UserRole + 1).value<QImage>();
}

void ThumbnailStrip::activatePage(int pageIndex) {
    if (pageIndex < 0 || pageIndex >= pageCount_) return;
    emit pageRequested(pageIndex);
}

void ThumbnailStrip::setCurrentPage(int pageIndex) {
    if (pageIndex < 0 || pageIndex >= pageCount_) return;
    if (currentRow() == pageIndex) return;
    setCurrentRow(pageIndex, QItemSelectionModel::ClearAndSelect);
    scrollToItem(item(pageIndex), QAbstractItemView::EnsureVisible);
}
