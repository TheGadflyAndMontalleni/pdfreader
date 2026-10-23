#pragma once
#include <QWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>
#include <QHash>
#include <memory>
#include "session.h"

class EpubServer;
class EpubView : public QWidget {
    Q_OBJECT
public:
    explicit EpubView(QWidget* parent = nullptr);
    ~EpubView() override;
    bool open(const QString& path, QString* error);
    bool ready() const { return ready_; }
    QString error() const { return error_; }
    QString title() const { return title_; }
    int section() const { return section_; }
    int sectionCount() const { return sectionCount_; }
    double progress() const { return progress_; }
    QString locator() const { return locator_; }
    double fontScale() const { return fontScale_; }
    bool dark() const { return dark_; }
    bool scrolled() const { return scrolled_; }
    QJsonArray toc() const { return toc_; }
    QString selectedText() const { return selection_; }
    QRect selectionScreenRect() const;
    QString sectionText(int index) const;
    void goTo(const QString& target);
    void goToSection(int index);
    void next();
    void previous();
    void setFontScale(double scale);
    void setDark(bool dark);
    void setScrolled(bool scrolled);
    void selectAll();
    void clearSelection();
    void copySelection();
    void find(const QString& text, bool matchCase);
    void findNext(bool backwards = false);
    void saveState();
    // Also used by integration checks; execution stays in this view's origin.
    void evaluate(const QString& javascript);
    QUrl origin() const;
    qint64 bytesServed() const;
    void capture(const QString& path);
signals:
    void loaded();
    void failed(const QString& error);
    void locationChanged();
    void selectionChanged();
    void findChanged(int current, int total, bool truncated);
    void commandRequested(const QString& command);
    void testResult(const QJsonObject& result);
    void captured(bool success);
protected:
    bool event(QEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void showEvent(QShowEvent*) override;
    void focusInEvent(QFocusEvent*) override;
private:
    struct Native;
    std::unique_ptr<Native> native_;
    EpubServer* server_ = nullptr;
    void receive(const QByteArray& json);
    void configure();
    QString path_, title_, error_, locator_, selection_;
    QRectF selectionRect_;
    QSizeF cssViewport_;
    int section_ = 0, sectionCount_ = 0;
    double progress_ = 0, fontScale_ = 1;
    bool ready_ = false, dark_ = false, scrolled_ = false;
    QJsonArray toc_;
    QHash<int, QString> sectionTexts_;
};
