#pragma once

#include <QWidget>
#include <QHash>
#include "comments.h"

class QLabel;
class QScrollArea;
class QPlainTextEdit;
class QFrame;

// Word-style margin cards. Editors update the model immediately; rebuilding is
// reserved for a document switch, addition or deletion so typing keeps its caret.
class CommentPanel : public QWidget {
    Q_OBJECT
public:
    explicit CommentPanel(QWidget* parent = nullptr);
    void setComments(const QVector<app::Comment>& comments);
    void setActiveComment(const QString& id, bool focusEditor = false);
    void setSaveState(const QString& text);

signals:
    void addRequested();
    void closeRequested();
    void activated(const QString& id);
    void navigateRequested(const QString& id);
    void edited(const QString& id, const QString& text);
    void removeRequested(const QString& id);

protected:
    bool eventFilter(QObject* object, QEvent* event) override;

private:
    QScrollArea* scroll_ = nullptr;
    QLabel* count_ = nullptr;
    QLabel* saved_ = nullptr;
    QHash<QString, QFrame*> cards_;
    QHash<QString, QPlainTextEdit*> editors_;
    QString active_;
};
