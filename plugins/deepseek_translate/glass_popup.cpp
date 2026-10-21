#include "glass_popup.h"
#include <QApplication>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <algorithm>

namespace translate {
QRect PlacePopup(const QRect& selection, const QSize& preferred, const QRect& available, const QSize& minimum) {
    if (selection.isEmpty() || available.isEmpty()) return {};
    const QRect safe = available.adjusted(8, 8, -8, -8);
    const QRect avoid = selection.adjusted(-8, -8, 8, 8);
    const QSize size(std::min(preferred.width(), safe.width()), std::min(preferred.height(), safe.height()));
    if (size.width() < minimum.width() || size.height() < minimum.height()) return {};
    const auto fit = [&](QPoint position, QSize dimensions) {
        position.setX(std::clamp(position.x(), safe.left(), safe.right() - dimensions.width() + 1));
        position.setY(std::clamp(position.y(), safe.top(), safe.bottom() - dimensions.height() + 1));
        return QRect(position, dimensions);
    };
    const QPoint candidates[]{
        {avoid.right() + 1, avoid.bottom() + 1},
        {avoid.right() + 1, avoid.top() - size.height()},
        {avoid.right() + 1, avoid.top()},
        {avoid.left() - size.width(), avoid.top()},
        {avoid.left(), avoid.bottom() + 1},
        {avoid.left(), avoid.top() - size.height()}
    };
    for (const auto& position : candidates) {
        const auto rectangle = fit(position, size);
        if (!rectangle.intersects(avoid)) return rectangle;
    }
    // A long selection may leave only a thin strip. Keep a scrollable card
    // readable there, or decline to cover the source when no strip is usable.
    const QRect strips[]{
        QRect(safe.left(), avoid.bottom() + 1, safe.width(), safe.bottom() - avoid.bottom()).intersected(safe),
        QRect(safe.left(), safe.top(), safe.width(), avoid.top() - safe.top()).intersected(safe),
        QRect(avoid.right() + 1, safe.top(), safe.right() - avoid.right(), safe.height()).intersected(safe),
        QRect(safe.left(), safe.top(), avoid.left() - safe.left(), safe.height()).intersected(safe)
    };
    for (const auto& strip : strips) {
        if (strip.width() < minimum.width() || strip.height() < minimum.height()) continue;
        const QSize smaller(std::min(size.width(), strip.width()), std::min(size.height(), strip.height()));
        const auto rectangle = fit(strip.topLeft(), smaller);
        if (!rectangle.intersects(avoid)) return rectangle;
    }
    return {};
}
GlassPopup::GlassPopup(QWidget* reader)
    : QWidget(reader, Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint), reader_(reader) {
    setObjectName("translationGlassPopup");
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    if (reader->testAttribute(Qt::WA_DontShowOnScreen)) setAttribute(Qt::WA_DontShowOnScreen);
    resize(374, 420);
    setStyleSheet(R"(
        QWidget { color: #243447; font-family: "Microsoft YaHei UI", "Segoe UI"; }
        QLabel, QPlainTextEdit, QCheckBox { background: transparent; }
        QLabel#translationLanguage, QLabel#translationPhonetic, QLabel#translationNote { color: #68768a; font-size: 11px; }
        QLabel#translationWord { color: #152b46; font-size: 19px; font-weight: 600; }
        QLabel#translationStatus { color: #82643f; font-size: 11px; }
        QPlainTextEdit { border: none; font-size: 13px; selection-background-color: #c5dcf3; }
        QPushButton { border: none; border-radius: 7px; background: rgba(255,255,255,135); padding: 5px 9px; }
        QPushButton:hover { background: rgba(209,229,249,210); }
        QPushButton:disabled { color: #9ca8b5; }
        QComboBox { background: rgba(255,255,255,125); border: 1px solid rgba(130,150,175,45); border-radius: 7px; padding: 4px 8px; }
        QComboBox QAbstractItemView { background: #f5f8fc; color: #243447; selection-background-color: #d9e8f8; }
        QScrollBar:vertical { background: transparent; width: 5px; margin: 1px; }
        QScrollBar::handle:vertical { background: #adbed1; border-radius: 2px; min-height: 22px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
    )");
}
bool GlassPopup::present(const QRect& selection) {
    auto* screen = QApplication::screenAt(selection.center());
    if (!screen) screen = QApplication::primaryScreen();
    const auto minimum = minimumSizeHint().expandedTo(QSize(280, 280));
    const auto placement = screen ? PlacePopup(selection, QSize(374, 420).expandedTo(minimum), screen->availableGeometry(), minimum) : QRect();
    if (placement.isEmpty()) { hide(); return false; }
    const bool changed = !isVisible() || geometry() != placement;
    setGeometry(placement);
    if (changed) refreshBackdrop();
    if (!isVisible()) { show(); raise(); }
    return true;
}
void GlassPopup::refreshBackdrop() {
    if (!reader_) return;
    const auto pixmap = reader_->grab();
    if (pixmap.isNull()) return;
    const qreal ratio = pixmap.devicePixelRatio();
    const auto origin = reader_->mapFromGlobal(pos());
    const QRect physical(qRound(origin.x() * ratio), qRound(origin.y() * ratio),
                         qRound(width() * ratio), qRound(height() * ratio));
    QImage sample(physical.size(), QImage::Format_ARGB32_Premultiplied);
    sample.fill(QColor("#edf2f8"));
    QPainter painter(&sample);
    auto source = pixmap.toImage(); source.setDevicePixelRatio(1);
    painter.drawImage(-physical.topLeft(), source);
    painter.end();
    // A downsampled reconstruction blurs only the reader's own backing pixels;
    // it requires neither desktop capture nor an undocumented Windows API.
    backdrop_ = sample.scaled(std::max(1, width() / 14), std::max(1, height() / 14),
        Qt::IgnoreAspectRatio, Qt::SmoothTransformation).scaled(size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    update();
}
void GlassPopup::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF card = QRectF(rect()).adjusted(10, 9, -10, -12);
    for (int step = 9; step > 0; --step) {
        painter.setPen(QPen(QColor(35, 55, 80, 2 + (9 - step)), step * 1.3));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(card.translated(0, 2), 16, 16);
    }
    QPainterPath clip; clip.addRoundedRect(card, 16, 16);
    painter.setClipPath(clip);
    if (!backdrop_.isNull()) painter.drawImage(rect(), backdrop_);
    painter.fillRect(card, QColor(245, 249, 254, 218));
    QLinearGradient sheen(card.topLeft(), card.bottomRight());
    sheen.setColorAt(0, QColor(255, 255, 255, 72)); sheen.setColorAt(1, QColor(219, 233, 249, 30));
    painter.fillRect(card, sheen);
    painter.setClipping(false);
    painter.setPen(QPen(QColor(255, 255, 255, 225), 1)); painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(card, 16, 16);
}
void GlassPopup::closeEvent(QCloseEvent* event) { emit dismissed(); QWidget::closeEvent(event); }
void GlassPopup::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) { close(); event->accept(); return; }
    QWidget::keyPressEvent(event);
}
}
