#include "commentpanel.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

CommentPanel::CommentPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("commentsPanel");
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumWidth(290);
    setMaximumWidth(560);
    setStyleSheet(QStringLiteral(
        "QWidget#commentsPanel { background: #f4f5f7; border-left: 1px solid #dfe2e7; }"
        "QWidget#commentsPanel QLabel { color: #465367; }"
        "QFrame[commentCard=\"true\"] { background: #ffffff; border: 1px solid #dde2e8; border-radius: 8px; }"
        "QFrame[commentCard=\"true\"][active=\"true\"] { border: 2px solid #bc6b27; background: #fffdf7; }"
        "QPlainTextEdit { background: transparent; color: #243247; border: none; selection-background-color: #cce0fb; }"
        "QLabel#commentQuote { background: #faf3df; color: #70582b; padding: 8px; border-radius: 4px; }"
        "QToolButton { color: #6e543b; background: transparent; padding: 4px 6px; }"
        "QToolButton:hover { background: #ede4d6; }"
        "QPushButton { background: #ffffff; color: #77502c; padding: 7px 12px; border: 1px solid #d9c5ac; border-radius: 5px; }"
        "QPushButton:hover { background: #fff4df; }"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 10);
    layout->setSpacing(10);
    auto* heading = new QHBoxLayout;
    count_ = new QLabel(tr("批注"), this);
    QFont font = count_->font(); font.setPointSize(font.pointSize() + 2); font.setBold(true);
    count_->setFont(font);
    heading->addWidget(count_, 1);
    auto* close = new QToolButton(this);
    close->setText(QString::fromUtf8("×")); close->setToolTip(tr("收起批注侧栏"));
    close->setObjectName("closeComments");
    connect(close, &QToolButton::clicked, this, &CommentPanel::closeRequested);
    heading->addWidget(close);
    layout->addLayout(heading);
    auto* add = new QPushButton(tr("＋ 新建批注"), this);
    add->setObjectName("newCommentButton");
    add->setToolTip(tr("先选择正文，再新建批注（Ctrl+Alt+M）"));
    connect(add, &QPushButton::clicked, this, &CommentPanel::addRequested);
    layout->addWidget(add);
    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setStyleSheet("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }");
    layout->addWidget(scroll_, 1);
    saved_ = new QLabel(tr("批注自动保存在本机"), this);
    saved_->setObjectName("commentSaveState");
    saved_->setWordWrap(true);
    layout->addWidget(saved_);
    setComments({});
}

void CommentPanel::setSaveState(const QString& text) { saved_->setText(text); }

void CommentPanel::setComments(const QVector<app::Comment>& comments) {
    const int scrollPosition = scroll_->verticalScrollBar()->value();
    if (auto* old = scroll_->takeWidget()) { old->setEnabled(false); old->hide(); old->deleteLater(); }
    cards_.clear(); editors_.clear();
    count_->setText(tr("批注 · %1").arg(comments.size()));
    auto* content = new QWidget(scroll_);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 3, 0); layout->setSpacing(10);
    if (comments.isEmpty()) {
        auto* empty = new QLabel(tr("把想法写在正文旁\n\n选中一段文字，点击「新建批注」\n也可以按 Ctrl+Alt+M\n\n点击正文标记与卡片，可相互定位"), content);
        empty->setObjectName("commentsEmpty"); empty->setWordWrap(true);
        empty->setAlignment(Qt::AlignCenter); empty->setMargin(16);
        layout->addStretch(); layout->addWidget(empty);
    }
    for (const auto& c : comments) {
        auto* card = new QFrame(content);
        card->setProperty("commentCard", true);
        card->setProperty("active", c.id == active_);
        card->setObjectName("commentCard:" + c.id);
        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(11, 10, 11, 9); cardLayout->setSpacing(7);
        auto* tools = new QHBoxLayout;
        auto* page = new QToolButton(card);
        const bool epub = !c.locator.isEmpty();
        const int first = epub ? c.section + 1 : c.anchors.begin()->first + 1;
        const int last = epub ? first : c.anchors.rbegin()->first + 1;
        page->setText(epub ? tr("第 %1 节 ↗").arg(first) :
            first == last ? tr("第 %1 页 ↗").arg(first) : tr("第 %1–%2 页 ↗").arg(first).arg(last));
        page->setObjectName("commentJump:" + c.id);
        page->setToolTip(tr("定位到正文中的这段文字"));
        connect(page, &QToolButton::clicked, this, [this, id = c.id] { emit navigateRequested(id); });
        tools->addWidget(page); tools->addStretch();
        auto* remove = new QToolButton(card);
        remove->setText(tr("删除")); remove->setObjectName("commentDelete:" + c.id);
        connect(remove, &QToolButton::clicked, this, [this, id = c.id] { emit removeRequested(id); });
        tools->addWidget(remove); cardLayout->addLayout(tools);
        auto* quote = new QLabel(card);
        quote->setObjectName("commentQuote"); quote->setTextFormat(Qt::PlainText);
        quote->setText(c.quote.size() > 180 ? c.quote.left(180) + QString::fromUtf8("…") : c.quote);
        quote->setToolTip(c.quote); quote->setWordWrap(true);
        cardLayout->addWidget(quote);
        auto* editor = new QPlainTextEdit(card);
        editor->setObjectName("commentEditor:" + c.id);
        editor->setProperty("commentId", c.id);
        editor->setAccessibleName((epub ? tr("第 %1 节批注正文") : tr("第 %1 页批注正文")).arg(first));
        editor->setPlaceholderText(tr("写下你的想法…")); editor->setPlainText(c.text);
        editor->setFixedHeight(106);
        editor->setTabChangesFocus(true);
        editor->installEventFilter(this);
        connect(editor, &QPlainTextEdit::textChanged, this, [this, editor, id = c.id] {
            emit edited(id, editor->toPlainText());
        });
        cardLayout->addWidget(editor);
        auto* date = new QLabel(c.created.toLocalTime().toString("MM-dd HH:mm"), card);
        date->setToolTip(tr("创建于 %1").arg(c.created.toLocalTime().toString("yyyy-MM-dd HH:mm")));
        cardLayout->addWidget(date);
        layout->addWidget(card);
        cards_.insert(c.id, card); editors_.insert(c.id, editor);
    }
    layout->addStretch();
    scroll_->setWidget(content);
    scroll_->verticalScrollBar()->setValue(scrollPosition);
}

void CommentPanel::setActiveComment(const QString& id, bool focusEditor) {
    if (active_ != id) {
        for (auto it = cards_.begin(); it != cards_.end(); ++it) {
            it.value()->setProperty("active", it.key() == id);
            it.value()->style()->unpolish(it.value()); it.value()->style()->polish(it.value());
        }
        active_ = id;
    }
    if (auto* card = cards_.value(id)) scroll_->ensureWidgetVisible(card, 0, 14);
    if (focusEditor) if (auto* editor = editors_.value(id)) editor->setFocus(Qt::OtherFocusReason);
}

bool CommentPanel::eventFilter(QObject* object, QEvent* event) {
    if (event->type() == QEvent::FocusIn) emit activated(object->property("commentId").toString());
    return QWidget::eventFilter(object, event);
}
