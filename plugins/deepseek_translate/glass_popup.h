#pragma once
#include <QImage>
#include <QPointer>
#include <QWidget>

namespace translate {
// Empty means that the screen has no readable space outside the selection.
QRect PlacePopup(const QRect& selection, const QSize& preferred, const QRect& available,
                 const QSize& minimum = QSize(240, 140));
class GlassPopup : public QWidget {
    Q_OBJECT
public:
    explicit GlassPopup(QWidget* reader);
    bool present(const QRect& selection);
    void refreshBackdrop();
signals:
    void dismissed();
protected:
    void paintEvent(QPaintEvent*) override;
    void closeEvent(QCloseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
private:
    QPointer<QWidget> reader_;
    QImage backdrop_;
};
}
