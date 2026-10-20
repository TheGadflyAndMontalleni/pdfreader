#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QBrush>
#include <QCheckBox>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPrintDialog>
#include <QPrinter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>

#include "annot.h"
#include "pdfview.h"
#include "printing.h"
#include "render.h"
#include "session.h"
#include "pluginmanager.h"
#include "commentpanel.h"
#include "fpdf_save.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QEventLoop>
#include <QHeaderView>
#include <QIntValidator>
#include <QProcess>
#include <QSignalBlocker>
#include <QStyle>
#include <QTreeWidgetItemIterator>
#include "tocworker.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace {

// Page index for a TOC row (-1 when the entry has no resolved target).
constexpr int kPageRole = Qt::UserRole + 1;
constexpr int kNodeKeyRole = Qt::UserRole + 2;

QTreeWidgetItem* MakeItem(const pre::TocNode& node, const QString& key) {
    auto* item = new QTreeWidgetItem();
    QString title = QString::fromUtf8(node.title.c_str());
    if (node.targetPage < 0) {
        item->setText(0, title + QObject::tr("   (未定位)"));
        item->setForeground(0, QBrush(QColor(0x99, 0x99, 0x99)));
    } else {
        item->setText(0, title);
        item->setText(1, QString::number(node.targetPage + 1));
    }
    item->setData(0, kPageRole, node.targetPage);
    item->setData(0, kNodeKeyRole, key);
    item->setToolTip(0, title + "\n" + QObject::tr("来源: %1   置信度: %2")
                            .arg(QString::fromUtf8(pre::TocSourceName(node.source)))
                            .arg(node.confidence, 0, 'f', 2));
    for (size_t i = 0; i < node.children.size(); ++i) {
        item->addChild(MakeItem(node.children[i], key + "/" + QString::number(i)));
    }
    return item;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent, bool autoLoadPlugins) : QMainWindow(parent) {
    buildUi(autoLoadPlugins);
    const QByteArray geo = app::Session::WindowGeometry();
    if (!geo.isEmpty()) restoreGeometry(geo);
    const QByteArray sidebar = app::Session::SidebarState();
    if (!sidebar.isEmpty()) splitter_->restoreState(sidebar);
    updateStatus();
}

MainWindow::~MainWindow() {
    closing_ = true;
    flushComments();
    plugins_.reset();
    for (auto& entry : tabs_) stopToc(entry.get());
}

void MainWindow::buildUi(bool autoLoadPlugins) {
    setWindowTitle(tr("PDF Reader"));
    resize(1280, 860);
    setStyleSheet(QStringLiteral(
        "QToolBar { spacing: 5px; padding: 6px; border: none; }"
        "QToolButton { padding: 6px 9px; border-radius: 5px; }"
        "QToolButton:hover { background: palette(midlight); }"
        "QToolButton:checked { background: palette(highlight); color: palette(highlighted-text); }"
        "QTabBar::tab { padding: 7px 13px; }"
        "QTreeView { border: none; outline: none; }"
        "QTreeView::item { padding: 5px 2px; }"
        "QLineEdit, QComboBox { padding: 4px 6px; }"
        "QStatusBar { padding: 3px; }"));

    // ---- menus ----
    QMenu* fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    fileMenu->addAction(tr("打开(&O)…"), QKeySequence::Open, this, &MainWindow::onOpen);
    recentMenu_ = fileMenu->addMenu(tr("最近文件(&R)"));
    rebuildRecentMenu();
    fileMenu->addAction(tr("重新打开上次关闭"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T),
                        this, [this] {
                            const QString p = app::Session::LastClosedFile();
                            if (!p.isEmpty()) openDocument(p);
                        });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("关闭标签页"), QKeySequence::Close, this,
                        [this] { closeTab(activeTab_); });
    fileMenu->addAction(tr("下一个标签页"), QKeySequence(Qt::CTRL | Qt::Key_Tab), this,
                        &MainWindow::nextTab);
    fileMenu->addAction(tr("上一个标签页"),
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), this,
                        &MainWindow::prevTab);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("打印(&P)…"), QKeySequence::Print, this,
                        &MainWindow::printDocument);
    fileMenu->addAction(tr("另存为…"), QKeySequence::SaveAs, this,
                        &MainWindow::saveDocumentAs);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("导出目录为 JSON…"), this, &MainWindow::saveTocAsJson);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("退出(&Q)"), QKeySequence::Quit, this, &QWidget::close);

    QMenu* viewMenu = menuBar()->addMenu(tr("视图(&V)"));
    viewMenu->addAction(tr("放大"), QKeySequence::ZoomIn, this, [this] { applyZoom(1.25); });
    viewMenu->addAction(tr("缩小"), QKeySequence::ZoomOut, this, [this] { applyZoom(1 / 1.25); });
    fitWidthAction_ = viewMenu->addAction(tr("适应宽度"), QKeySequence(Qt::CTRL | Qt::Key_0), this,
                        &MainWindow::fitWidth);
    fitPageAction_ = viewMenu->addAction(tr("适应页面"), QKeySequence(Qt::CTRL | Qt::Key_1), this,
                        [this] { if (view_) view_->setFitMode(PdfView::FitMode::Page); });
    fitWidthAction_->setCheckable(true);
    fitPageAction_->setCheckable(true);
    viewMenu->addSeparator();
    viewMenu->addAction(tr("连续视图"), QKeySequence(Qt::Key_C), this, [this] {
        if (view_) view_->setViewMode(PdfView::ViewMode::Continuous);
    });
    viewMenu->addAction(tr("单页视图"), QKeySequence(Qt::CTRL | Qt::Key_6), this, [this] {
        if (view_) view_->setViewMode(PdfView::ViewMode::SinglePage);
    });
    viewMenu->addSeparator();

    invertAction_ = viewMenu->addAction(tr("反色"), QKeySequence(Qt::Key_I), this,
                                        &MainWindow::toggleInvert);
    invertAction_->setCheckable(true);

    // Off by default; see the comment on g_lcdText in render.cpp. Left exposed
    // because on a normal LCD at exact 1:1 some people prefer it.
    lcdAction_ = viewMenu->addAction(tr("次像素文字"), this, [this](bool on) {
        pre::SetLcdText(on);
        if (view_) view_->invalidateRenderedPages();
    });
    lcdAction_->setCheckable(true);
    lcdAction_->setChecked(pre::LcdText());

    viewMenu->addSeparator();
    viewMenu->addAction(tr("跳转到页…"), QKeySequence(Qt::CTRL | Qt::Key_G), this,
                        &MainWindow::goToPageDialog);

    QMenu* editMenu = menuBar()->addMenu(tr("编辑(&E)"));
    copyAction_ = editMenu->addAction(tr("复制"), QKeySequence::Copy, this,
                                      [this] { view_->copySelectionToClipboard(); });
    copyAction_->setEnabled(false);
    editMenu->addAction(tr("全选"), QKeySequence::SelectAll, this, [this] { view_->selectAll(); });
    editMenu->addAction(tr("取消选择"), QKeySequence(Qt::Key_Escape), this,
                        [this] { view_->clearSelection(); });
    editMenu->addSeparator();
    markupMenu_ = editMenu->addMenu(tr("标注(&A)"));
    markupMenu_->addAction(tr("高亮"), QKeySequence(Qt::CTRL | Qt::Key_H), this,
                           [this] { applyMarkup(0); });
    markupMenu_->addAction(tr("下划线"), QKeySequence(Qt::CTRL | Qt::Key_U), this,
                           [this] { applyMarkup(1); });
    markupMenu_->addAction(tr("删除线"), this, [this] { applyMarkup(2); });
    markupMenu_->addSeparator();
    markupMenu_->addAction(tr("撤销上一处标注"), QKeySequence(Qt::CTRL | Qt::Key_Z), this,
                           &MainWindow::undoMarkup);
    newCommentAction_ = markupMenu_->addAction(tr("新建批注"),
        QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_M), this, [this] { addComment(); });
    newCommentAction_->setObjectName("newComment");
    newCommentAction_->setToolTip(tr("选中文字，在正文旁写下批注（Ctrl+Alt+M）"));
    showCommentsAction_ = viewMenu->addAction(tr("批注侧栏"));
    showCommentsAction_->setObjectName("showComments");
    showCommentsAction_->setCheckable(true);
    showCommentsAction_->setIconText(tr("批注"));
    editMenu->addSeparator();
    editMenu->addAction(tr("查找…"), QKeySequence::Find, this, &MainWindow::showFindBar);
    editMenu->addAction(tr("查找下一个"), QKeySequence(Qt::Key_F3), this, &MainWindow::findNext);
    editMenu->addAction(tr("查找上一个"), QKeySequence(Qt::SHIFT | Qt::Key_F3), this,
                        &MainWindow::findPrev);

    QMenu* bookmarksMenu = menuBar()->addMenu(tr("书签(&B)"));
    bookmarksMenu->setObjectName("bookmarksMenu");
    addBookmarkAction_ = bookmarksMenu->addAction(tr("添加书签"),
        QKeySequence(Qt::CTRL | Qt::Key_D), this, [this] { addBookmark(); });
    addBookmarkAction_->setObjectName("addBookmark");
    bookmarksMenu->addAction(tr("显示书签"), this, [this] { showSidebarPage(1); });
    bookmarksMenu->addSeparator();
    renameBookmarkAction_ = bookmarksMenu->addAction(tr("重命名书签…"), this, &MainWindow::editSelectedBookmark);
    renameBookmarkAction_->setObjectName("renameBookmark");
    renameBookmarkAction_->setIconText(tr("重命名"));
    renameBookmarkAction_->setShortcut(QKeySequence(Qt::Key_F2));
    renameBookmarkAction_->setShortcutContext(Qt::WidgetShortcut);
    removeBookmarkAction_ = bookmarksMenu->addAction(tr("删除所选书签"), this, [this] {
        if (bookmarkTree_->currentItem()) removeBookmark(bookmarkTree_->indexOfTopLevelItem(bookmarkTree_->currentItem()));
    });
    removeBookmarkAction_->setObjectName("removeBookmark");
    removeBookmarkAction_->setIconText(tr("删除"));
    removeBookmarkAction_->setShortcut(QKeySequence(Qt::Key_Delete));
    removeBookmarkAction_->setShortcutContext(Qt::WidgetShortcut);
    removeBookmarkAction_->setToolTip(tr("删除选中的个人书签（选中书签后按 Delete）"));
    renameBookmarkAction_->setEnabled(false);
    removeBookmarkAction_->setEnabled(false);

    auto* pluginsMenu = menuBar()->addMenu(tr("插件(&P)"));
    pluginsMenu->setObjectName("pluginsMenu");
    pluginsMenu->addAction(tr("插件管理…"), this, [this] { plugins_->showDialog(); })->setObjectName("managePlugins");
    pluginsMenu->addSeparator();

    // ---- toolbar ----
    QToolBar* bar = addToolBar(tr("阅读工具"));
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    bar->setIconSize(QSize(18, 18));
    bar->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton), tr("打开"),
                   this, &MainWindow::onOpen);
    bar->addSeparator();
    auto* prevAct = bar->addAction(style()->standardIcon(QStyle::SP_ArrowBack),
                                   tr("上一页"), this, &MainWindow::onPrevPage);
    auto* nextAct = bar->addAction(style()->standardIcon(QStyle::SP_ArrowForward),
                                   tr("下一页"), this, &MainWindow::onNextPage);
    prevAct->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Left));
    nextAct->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Right));
    pageEdit_ = new QLineEdit(bar);
    pageEdit_->setObjectName("pageNumber");
    pageEdit_->setFixedWidth(58);
    pageEdit_->setAlignment(Qt::AlignCenter);
    pageEdit_->setValidator(new QIntValidator(1, 10000000, pageEdit_));
    pageEdit_->setToolTip(tr("输入页码并按回车跳转"));
    bar->addWidget(pageEdit_);
    pageCountLabel_ = new QLabel(bar);
    pageCountLabel_->setMinimumWidth(44);
    bar->addWidget(pageCountLabel_);
    connect(pageEdit_, &QLineEdit::returnPressed, this, [this] {
        if (document_) view_->setPage(pageEdit_->text().toInt() - 1);
        updateStatus();
        view_->setFocus();
    });
    bar->addSeparator();
    bar->addAction(tr("−"), this, [this] { applyZoom(1 / 1.25); })->setToolTip(tr("缩小"));
    zoomBox_ = new QComboBox(bar);
    zoomBox_->setObjectName("zoomLevel");
    zoomBox_->setEditable(true);
    zoomBox_->setFixedWidth(92);
    zoomBox_->addItems({"50%", "75%", "100%", "125%", "150%", "200%", "300%", "400%"});
    bar->addWidget(zoomBox_);
    auto applyEnteredZoom = [this] {
        QString text = zoomBox_->currentText();
        text.remove('%');
        bool valid = false;
        const double zoom = text.toDouble(&valid) / 100.0;
        if (document_ && valid && zoom >= 0.1 && zoom <= 8.0) {
            view_->setFitMode(PdfView::FitMode::None);
            view_->setZoom(zoom);
        }
        updateStatus();
    };
    connect(zoomBox_, &QComboBox::activated, this, [applyEnteredZoom](int) { applyEnteredZoom(); });
    connect(zoomBox_->lineEdit(), &QLineEdit::returnPressed, this, applyEnteredZoom);
    bar->addAction(tr("+"), this, [this] { applyZoom(1.25); })->setToolTip(tr("放大"));
    bar->addAction(fitWidthAction_);
    bar->addAction(fitPageAction_);
    bar->addSeparator();
    continuousAction_ = bar->addAction(tr("连续阅读"), this, [this](bool checked) {
        view_->setViewMode(checked ? PdfView::ViewMode::Continuous : PdfView::ViewMode::SinglePage);
        updateStatus();
    });
    continuousAction_->setCheckable(true);
    continuousAction_->setObjectName("continuousView");
    bar->addAction(tr("查找"), this, &MainWindow::showFindBar);
    bar->addAction(addBookmarkAction_);
    bar->addSeparator();
    bar->addAction(newCommentAction_);
    bar->addAction(showCommentsAction_);

    // ---- find bar (hidden until Ctrl+F) ----
    addToolBarBreak();
    findBar_ = new QToolBar(tr("查找"), this);
    findBar_->setMovable(false);
    addToolBar(findBar_);

    findEdit_ = new QLineEdit(findBar_);
    findEdit_->setPlaceholderText(tr("查找…"));
    findEdit_->setClearButtonEnabled(true);
    // A QLineEdit is horizontally expanding by default, which in a toolbar
    // swallows the whole row and pushes every action out of sight.
    findEdit_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    findEdit_->setFixedWidth(260);
    findBar_->addWidget(findEdit_);

    findBar_->addAction(tr("上一个"), this, &MainWindow::findPrev);
    findBar_->addAction(tr("下一个"), this, &MainWindow::findNext);

    findCase_ = new QCheckBox(tr("区分大小写"), findBar_);
    findBar_->addWidget(findCase_);

    findStatus_ = new QLabel(findBar_);
    findStatus_->setMinimumWidth(100);
    findBar_->addWidget(findStatus_);

    findBar_->addAction(tr("关闭"), this, &MainWindow::closeFindBar);
    findBar_->hide();

    connect(findEdit_, &QLineEdit::textChanged, this, [this](const QString&) { runFind(); });
    connect(findEdit_, &QLineEdit::returnPressed, this, &MainWindow::findNext);
    connect(findCase_, &QCheckBox::toggled, this, [this](bool) { runFind(); });

    // ---- sidebar: automatic TOC and personal bookmarks ----
    auto* sidebar = new QWidget(this);
    auto* sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    auto* tocPage = new QWidget(sidebar);
    auto* tocLayout = new QVBoxLayout(tocPage);
    tocLayout->setContentsMargins(10, 8, 10, 6);
    tocStatus_ = new QLabel(tocPage);
    tocLayout->addWidget(tocStatus_);
    auto* tocTools = new QToolBar(tocPage);
    tocTools->setIconSize(QSize(16, 16));
    tocTree_ = new QTreeWidget(tocPage);
    tocTree_->setObjectName("tableOfContents");
    tocTree_->setColumnCount(2);
    tocTree_->setHeaderLabels({tr("目录"), tr("页码")});
    tocTree_->setUniformRowHeights(true);
    tocTree_->setIndentation(16);
    tocTree_->header()->setStretchLastSection(false);
    tocTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tocTree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tocTools->addAction(tr("全部展开"), tocTree_, &QTreeWidget::expandAll)->setObjectName("expandToc");
    tocTools->addAction(tr("全部收起"), tocTree_, &QTreeWidget::collapseAll)->setObjectName("collapseToc");
    tocLayout->addWidget(tocTools);
    tocLayout->addWidget(tocTree_, 1);
    connect(tocTree_, &QTreeWidget::itemClicked, this, &MainWindow::onTocItemClicked);

    auto* bookmarksPage = new QWidget(sidebar);
    auto* bookmarkLayout = new QVBoxLayout(bookmarksPage);
    bookmarkLayout->setContentsMargins(10, 8, 10, 6);
    // A standalone QToolBar can hide its last action without exposing an
    // overflow button. Keep every bookmark operation visible in a narrow pane.
    auto* bookmarkTools = new QHBoxLayout;
    bookmarkTools->setContentsMargins(0, 0, 0, 0);
    bookmarkTools->setSpacing(4);
    for (auto* action : {addBookmarkAction_, renameBookmarkAction_, removeBookmarkAction_}) {
        auto* button = new QToolButton(bookmarksPage);
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        button->setStyleSheet("QToolButton { padding: 5px 4px; }");
        bookmarkTools->addWidget(button);
    }
    bookmarkLayout->addLayout(bookmarkTools);
    bookmarkEmpty_ = new QLabel(tr("还没有书签\n阅读时按 Ctrl+D 保存当前页"), bookmarksPage);
    bookmarkEmpty_->setAlignment(Qt::AlignCenter);
    bookmarkEmpty_->setWordWrap(true);
    bookmarkLayout->addWidget(bookmarkEmpty_);
    bookmarkTree_ = new QTreeWidget(bookmarksPage);
    bookmarkTree_->setObjectName("personalBookmarks");
    bookmarkTree_->setColumnCount(2);
    bookmarkTree_->setHeaderLabels({tr("书签"), tr("页码")});
    bookmarkTree_->setRootIsDecorated(false);
    bookmarkTree_->setUniformRowHeights(true);
    bookmarkTree_->addAction(renameBookmarkAction_);
    bookmarkTree_->addAction(removeBookmarkAction_);
    bookmarkTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    bookmarkTree_->header()->setStretchLastSection(false);
    bookmarkTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    bookmarkTree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    bookmarkLayout->addWidget(bookmarkTree_, 1);
    connect(bookmarkTree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int) {
        jumpToBookmark(bookmarkTree_->indexOfTopLevelItem(item));
    });
    connect(bookmarkTree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem*, int) { editSelectedBookmark(); });
    connect(bookmarkTree_, &QWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        bookmarkTree_->setCurrentItem(bookmarkTree_->itemAt(position));
        QMenu menu(bookmarkTree_);
        menu.addAction(renameBookmarkAction_);
        menu.addAction(removeBookmarkAction_);
        menu.exec(bookmarkTree_->viewport()->mapToGlobal(position));
    });
    connect(bookmarkTree_, &QTreeWidget::itemSelectionChanged, this, [this] {
        const bool selected = bookmarkTree_->currentItem() != nullptr;
        renameBookmarkAction_->setEnabled(selected);
        removeBookmarkAction_->setEnabled(selected);
    });
    sideTabs_ = new QTabWidget(sidebar);
    sideTabs_->setObjectName("navigationTabs");
    sideTabs_->addTab(tocPage, tr("目录"));
    sideTabs_->addTab(bookmarksPage, tr("书签"));
    sideLayout->addWidget(sideTabs_, 1);

    // ---- page view ----
    // PdfView is itself a scroll area: it owns the continuous-scroll strip of
    // pages, so there is no outer QScrollArea to nest it in.
    view_ = new PdfView(this);

    // Keep the existing TOC splitter state compatible. A nested reading
    // splitter gives the margin its own independently resizable width.
    commentPanel_ = new CommentPanel(this);
    commentPanel_->hide();
    readingSplitter_ = new QSplitter(Qt::Horizontal, this);
    readingSplitter_->setObjectName("readingSplitter");
    readingSplitter_->addWidget(view_);
    readingSplitter_->addWidget(commentPanel_);
    readingSplitter_->setStretchFactor(0, 1);
    readingSplitter_->setStretchFactor(1, 0);
    readingSplitter_->setChildrenCollapsible(false);
    readingSplitter_->setSizes({660, 320});
    connect(showCommentsAction_, &QAction::toggled, this, [this](bool show) {
        commentPanel_->setVisible(show && document_);
    });
    connect(commentPanel_, &CommentPanel::closeRequested, this, [this] { showCommentsAction_->setChecked(false); });
    connect(commentPanel_, &CommentPanel::addRequested, this, [this] { addComment(); });
    connect(commentPanel_, &CommentPanel::activated, this, [this](const QString& id) { activateComment(id, false); });
    connect(commentPanel_, &CommentPanel::navigateRequested, this, [this](const QString& id) { activateComment(id); });
    connect(commentPanel_, &CommentPanel::edited, this, [this](const QString& id, const QString& text) { editComment(id, text); });
    connect(commentPanel_, &CommentPanel::removeRequested, this, [this](const QString& id) { removeComment(id); });
    connect(view_, &PdfView::newCommentRequested, this, [this] { addComment(); });
    connect(view_, &PdfView::commentActivated, this, [this](const QString& id) { activateComment(id, false, true); });
    commentSaveTimer_ = new QTimer(this);
    commentSaveTimer_->setSingleShot(true); commentSaveTimer_->setInterval(350);
    connect(commentSaveTimer_, &QTimer::timeout, this, [this] { flushComments(); });
    view_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(view_, &QWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        QMenu menu(view_);
        menu.addAction(copyAction_);
        menu.addSeparator();
        menu.addAction(newCommentAction_);
        menu.addAction(showCommentsAction_);
        menu.addSeparator();
        auto* highlight = menu.addAction(tr("高亮所选文字"), this, [this] { applyMarkup(0); });
        highlight->setEnabled(view_->hasSelection());
        menu.exec(view_->viewport()->mapToGlobal(position));
    });

    splitter_ = new QSplitter(Qt::Horizontal, this);
    splitter_->addWidget(sidebar);
    splitter_->addWidget(readingSplitter_);
    splitter_->setStretchFactor(0, 0);
    splitter_->setStretchFactor(1, 1);
    sidebar->setMinimumWidth(220);
    sidebar->setMaximumWidth(520);
    splitter_->setSizes({300, 980});

    // The tab strip is purely a selector: the content below is a single
    // sidebar + view pair that gets re-bound when the active tab changes.
    tabBar_ = new QTabBar(this);
    tabBar_->setObjectName("documentTabs");
    tabBar_->setExpanding(false);
    tabBar_->setDrawBase(false);
    tabBar_->setTabsClosable(true);
    tabBar_->setMovable(true);
    connect(tabBar_, &QTabBar::tabMoved, this, [this](int from, int to) {
        DocTab* active = cur();
        auto entry = std::move(tabs_[static_cast<size_t>(from)]);
        tabs_.erase(tabs_.begin() + from);
        tabs_.insert(tabs_.begin() + to, std::move(entry));
        for (int i = 0; i < tabCount(); ++i) if (tab(i) == active) activeTab_ = i;
    });
    tabBar_->setElideMode(Qt::ElideRight);
    tabBar_->hide();   // no documents yet
    connect(tabBar_, &QTabBar::currentChanged, this, &MainWindow::onTabChanged);
    connect(tabBar_, &QTabBar::tabCloseRequested, this, &MainWindow::onTabCloseRequested);

    auto* central       = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(tabBar_);
    centralLayout->addWidget(splitter_, 1);
    setCentralWidget(central);

    // ---- status bar ----
    pageLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(pageLabel_);

    connect(view_, &PdfView::pageChanged, this, &MainWindow::onPageChanged);
    connect(view_, &PdfView::openRequested, this, &MainWindow::onOpen);
    connect(view_, &PdfView::zoomChanged, this, &MainWindow::onZoomChanged);
    connect(view_, &PdfView::configurationChanged, this, &MainWindow::updateStatus);
    connect(view_, &PdfView::selectionChanged, this, [this](bool has) {
        if (copyAction_) copyAction_->setEnabled(has);
        newCommentAction_->setEnabled(document_ && has);
        if (plugins_) plugins_->notifySelectionChanged();
    });
    connect(view_, &PdfView::findMatchChanged, this, [this](int index, int total) {
        if (total <= 0) {
            if (!findEdit_->text().isEmpty()) findStatus_->setText(tr("无匹配"));
            return;
        }
        findStatus_->setText(tr("%1 / %2").arg(index + 1).arg(total));
    });
    plugins_ = std::make_unique<app::PluginManager>(this, pluginsMenu);
    // Plugin metadata and enabled extensions are processed after the first frame.
    // New plugins (including the bundled example) remain disabled until chosen.
    if (autoLoadPlugins) QTimer::singleShot(250, this, [this] { if (!closing_) plugins_->loadEnabled(); });
}

pdfreader::DocumentInfo MainWindow::pluginDocument() const {
    const auto* entry = cur();
    if (!entry) return {};
    return {entry->path, QFileInfo(entry->path).fileName(), entry->index->PageCount(), view_->page() + 1};
}
QString MainWindow::pluginSelectedText() const { return cur() ? view_->selectedText() : QString(); }
QString MainWindow::pluginPageText(int pageNumber) const {
    if (!index_ || pageNumber < 1 || pageNumber > index_->PageCount()) return {};
    QStringList lines;
    for (const auto& line : index_->Lines(pageNumber - 1)) lines.append(QString::fromUtf8(line.text.c_str()));
    return lines.join('\n');
}

void MainWindow::setTocStatus(const QString& text, bool warning) {
    tocStatus_->setText(text);
    QPalette colors = tocStatus_->palette();
    colors.setColor(QPalette::WindowText, palette().color(warning ? QPalette::Text : QPalette::PlaceholderText));
    tocStatus_->setPalette(colors);
    tocStatus_->setToolTip(text);
}

bool MainWindow::openDocument(const QString& path) {
    QApplication::setOverrideCursor(Qt::WaitCursor);

    // Already open? Just bring that tab forward rather than parsing it twice.
    const QString canonical = QFileInfo(path).canonicalFilePath().isEmpty()
                                  ? QFileInfo(path).absoluteFilePath()
                                  : QFileInfo(path).canonicalFilePath();
    for (int i = 0; i < tabCount(); ++i) {
        DocTab* t = tab(i);
        if (!t) continue;
        const QString other = QFileInfo(t->path).canonicalFilePath().isEmpty()
                                  ? QFileInfo(t->path).absoluteFilePath()
                                  : QFileInfo(t->path).canonicalFilePath();
        if (other == canonical) {
            QApplication::restoreOverrideCursor();
            activateTab(i);
            return true;
        }
    }

    auto entry = std::make_unique<DocTab>();
    entry->doc = std::make_unique<pre::Document>();
    std::string err;
    const QByteArray local = path.toUtf8();
    if (!entry->doc->Open(local.constData(), &err)) {
        QApplication::restoreOverrideCursor();
        QMessageBox::critical(this, tr("打开失败"),
                              tr("无法打开 %1\n\n%2").arg(path,
                                 QString::fromUtf8(err.c_str())));
        return false;
    }

    entry->path  = path;
    entry->commentSourceSize = QFileInfo(path).size();
    entry->commentSourceModified = QFileInfo(path).lastModified().toMSecsSinceEpoch();
    entry->index = std::make_unique<pre::PageIndex>(entry->doc->handle());

    entry->id = ++nextDocumentId_;
    entry->bookmarks = app::Session::LoadBookmarks(path);
    entry->comments = app::ReadPdfComments(entry->doc->handle());
    if (const auto savedComments = app::LoadLocalComments(path, entry->index->PageCount())) {
        // Reconcile our embedded annotations with the latest local snapshot.
        // Deleted comments stay deleted, including after reopening an export.
        const auto embedded = entry->comments;
        for (const auto& c : embedded) app::RemovePdfComment(entry->doc->handle(), c);
        entry->comments.clear();
        bool restored = true;
        for (const auto& c : *savedComments) {
            if (app::AddPdfComment(entry->doc->handle(), c)) entry->comments.push_back(c);
            else { restored = false; break; }
        }
        if (!restored) {
            for (const auto& c : entry->comments) app::RemovePdfComment(entry->doc->handle(), c);
            entry->comments.clear();
            for (const auto& c : embedded)
                if (app::AddPdfComment(entry->doc->handle(), c)) entry->comments.push_back(c);
            statusBar()->showMessage(tr("部分本机批注无法恢复，已保留文件中的批注"), 8000);
        }
    }
    entry->bookmarks.erase(std::remove_if(entry->bookmarks.begin(), entry->bookmarks.end(),
        [&entry](const app::Bookmark& b) { return b.page >= entry->index->PageCount(); }), entry->bookmarks.end());
    const app::DocState saved = app::Session::LoadDocState(path);
    if (saved.valid) {
        entry->page = std::clamp(saved.page, 0, std::max(0, entry->index->PageCount() - 1));
        entry->zoom = saved.zoom;
        entry->fitMode = std::clamp(saved.fitMode, 0, 2);
        entry->viewMode = std::clamp(saved.viewMode, 0, 1);
        entry->invert = saved.invert;
    }
    entry->toc = pre::OutlineToToc(entry->doc->handle());
    entry->tocCacheKey = app::TocCacheKey(path);
    if (entry->toc.roots.empty()) {
        entry->tocPending = !app::LoadTocCache(entry->tocCacheKey, entry->index->PageCount(),
                                              &entry->toc, &entry->stats);
    }

    tabs_.push_back(std::move(entry));
    const int newIndex = tabCount() - 1;

    tabBar_->blockSignals(true);
    tabBar_->insertTab(newIndex, QFileInfo(path).fileName());
    tabBar_->setTabToolTip(newIndex, path);
    tabBar_->blockSignals(false);
    tabBar_->show();

    QApplication::restoreOverrideCursor();

    app::Session::AddRecentFile(path);
    app::Session::SetLastOpenDir(QFileInfo(path).absolutePath());
    app::Session::SetLastClosedFile(path);
    rebuildRecentMenu();

    activateTab(newIndex);

    // State belongs to this DocTab before it is bound. No queued callback may
    // accidentally apply another document's saved page to the active view.
    QTimer::singleShot(0, this, &MainWindow::startNextToc);

    updateStatus();
    return true;
}

// ---------------------------------------------------------------------------
// tabs
// ---------------------------------------------------------------------------

MainWindow::DocTab* MainWindow::tab(int i) const {
    if (i < 0 || i >= tabCount()) return nullptr;
    return tabs_[static_cast<size_t>(i)].get();
}

MainWindow::DocTab* MainWindow::cur() const { return tab(activeTab_); }

void MainWindow::stashActiveState() {
    DocTab* t = cur();
    if (!t || !view_) return;
    t->page     = view_->page();
    t->zoom     = view_->zoom();
    t->fitMode  = static_cast<int>(view_->fitMode());
    t->viewMode = static_cast<int>(view_->viewMode());
    t->invert   = view_->invertColors();
    if (!t->tocPending && !t->toc.roots.empty()) {
        t->expandedToc.clear();
        for (QTreeWidgetItemIterator it(tocTree_); *it; ++it)
            if ((*it)->isExpanded()) t->expandedToc.insert((*it)->data(0, kNodeKeyRole).toString());
        t->tocStateSaved = true;
    }
}

void MainWindow::activateTab(int index) {
    if (index < 0 || index >= tabCount()) return;
    if (index == activeTab_) return;
    flushComments();
    stashActiveState();

    activeTab_ = index;
    if (tabBar_->currentIndex() != index) {
        tabBar_->blockSignals(true);
        tabBar_->setCurrentIndex(index);
        tabBar_->blockSignals(false);
    }
    bindTab(index);
}

void MainWindow::bindTab(int index) {
    DocTab* t = tab(index);
    if (!t) return;

    document_    = t->doc.get();
    index_       = t->index.get();
    toc_         = t->toc;
    currentPath_ = t->path;

    refreshToc();
    refreshBookmarks();
    view_->setDocument(document_->handle(), index_->PageCount(), index_);
    view_->setViewMode(static_cast<PdfView::ViewMode>(t->viewMode));
    view_->setFitMode(static_cast<PdfView::FitMode>(t->fitMode));
    view_->setInvertColors(t->invert);
    if (t->fitMode == static_cast<int>(PdfView::FitMode::None) && t->zoom > 0.0) {
        view_->setZoom(t->zoom);
    }
    if (t->page > 0 && t->page < index_->PageCount()) view_->setPage(t->page);
    refreshComments();
    commentPanel_->setVisible(showCommentsAction_->isChecked());

    // Find results belong to a document, not to the window.
    view_->clearFindResults();
    if (findBar_ && findBar_->isVisible() && findEdit_ && !findEdit_->text().isEmpty()) {
        runFind();
    }

    setWindowTitle(QString("%1 — PDF Reader").arg(QFileInfo(t->path).fileName()));
    updateStatus();
    if (plugins_) plugins_->notifyDocumentChanged();
}

void MainWindow::refreshTabTitle(int index) {
    DocTab* t = tab(index);
    if (!t) return;
    tabBar_->setTabText(index, QFileInfo(t->path).fileName());
    tabBar_->setTabToolTip(index, t->path);
}

void MainWindow::stopToc(DocTab* entry) {
    if (!entry || !entry->tocProcess) return;
    QProcess* process = entry->tocProcess;
    entry->tocProcess = nullptr;
    process->disconnect(this);
    process->kill();
    process->deleteLater();
}

void MainWindow::refreshToc() {
    const DocTab* entry = cur();
    if (!entry) { tocTree_->clear(); return; }
    if (entry->tocPending) {
        tocTree_->clear();
        setTocStatus(tr("正在生成目录… 可继续阅读"), false);
    } else if (entry->tocFailed) {
        tocTree_->clear();
        setTocStatus(tr("目录生成失败，可重新打开文档重试"), true);
    } else {
        populateToc(entry->toc, entry->stats);
    }
}

void MainWindow::startNextToc() {
    if (closing_) return;
    for (const auto& entry : tabs_) if (entry->tocProcess) return;
    DocTab* next = cur();
    if (!next || !next->tocPending) {
        next = nullptr;
        for (const auto& entry : tabs_) if (entry->tocPending) { next = entry.get(); break; }
    }
    if (!next) return;
    auto* worker = new QProcess(this);
    next->tocProcess = worker;
    const quint64 id = next->id;
    connect(worker, &QProcess::finished, this, [this, id, worker](int code, QProcess::ExitStatus) {
        finishToc(id, worker, code);
    });
    connect(worker, &QProcess::errorOccurred, this, [this, id, worker](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finishToc(id, worker, -1);
    });
    auto* deadline = new QTimer(worker);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, worker, &QProcess::kill);
    deadline->start(60000);
    worker->start(QCoreApplication::applicationFilePath(), {"--toc-worker", next->path});
}

void MainWindow::finishToc(quint64 id, QProcess* worker, int exitCode) {
    DocTab* entry = nullptr;
    for (const auto& candidate : tabs_)
        if (candidate->id == id && candidate->tocProcess == worker) entry = candidate.get();
    if (!entry) { worker->deleteLater(); return; }
    entry->tocProcess = nullptr;
    const QByteArray json = worker->readAllStandardOutput();
    entry->tocPending = false;
    entry->tocFailed = exitCode != 0 ||
        !app::DecodeToc(json, entry->index->PageCount(), &entry->toc, &entry->stats);
    if (!entry->tocFailed && entry->tocCacheKey == app::TocCacheKey(entry->path))
        app::SaveTocCache(entry->tocCacheKey, json);
    worker->deleteLater();
    if (entry == cur()) {
        toc_ = entry->toc;
        refreshToc();
    }
    QTimer::singleShot(0, this, &MainWindow::startNextToc);
}

bool MainWindow::waitForTocs(int timeoutMs) {
    auto ready = [this] {
        for (const auto& entry : tabs_) if (entry->tocPending) return false;
        return true;
    };
    startNextToc();
    if (ready()) return true;
    QEventLoop loop;
    QTimer poll;
    poll.setInterval(10);
    connect(&poll, &QTimer::timeout, &loop, [&] { if (ready()) loop.quit(); });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    poll.start();
    loop.exec();
    return ready();
}

int MainWindow::bookmarkCount() const {
    return cur() ? cur()->bookmarks.size() : 0;
}

void MainWindow::refreshBookmarks() {
    bookmarkTree_->clear();
    if (const DocTab* entry = cur()) {
        for (const auto& bookmark : entry->bookmarks) {
            auto* item = new QTreeWidgetItem({bookmark.title, QString::number(bookmark.page + 1)});
            item->setData(0, kPageRole, bookmark.page);
            item->setToolTip(0, bookmark.title);
            bookmarkTree_->addTopLevelItem(item);
        }
    }
    bookmarkEmpty_->setVisible(bookmarkCount() == 0);
    renameBookmarkAction_->setEnabled(false);
    removeBookmarkAction_->setEnabled(false);
}

bool MainWindow::addBookmark(const QString& title) {
    DocTab* entry = cur();
    if (!entry || view_->pageCount() == 0) return false;
    const int page = view_->page();
    auto it = std::find_if(entry->bookmarks.begin(), entry->bookmarks.end(),
                           [page](const app::Bookmark& b) { return b.page == page; });
    if (it == entry->bookmarks.end()) {
        entry->bookmarks.push_back({page, title.trimmed().isEmpty() ? tr("第 %1 页").arg(page + 1) : title.trimmed()});
    } else if (!title.trimmed().isEmpty()) {
        it->title = title.trimmed();
    }
    std::sort(entry->bookmarks.begin(), entry->bookmarks.end(),
              [](const app::Bookmark& a, const app::Bookmark& b) { return a.page < b.page; });
    app::Session::SaveBookmarks(entry->path, entry->bookmarks);
    refreshBookmarks();
    showSidebarPage(1);
    for (int row = 0; row < entry->bookmarks.size(); ++row)
        if (entry->bookmarks[row].page == page) bookmarkTree_->setCurrentItem(bookmarkTree_->topLevelItem(row));
    return true;
}

bool MainWindow::renameBookmark(int row, const QString& title) {
    DocTab* entry = cur();
    if (!entry || row < 0 || row >= entry->bookmarks.size() || title.trimmed().isEmpty()) return false;
    entry->bookmarks[row].title = title.trimmed();
    app::Session::SaveBookmarks(entry->path, entry->bookmarks);
    refreshBookmarks();
    bookmarkTree_->setCurrentItem(bookmarkTree_->topLevelItem(row));
    return true;
}

bool MainWindow::removeBookmark(int row) {
    DocTab* entry = cur();
    if (!entry || row < 0 || row >= entry->bookmarks.size()) return false;
    entry->bookmarks.removeAt(row);
    app::Session::SaveBookmarks(entry->path, entry->bookmarks);
    refreshBookmarks();
    if (!entry->bookmarks.isEmpty())
        bookmarkTree_->setCurrentItem(bookmarkTree_->topLevelItem(std::min(row, int(entry->bookmarks.size()) - 1)));
    return true;
}

bool MainWindow::jumpToBookmark(int row) {
    const DocTab* entry = cur();
    if (!entry || row < 0 || row >= entry->bookmarks.size()) return false;
    view_->setPage(entry->bookmarks[row].page);
    return true;
}

void MainWindow::editSelectedBookmark() {
    const int row = bookmarkTree_->indexOfTopLevelItem(bookmarkTree_->currentItem());
    if (!cur() || row < 0) return;
    bool accepted = false;
    const QString title = QInputDialog::getText(this, tr("重命名书签"), tr("书签名称"),
        QLineEdit::Normal, cur()->bookmarks[row].title, &accepted);
    if (accepted) renameBookmark(row, title);
}

void MainWindow::closeTab(int index) {
    if (index < 0 || index >= tabCount()) return;
    if (!flushComments()) {
        QMessageBox::warning(this, tr("批注尚未保存"), tr("无法保存本机批注，请先使用「文件 → 另存为」保存 PDF，再关闭文档。"));
        return;
    }
    stashActiveState();
    DocTab* active = cur();
    DocTab* removed = tab(index);
    app::DocState st;
    st.page = removed->page; st.zoom = removed->zoom; st.fitMode = removed->fitMode;
    st.viewMode = removed->viewMode; st.invert = removed->invert; st.valid = true;
    app::Session::SaveDocState(removed->path, st);
    stopToc(removed);
    // Borrowed page/text handles must be detached before their owner dies.
    if (removed == active) {
        view_->clearDocument();
        document_ = nullptr;
        index_ = nullptr;
    }
    { QSignalBlocker block(tabBar_); tabBar_->removeTab(index); }
    tabs_.erase(tabs_.begin() + index);
    activeTab_ = -1;
    if (tabs_.empty()) {
        toc_ = pre::TocTree();
        currentPath_.clear();
        tocTree_->clear();
        refreshBookmarks();
        refreshComments();
        commentPanel_->hide();
        tabBar_->hide();
        setTocStatus(tr("打开文档后显示目录"), false);
        setWindowTitle(tr("PDF Reader"));
        updateStatus();
        if (plugins_) plugins_->notifyDocumentChanged();
        return;
    }
    int next = std::min(index, tabCount() - 1);
    if (removed != active) for (int i = 0; i < tabCount(); ++i) if (tab(i) == active) next = i;
    activateTab(next);
    startNextToc();
}

void MainWindow::onTabChanged(int index) { activateTab(index); }

void MainWindow::onTabCloseRequested(int index) { closeTab(index); }

void MainWindow::nextTab() {
    if (tabCount() < 2) return;
    activateTab((activeTab_ + 1) % tabCount());
}

void MainWindow::prevTab() {
    if (tabCount() < 2) return;
    activateTab((activeTab_ - 1 + tabCount()) % tabCount());
}

bool MainWindow::activateDocTab(int index) {
    if (index < 0 || index >= tabCount()) return false;
    activateTab(index);
    return activeTab_ == index;
}

int MainWindow::tabPage(int index) const {
    if (index == activeTab_) return view_ ? view_->page() : -1;
    DocTab* t = tab(index);
    return t ? t->page : -1;
}

int MainWindow::tabPageCount(int index) const {
    DocTab* t = tab(index);
    return (t && t->index) ? t->index->PageCount() : 0;
}

QString MainWindow::tabTitle(int index) const {
    DocTab* t = tab(index);
    return t ? QFileInfo(t->path).fileName() : QString();
}

bool MainWindow::setActivePage(int page) {
    if (!view_ || activeTab_ < 0) return false;
    view_->setPage(page);
    return view_->page() == page;
}

int MainWindow::tabTocNodes(int index) const {
    DocTab* t = tab(index);
    return t ? t->toc.NodeCount() : 0;
}

QString MainWindow::tabTocSource(int index) const {
    DocTab* t = tab(index);
    if (!t) return QString();
    return QString::fromUtf8(pre::TocSourceName(t->toc.source));
}

int MainWindow::activeViewMode() const {
    return view_ ? static_cast<int>(view_->viewMode()) : -1;
}

int MainWindow::activeFitMode() const {
    return view_ ? static_cast<int>(view_->fitMode()) : -1;
}

bool MainWindow::setActiveViewMode(int mode) {
    if (!view_) return false;
    view_->setViewMode(static_cast<PdfView::ViewMode>(mode));
    return static_cast<int>(view_->viewMode()) == mode;
}

bool MainWindow::setActiveFitMode(int mode) {
    if (!view_) return false;
    view_->setFitMode(static_cast<PdfView::FitMode>(mode));
    return static_cast<int>(view_->fitMode()) == mode;
}

int MainWindow::activePageCount() const {
    return view_ ? view_->pageCount() : 0;
}

bool MainWindow::activeInvert() const {
    return view_ && view_->invertColors();
}

double MainWindow::viewportLuminance() {
    if (!view_) return -1.0;
    view_->repaint();
    const QImage shot = view_->grabViewport();
    if (shot.isNull() || shot.width() == 0 || shot.height() == 0) return -1.0;

    long long sum = 0;
    long long n = 0;
    for (int y = 0; y < shot.height(); y += 3) {
        for (int x = 0; x < shot.width(); x += 3) {
            const QRgb px = shot.pixel(x, y);
            // Rec. 601 luma, integer form.
            sum += (299 * qRed(px) + 587 * qGreen(px) + 114 * qBlue(px)) / 1000;
            ++n;
        }
    }
    return n > 0 ? static_cast<double>(sum) / static_cast<double>(n) : -1.0;
}

void MainWindow::restoreSessionTabs() {
    const QStringList paths = app::Session::OpenTabs();
    for (const QString& p : paths) openDocument(p);
}

void MainWindow::saveCurrentState() {
    stashActiveState();
    DocTab* t = cur();
    if (!t) return;
    app::DocState st;
    st.page     = t->page;
    st.zoom     = t->zoom;
    st.fitMode  = t->fitMode;
        st.viewMode = t->viewMode;
        st.invert   = t->invert;
    st.valid    = true;
    app::Session::SaveDocState(t->path, st);
}

void MainWindow::rebuildRecentMenu() {
    if (!recentMenu_) return;
    recentMenu_->clear();

    const QStringList files = app::Session::RecentFiles();
    if (files.isEmpty()) {
        QAction* none = recentMenu_->addAction(tr("(无)"));
        none->setEnabled(false);
        return;
    }

    int index = 0;
    for (const QString& f : files) {
        ++index;
        const QString label = index <= 9
                                  ? tr("&%1  %2").arg(index).arg(QFileInfo(f).fileName())
                                  : QFileInfo(f).fileName();
        QAction* act = recentMenu_->addAction(label);
        act->setToolTip(f);
        connect(act, &QAction::triggered, this, [this, f] { openRecent(f); });
    }
    recentMenu_->addSeparator();
    recentMenu_->addAction(tr("清除最近记录"), this, [this] {
        app::Session::ClearRecentFiles();
        rebuildRecentMenu();
    });
}

void MainWindow::openRecent(const QString& path) {
    if (!openDocument(path)) {
        // The entry is gone or unreadable; drop it so it stops being offered.
        app::Session::RemoveRecentFile(path);
        rebuildRecentMenu();
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!flushComments()) {
        QMessageBox::warning(this, tr("批注尚未保存"), tr("无法保存本机批注，请先使用「文件 → 另存为」保存 PDF，再退出。"));
        event->ignore();
        return;
    }
    stashActiveState();
    QStringList open;
    for (int i = 0; i < tabCount(); ++i) {
        DocTab* t = tab(i);
        if (!t) continue;
        app::DocState st;
        st.page     = t->page;
        st.zoom     = t->zoom;
        st.fitMode  = t->fitMode;
        st.viewMode = t->viewMode;
        st.invert   = t->invert;
        st.valid    = true;
        app::Session::SaveDocState(t->path, st);
        open << t->path;
    }
    app::Session::SetOpenTabs(open);
    app::Session::SetWindowGeometry(saveGeometry());
    app::Session::SetSidebarState(splitter_->saveState());
    app::Session::Flush();
    if (plugins_) plugins_->shutdown();
    QMainWindow::closeEvent(event);
}

void MainWindow::populateToc(const pre::TocTree& tree, const pre::ResolveStats& stats) {
    tocTree_->clear();
    for (size_t i = 0; i < tree.roots.size(); ++i)
        tocTree_->addTopLevelItem(MakeItem(tree.roots[i], QString::number(i)));
    if (cur() && cur()->tocStateSaved) {
        for (QTreeWidgetItemIterator it(tocTree_); *it; ++it)
            (*it)->setExpanded(cur()->expandedToc.contains((*it)->data(0, kNodeKeyRole).toString()));
    } else {
        tocTree_->expandToDepth(1);
    }

    if (tree.roots.empty()) {
        setTocStatus(tr("未识别到目录"),
                     true);
        return;
    }

    QString source = tr("排版目录");
    if (tree.source == pre::TocSource::EmbeddedOutline) source = tr("文档目录");
    else if (tree.source == pre::TocSource::PrintedTocPage) source = tr("自动目录");
    setTocStatus(tr("%1 · %2 项").arg(source).arg(tree.NodeCount()), false);
    tocStatus_->setToolTip(tr("目录来源：%1\n标题定位命中率：%2")
        .arg(source).arg(stats.titleHitRate, 0, 'f', 2));
}

void MainWindow::onTocItemClicked(QTreeWidgetItem* item, int) {
    if (!item) return;
    const int page = item->data(0, kPageRole).toInt();
    if (page < 0) return;
    view_->setPage(page);
}

void MainWindow::onPageChanged(int page) {
    updateStatus();
    if (plugins_) plugins_->notifyPageChanged();
}

void MainWindow::onZoomChanged(double) { updateStatus(); }

void MainWindow::applyMarkup(int kind) {
    if (!document_ || !view_) return;
    if (!view_->hasSelection()) {
        statusBar()->showMessage(tr("请先选中要标注的文字"), 4000);
        return;
    }

    const auto byPage = view_->selectionRectsByPage();
    if (byPage.empty()) return;

    // Highlight is the classic highlighter yellow; the line marks are red so
    // they read as an edit rather than a marker.
    const unsigned int rgb = (kind == 0) ? 0xFFF000u : 0xE03030u;

    // Marking a whole document is a synchronous walk over every page, so say so
    // rather than appearing to hang.
    QApplication::setOverrideCursor(Qt::WaitCursor);

    int total = 0;
    for (const auto& entry : byPage) {
        const int drawn = pre::AddMarkup(document_->handle(), entry.first,
                                         static_cast<pre::MarkupKind>(kind),
                                         entry.second, rgb);
        if (drawn > 0) total += drawn;
    }

    QApplication::restoreOverrideCursor();

    if (total <= 0) {
        statusBar()->showMessage(tr("标注失败"), 4000);
        return;
    }
    // The cached rasters predate the mark, so they have to be thrown away for
    // the reader to see it.
    view_->invalidateRenderedPages();
    statusBar()->showMessage(
        tr("已添加标注（%1 处）· 用「文件 → 另存为」保存").arg(total), 6000);
}

void MainWindow::undoMarkup() {
    if (!document_) return;
    const int page = pre::UndoLastMarkup(document_->handle());
    if (page < 0) {
        statusBar()->showMessage(tr("没有可撤销的标注"), 3000);
        return;
    }
    view_->invalidateRenderedPages();
    statusBar()->showMessage(tr("已撤销第 %1 页上的标注").arg(page + 1), 4000);
}

void MainWindow::saveDocumentAs() {
    if (!document_) return;

    const QFileInfo info(currentPath_);
    const QString suggested =
        info.absolutePath() + "/" + info.completeBaseName() + "-annotated.pdf";
    const QString path = QFileDialog::getSaveFileName(this, tr("另存为"), suggested,
                                                      tr("PDF 文档 (*.pdf)"));
    if (path.isEmpty()) return;

    if (!saveDocumentToPath(path)) {
        QMessageBox::warning(this, tr("保存失败"), tr("无法写入 %1").arg(path));
        return;
    }
    pre::ResetMarkupState();
    statusBar()->showMessage(tr("已保存到 %1").arg(path), 6000);
}

bool MainWindow::saveDocumentToPath(const QString& path) {
    if (!document_ || path.isEmpty()) return false;
    // QSaveFile provides Unicode paths and atomic replacement: a failed write
    // cannot truncate an existing original or exported PDF.
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) return false;
    struct Writer {
        FPDF_FILEWRITE base;
        QSaveFile* file;
    } writer;
    writer.base.version = 1;
    writer.base.WriteBlock = [](FPDF_FILEWRITE* self, const void* data, unsigned long size) -> int {
        auto* w = reinterpret_cast<Writer*>(self);
        return w->file->write(static_cast<const char*>(data), size) == qint64(size);
    };
    writer.file = &output;
    if (!FPDF_SaveAsCopy(document_->handle(), &writer.base, 0)) return false;
    if (!output.commit()) return false;
    // Re-stamp local state when the user chose to overwrite the source.
    if (QFileInfo(path).canonicalFilePath() == QFileInfo(currentPath_).canonicalFilePath()) {
        cur()->commentSourceSize = QFileInfo(currentPath_).size();
        cur()->commentSourceModified = QFileInfo(currentPath_).lastModified().toMSecsSinceEpoch();
        cur()->commentsDirty = true;
        flushComments();
    }
    // A successful PDF export is also a durable fallback if local settings
    // cannot be written. Never strand the user in a close-warning loop.
    if (!flushComments()) {
        cur()->commentsDirty = false;
        commentPanel_->setSaveState(tr("批注已保存到 PDF · 本机自动保存不可用"));
    }
    return true;
}

QVector<app::Comment> MainWindow::comments() const { return cur() ? cur()->comments : QVector<app::Comment>{}; }

void MainWindow::refreshComments() {
    auto* entry = cur();
    const auto list = comments();
    commentPanel_->setComments(list);
    view_->setComments(list);
    if (entry) {
        view_->setActiveComment(entry->activeComment);
        commentPanel_->setActiveComment(entry->activeComment);
    }
    commentPanel_->setSaveState(entry && entry->commentsDirty ? tr("正在保存批注…") :
        tr("批注自动保存在本机 · 另存 PDF 可随文件分享"));
}

bool MainWindow::flushComments() {
    bool ok = true;
    for (const auto& entry : tabs_) {
        if (!entry->commentsDirty) continue;
        if (app::SaveLocalComments(entry->path, entry->comments, entry->commentSourceSize,
            entry->commentSourceModified)) entry->commentsDirty = false;
        else ok = false;
    }
    if (commentPanel_) commentPanel_->setSaveState(ok ? tr("已自动保存在本机 · 另存 PDF 可随文件分享") :
        tr("本机保存失败，请另存 PDF 保留批注"));
    return ok;
}

bool MainWindow::addComment(const QString& text) {
    auto* entry = cur();
    if (!entry || !view_->hasSelection()) {
        statusBar()->showMessage(tr("先拖动选中正文中的文字，再新建批注（Ctrl+Alt+M）"), 5000);
        return false;
    }
    app::Comment c;
    c.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.quote = view_->selectedText(); c.text = text;
    c.anchors = view_->selectionRectsByPage();
    c.created = c.modified = QDateTime::currentDateTimeUtc();
    if (!app::AddPdfComment(document_->handle(), c)) {
        statusBar()->showMessage(tr("无法为这段文字添加批注"), 5000); return false;
    }
    entry->comments.push_back(c);
    std::stable_sort(entry->comments.begin(), entry->comments.end(), [](const app::Comment& a, const app::Comment& b) {
        if (a.anchors.begin()->first != b.anchors.begin()->first) return a.anchors.begin()->first < b.anchors.begin()->first;
        return a.anchors.begin()->second.front().top > b.anchors.begin()->second.front().top;
    });
    entry->activeComment = c.id; entry->commentsDirty = true;
    view_->clearSelection();
    view_->invalidateRenderedPages();
    refreshComments();
    activateComment(c.id, true, true);
    flushComments();
    return true;
}

bool MainWindow::editComment(const QString& id, const QString& text) {
    auto* entry = cur();
    if (!entry) return false;
    for (auto& c : entry->comments) {
        if (c.id != id) continue;
        if (c.text == text) return true;
        auto changed = c;
        changed.text = text; changed.modified = QDateTime::currentDateTimeUtc();
        if (!app::UpdatePdfComment(document_->handle(), changed)) {
            commentPanel_->setSaveState(tr("批注更新失败，请保留文字后重试")); return false;
        }
        c = std::move(changed);
        entry->commentsDirty = true;
        commentPanel_->setSaveState(tr("正在保存批注…"));
        commentSaveTimer_->start();
        return true;
    }
    return false;
}

bool MainWindow::removeComment(const QString& id) {
    auto* entry = cur();
    if (!entry) return false;
    for (auto it = entry->comments.begin(); it != entry->comments.end(); ++it) {
        if (it->id != id) continue;
        if (!app::RemovePdfComment(document_->handle(), *it)) {
            statusBar()->showMessage(tr("删除批注失败"), 5000); return false;
        }
        entry->comments.erase(it);
        if (entry->activeComment == id) entry->activeComment.clear();
        entry->commentsDirty = true;
        view_->invalidateRenderedPages();
        refreshComments();
        flushComments();
        return true;
    }
    return false;
}

bool MainWindow::activateComment(const QString& id, bool navigate, bool focusEditor) {
    auto* entry = cur();
    if (!entry) return false;
    if (std::none_of(entry->comments.begin(), entry->comments.end(),
        [&](const app::Comment& c) { return c.id == id; })) return false;
    entry->activeComment = id;
    showCommentsAction_->setChecked(true);
    view_->setActiveComment(id);
    if (navigate) view_->goToComment(id);
    commentPanel_->setActiveComment(id, focusEditor);
    return true;
}

QImage MainWindow::viewportImage() {
    if (!view_) return QImage();
    view_->repaint();
    return view_->grabViewport();
}

int MainWindow::viewportMarkupPixels() {
    if (!view_) return -1;
    view_->repaint();
    const QImage shot = view_->grabViewport();
    if (shot.isNull()) return -1;

    int hits = 0;
    for (int y = 0; y < shot.height(); y += 2) {
        for (int x = 0; x < shot.width(); x += 2) {
            const QRgb px = shot.pixel(x, y);
            // Highlighter yellow: strong red and green, weak blue.
            if (qRed(px) > 180 && qGreen(px) > 160 && qBlue(px) < 140) ++hits;
        }
    }
    return hits;
}

void MainWindow::showSidebarPage(int index) {
    if (sideTabs_) sideTabs_->setCurrentIndex(index);
}

void MainWindow::toggleInvert() {
    if (!view_) return;
    const bool on = !view_->invertColors();
    view_->setInvertColors(on);
    if (invertAction_) invertAction_->setChecked(on);
}

// ---------------------------------------------------------------------------
// find
// ---------------------------------------------------------------------------

void MainWindow::showFindBar() {
    findBar_->show();
    findEdit_->setFocus();
    findEdit_->selectAll();
}

void MainWindow::closeFindBar() {
    findBar_->hide();
    findStatus_->clear();
    view_->clearFindResults();
    view_->setFocus();
}

void MainWindow::runFind() {
    if (!index_ || !view_) return;
    const QString needle = findEdit_->text();
    if (needle.isEmpty()) {
        view_->clearFindResults();
        findStatus_->clear();
        return;
    }
    const pre::FindResults results =
        pre::FindInDocument(*index_, needle.toStdString(), findCase_->isChecked());
    view_->setFindResults(results);
    if (results.matches.empty()) {
        findStatus_->setText(tr("无匹配"));
    } else if (results.truncated) {
        findStatus_->setText(tr("%1+ 个匹配").arg(results.matches.size()));
    }
}

void MainWindow::findNext() {
    if (findBar_->isHidden()) showFindBar();
    if (view_->matchCount() == 0) runFind();
    view_->goToNextMatch();
}

void MainWindow::findPrev() {
    if (findBar_->isHidden()) showFindBar();
    if (view_->matchCount() == 0) runFind();
    view_->goToPrevMatch();
}

void MainWindow::onNextPage() { view_->goToNextPage(); }void MainWindow::onPrevPage() { view_->goToPrevPage(); }
void MainWindow::onFirstPage() { view_->goToFirstPage(); }
void MainWindow::onLastPage() { view_->goToLastPage(); }

void MainWindow::updateStatus() {
    const bool loaded = document_ != nullptr;
    newCommentAction_->setEnabled(loaded && view_->hasSelection());
    showCommentsAction_->setEnabled(loaded);
    addBookmarkAction_->setEnabled(loaded);
    pageEdit_->setEnabled(loaded);
    zoomBox_->setEnabled(loaded);
    { QSignalBlocker block(pageEdit_); pageEdit_->setText(loaded ? QString::number(view_->page() + 1) : QString()); }
    pageCountLabel_->setText(loaded ? tr("/ %1").arg(view_->pageCount()) : tr("/ —"));
    { QSignalBlocker block(zoomBox_); zoomBox_->setCurrentText(loaded ? QString::number(qRound(view_->zoom() * 100)) + "%" : "100%"); }
    continuousAction_->setChecked(view_->viewMode() == PdfView::ViewMode::Continuous);
    fitWidthAction_->setChecked(loaded && view_->fitMode() == PdfView::FitMode::Width);
    fitPageAction_->setChecked(loaded && view_->fitMode() == PdfView::FitMode::Page);
    if (!document_) {
        pageLabel_->setText(tr("未打开文档"));
        return;
    }
    pageLabel_->setText(tr("第 %1 / %2 页    缩放 %3%")
                            .arg(view_->page() + 1)
                            .arg(view_->pageCount())
                            .arg(qRound(view_->zoom() * 100)));
}

void MainWindow::fitWidth() {
    if (!document_) return;
    // The view owns the fit-to-width flag and recomputes on its own resize.
    view_->setFitWidthMode(true);
}

void MainWindow::applyZoom(double factor) {
    // Explicit zoom leaves fit-to-width mode.
    view_->setFitWidthMode(false);
    view_->setZoom(view_->zoom() * factor);
}

bool MainWindow::runSelfTest(QString* report) {
    waitForTocs();
    QStringList lines;
    bool ok = true;

    auto check = [&](bool condition, const QString& what) {
        const QString line = QString("%1  %2").arg(condition ? "PASS" : "FAIL").arg(what);
        lines << line;
        // Print as we go: a crash part-way through would otherwise discard the
        // entire report and leave no clue where it died.
        std::printf("%s\n", line.toUtf8().constData());
        std::fflush(stdout);
        if (!condition) ok = false;
    };

    check(document_ != nullptr, "document opened");
    if (!document_) {
        if (report) *report = lines.join('\n');
        return false;
    }
    check(view_->pageCount() > 0, QString("page count = %1").arg(view_->pageCount()));
    view_->setViewMode(PdfView::ViewMode::Continuous);
    view_->setFitMode(PdfView::FitMode::Width);
    view_->goToFirstPage();
    view_->grabViewport();
    check(view_->hasImage(), "page 1 rendered to an image");

    const int tocNodes = toc_.NodeCount();
    check(tocNodes > 0, QString("TOC built with %1 node(s), source=%2")
                            .arg(tocNodes)
                            .arg(QString::fromUtf8(pre::TocSourceName(toc_.source))));

    // Find a TOC row with a resolved target, including one nested child, and
    // drive the real click handler.
    QTreeWidgetItem* target = nullptr;
    int expectedPage = -1;
    std::function<void(QTreeWidgetItem*)> walk = [&](QTreeWidgetItem* item) {
        if (!item || target) return;
        const int page = item->data(0, kPageRole).toInt();
        if (page > 0) {  // prefer a non-first page so the jump is observable
            target = item;
            expectedPage = page;
            return;
        }
        for (int i = 0; i < item->childCount() && !target; ++i) walk(item->child(i));
    };
    for (int i = 0; i < tocTree_->topLevelItemCount() && !target; ++i) {
        walk(tocTree_->topLevelItem(i));
    }

    check(target != nullptr, "a TOC entry with a resolved page exists");
    if (target) {
        const int before = view_->page();
        onTocItemClicked(target, 0);   // the exact path a user click takes
        check(view_->page() == expectedPage,
              QString("clicking \"%1\" moved the view %2 -> %3 (expected %4)")
                  .arg(target->text(0))
                  .arg(before)
                  .arg(view_->page())
                  .arg(expectedPage));
        check(view_->hasImage(), "page re-rendered after the jump");
    }

    // Layout: at fit-to-width no page should need horizontal scrolling.
    const int    viewportW = view_->viewport()->width();
    const int    viewportH = view_->viewport()->height();
    const double pageW     = view_->pageSizePoints(0).width() * view_->zoom();
    lines << QString("INFO  viewport=%1x%2px  page0=%3px  zoom=%4%  strip=%5px  splitter=[%6]")
                 .arg(viewportW)
                 .arg(viewportH)
                 .arg(static_cast<int>(pageW))
                 .arg(static_cast<int>(view_->zoom() * 100))
                 .arg(view_->contentHeight())
                 .arg(splitter_ ? QString("%1,%2")
                                      .arg(splitter_->sizes().value(0))
                                      .arg(splitter_->sizes().value(1))
                                : QStringLiteral("-"));
    if (viewportW > 0) {
        check(pageW <= viewportW + 4,
              QString("page width %1 fits viewport %2")
                  .arg(static_cast<int>(pageW))
                  .arg(viewportW));
    }

    // Continuous scroll: the whole document is one scrollable strip, so
    // jumping to the last page must actually land there and render it.
    if (view_->pageCount() > 1) {
        const int last = view_->pageCount() - 1;
        view_->goToLastPage();
        check(view_->page() == last,
              QString("goToLastPage lands on page %1 (got %2)").arg(last + 1).arg(view_->page() + 1));
        check(view_->hasImage(), "last page rendered");
        view_->goToFirstPage();
        check(view_->page() == 0, "goToFirstPage returns to page 1");

        // Walking down one page at a time must advance monotonically.
        view_->goToNextPage();
        check(view_->page() == 1, QString("goToNextPage advances to page 2 (got %1)")
                                      .arg(view_->page() + 1));
        view_->goToPrevPage();
        check(view_->page() == 0, "goToPrevPage returns to page 1");
    }

    // Text selection: a full-line selection must round-trip the page's own
    // line text, and copying must actually reach the clipboard.
    {
        const int testPage = (view_->pageCount() > 1) ? 1 : 0;
        const auto& pageLines = index_->Lines(testPage);
        int chosen = -1;
        for (size_t i = 0; i < pageLines.size(); ++i) {
            if (pageLines[i].charIndices.size() >= 4) {
                chosen = static_cast<int>(i);
                break;
            }
        }
        check(chosen >= 0, QString("page %1 has a selectable line").arg(testPage + 1));

        if (chosen >= 0) {
            check(view_->selectLine(testPage, chosen), "selectLine selects the whole line");
            check(view_->hasSelection(), "a selection is active after selectLine");

            const QString text = view_->selectedText();
            lines << QString("INFO  selected line = \"%1\"")
                         .arg(text.left(36).replace('\n', ' '));
            check(!text.isEmpty(), "selectedText() is not empty");
            check(pre::NormalizeForMatch(text.toStdString()) ==
                      pre::NormalizeForMatch(pageLines[static_cast<size_t>(chosen)].text),
                  "selected text matches the page's own line text");

            view_->copySelectionToClipboard();
            check(QApplication::clipboard()->text() == text,
                  "copy places the selection on the clipboard");
        }

        // A document-wide selection necessarily spans lines and pages.
        view_->selectAll();
        const QString all = view_->selectedText();
        check(all.contains('\n'), "select-all spans multiple lines");
        check(all.size() > 200, QString("select-all text is substantial (%1 chars)")
                                    .arg(all.size()));

        // The highlight must actually reach the screen, not just the model:
        // grab the viewport and look for pixels tinted toward the highlight
        // colour (blue channel clearly above red).
        if (chosen >= 0) {
            view_->selectLine(testPage, chosen);
            view_->repaint();
            const QImage shot = view_->grabViewport();
            int tinted = 0;
            for (int y = 0; y < shot.height(); y += 2) {
                for (int x = 0; x < shot.width(); x += 2) {
                    const QRgb px = shot.pixel(x, y);
                    if (qBlue(px) > qRed(px) + 8 && qBlue(px) > 120) ++tinted;
                }
            }
            check(tinted > 200, QString("selection highlight is painted (%1 tinted px)")
                                    .arg(tinted));
        }

        view_->clearSelection();
        check(!view_->hasSelection(), "clearSelection drops the selection");
    }

    // Find: the needle is taken from the document's own text, so the test
    // cannot pass vacuously, and every match position is round-tripped back
    // through the selection layer to prove it holds the searched text.
    {
        QString needle;
        for (int p = 0; p < view_->pageCount() && needle.isEmpty(); ++p) {
            const auto& pageLines = index_->Lines(p);
            for (const auto& L : pageLines) {
                const QString t = QString::fromStdString(L.text).trimmed();
                if (t.size() >= 6 && !t.contains(QLatin1Char(' '))) {
                    needle = t.left(4);
                    break;
                }
            }
        }
        check(!needle.isEmpty(),
              QString("derived a needle from the document: \"%1\"").arg(needle));

        if (!needle.isEmpty()) {
            const pre::FindResults fr =
                pre::FindInDocument(*index_, needle.toStdString(), false);
            check(!fr.matches.empty(), QString("find \"%1\" -> %2 match(es) on %3 page(s)")
                                           .arg(needle)
                                           .arg(fr.matches.size())
                                           .arg(fr.scannedPages));

            view_->setFindResults(fr);
            check(view_->matchCount() == static_cast<int>(fr.matches.size()),
                  "the view holds every match");

            const int probe = std::min<int>(5, static_cast<int>(fr.matches.size()));
            int exact = 0;
            for (int i = 0; i < probe; ++i) {
                if (view_->matchText(i) == needle) ++exact;
            }
            check(exact == probe,
                  QString("match positions hold the needle exactly (%1/%2)")
                      .arg(exact)
                      .arg(probe));

            if (view_->matchCount() > 1) {
                const int before = view_->currentMatchIndex();
                view_->goToNextMatch();
                check(view_->currentMatchIndex() != before ||
                          view_->matchCount() == 1,
                      QString("find-next advances the current match (%1 -> %2)")
                          .arg(before)
                          .arg(view_->currentMatchIndex()));
                view_->goToPrevMatch();
                check(view_->currentMatchIndex() == before,
                      "find-previous returns to the previous match");
            }

            // Case sensitivity needs a needle that has ASCII case at all; a
            // CJK needle cannot exercise it. Derive one from the document.
            QString asciiNeedle;
            for (int p = 0; p < view_->pageCount() && asciiNeedle.isEmpty(); ++p) {
                const auto& pageLines = index_->Lines(p);
                for (const auto& L : pageLines) {
                    const QString t = QString::fromStdString(L.text);
                    static const QRegularExpression kRun(QStringLiteral("[A-Za-z]{3,}"));
                    auto it = kRun.globalMatch(t);
                    while (it.hasNext()) {
                        const QString tok = it.next().captured(0);
                        if (tok != tok.toLower()) { asciiNeedle = tok; break; }
                    }
                    if (!asciiNeedle.isEmpty()) break;
                }
            }

            if (!asciiNeedle.isEmpty()) {
                const QString lower = asciiNeedle.toLower();
                const auto csUpper = pre::FindInDocument(*index_, asciiNeedle.toStdString(), true);
                const auto csLower = pre::FindInDocument(*index_, lower.toStdString(), true);
                const auto insens  = pre::FindInDocument(*index_, lower.toStdString(), false);
                lines << QString("INFO  case probe \"%1\": sensitive=%2 lower-sensitive=%3 "
                                 "insensitive=%4")
                             .arg(asciiNeedle)
                             .arg(csUpper.matches.size())
                             .arg(csLower.matches.size())
                             .arg(insens.matches.size());
                check(insens.matches.size() >= csUpper.matches.size(),
                      "case-insensitive finds at least what case-sensitive finds");
                check(csUpper.matches.size() != csLower.matches.size() ||
                          (csUpper.matches.empty() && csLower.matches.empty()) == false,
                      "case changes what matches");
            } else {
                lines << "INFO  no ASCII token with case in this document";
            }

            const auto none = pre::FindInDocument(*index_, "qqzzxx-no-such-text", false);
            check(none.matches.empty(), "a nonsense needle finds nothing");

            view_->clearFindResults();
            check(view_->matchCount() == 0, "clearFindResults empties the match list");
        }
    }

    // View modes: single-page must shrink the laid-out content to one page, and
    // fit-page must satisfy both dimensions.
    {
        view_->setViewMode(PdfView::ViewMode::Continuous);
        const int continuousH = view_->contentHeight();
        check(continuousH > viewportH,
              QString("continuous content (%1px) is taller than the viewport")
                  .arg(continuousH));

        view_->setFitMode(PdfView::FitMode::Page);
        const double pw = view_->pageSizePoints(view_->page()).width() * view_->zoom();
        const double ph = view_->pageSizePoints(view_->page()).height() * view_->zoom();
        lines << QString("INFO  fit-page: page=%1x%2px viewport=%3x%4px  zoom=%5%")
                     .arg(static_cast<int>(pw)).arg(static_cast<int>(ph))
                     .arg(viewportW).arg(viewportH)
                     .arg(static_cast<int>(view_->zoom() * 100));
        check(pw <= viewportW + 4 && ph <= viewportH + 4,
              "fit-page keeps the whole page inside the viewport");

        // Being the right *size* is not enough -- the page also has to land
        // inside the viewport rather than being pushed off to one side.
        {
            const auto& pl = index_->Lines(view_->page());
            if (!pl.empty() && !pl[0].charIndices.empty()) {
                const QPoint at = view_->posToViewport(TextPos{view_->page(), 0, 0});
                lines << QString("INFO  first line of page %1 maps to x=%2 y=%3 "
                                 "(viewport %4x%5)")
                             .arg(view_->page() + 1).arg(at.x()).arg(at.y())
                             .arg(viewportW).arg(viewportH);
                check(at.x() >= 0 && at.x() < viewportW,
                      QString("page content lies inside the viewport horizontally (x=%1)")
                          .arg(at.x()));
            }
        }

        view_->setViewMode(PdfView::ViewMode::SinglePage);
        const int singleH = view_->contentHeight();
        check(singleH < continuousH,
              QString("single-page content (%1px) is shorter than continuous (%2px)")
                  .arg(singleH).arg(continuousH));
        check(singleH <= viewportH + 4 || view_->zoom() < 1.0,
              QString("single page fits vertically (%1px vs %2px)").arg(singleH).arg(viewportH));

        // Paging must still work in single-page mode.
        view_->setPage(3);
        check(view_->page() == 3, QString("single-page mode pages to 4 (got %1)")
                                      .arg(view_->page() + 1));

        view_->setViewMode(PdfView::ViewMode::Continuous);
        // Compare at the *current* zoom: the fit-page step above changed it, so
        // the original `continuousH` is no longer the right baseline.
        check(view_->contentHeight() > singleH * 2,
              QString("switching back restores the full strip (%1px vs one page %2px)")
                  .arg(view_->contentHeight()).arg(singleH));
        view_->setFitMode(PdfView::FitMode::Width);
        view_->goToFirstPage();
    }

    // Invert colours: the toggle must reach the pixels, not just the model.
    if (view_) {
        view_->setInvertColors(false);
        const double normal = viewportLuminance();
        view_->setInvertColors(true);
        const double inverted = viewportLuminance();
        lines << QString("INFO  viewport luminance: normal=%1 inverted=%2")
                     .arg(normal, 0, 'f', 1).arg(inverted, 0, 'f', 1);
        check(normal > 0 && inverted > 0, "viewport could be sampled both ways");
        check(inverted < normal * 0.6,
              QString("invert darkens the page (%1 -> %2)")
                  .arg(normal, 0, 'f', 1).arg(inverted, 0, 'f', 1));
        view_->setInvertColors(false);
        const double restored = viewportLuminance();
        check(restored > inverted * 1.5,
              QString("turning invert off restores the page (%1 -> %2)")
                  .arg(inverted, 0, 'f', 1).arg(restored, 0, 'f', 1));
    }

    // Markup: the mark must reach the pixels, and it must survive saving.
    if (index_ && view_ && view_->pageCount() > 1) {
        const int markupPage = 1;
        view_->setPage(markupPage);
        const auto& pl = index_->Lines(markupPage);
        int lineIdx = -1;
        for (size_t i = 0; i < pl.size(); ++i) {
            if (pl[i].charIndices.size() >= 6) { lineIdx = static_cast<int>(i); break; }
        }
        check(lineIdx >= 0, "found a line to annotate");
        if (lineIdx >= 0) {
            view_->selectLine(markupPage, lineIdx);
            const QImage beforeShot = viewportImage();

            std::printf("MARK step: applyMarkup\n"); std::fflush(stdout);
            applyMarkup(0);   // highlight
            std::printf("MARK step: viewportImage (after)\n"); std::fflush(stdout);
            const QImage afterShot = viewportImage();

            // Compare images rather than testing for a specific colour: a
            // translucent highlighter over white is only faintly yellow, and a
            // colour threshold would call it invisible when it is plainly there.
            int changed = 0;
            if (!beforeShot.isNull() && !afterShot.isNull() &&
                beforeShot.size() == afterShot.size()) {
                for (int y = 0; y < afterShot.height(); y += 2) {
                    for (int x = 0; x < afterShot.width(); x += 2) {
                        if (beforeShot.pixel(x, y) != afterShot.pixel(x, y)) ++changed;
                    }
                }
            }
            lines << QString("INFO  highlighter changed %1 sampled pixel(s)").arg(changed);
            check(changed > 100,
                  QString("the highlight is visible on screen (%1 changed px)").arg(changed));

            // Undo must take it away again, or the reader loses control of it.
            std::printf("MARK step: undoMarkup\n"); std::fflush(stdout);
            undoMarkup();
            std::printf("MARK step: viewportImage (undone)\n"); std::fflush(stdout);
            const QImage undoneShot = viewportImage();
            int stillChanged = 0;
            if (!beforeShot.isNull() && !undoneShot.isNull() &&
                beforeShot.size() == undoneShot.size()) {
                for (int y = 0; y < undoneShot.height(); y += 2) {
                    for (int x = 0; x < undoneShot.width(); x += 2) {
                        if (beforeShot.pixel(x, y) != undoneShot.pixel(x, y)) ++stillChanged;
                    }
                }
            }
            check(stillChanged <= changed / 5,
                  QString("undo removes the mark (%1 -> %2 changed px)")
                      .arg(changed).arg(stillChanged));

            // Re-apply, then save and reopen: the mark has to be in the file.
            std::printf("MARK step: applyMarkup (again)\n"); std::fflush(stdout);
            applyMarkup(0);
            const QString outPath =
                QDir::tempPath() + "/pdfreader-markup-selftest.pdf";
            QFile::remove(outPath);
            std::printf("MARK step: save\n"); std::fflush(stdout);
            const bool saved =
                pre::SaveDocumentToFile(document_->handle(),
                                        outPath.toLocal8Bit().constData());
            check(saved, QString("saved to %1").arg(outPath));

            if (saved) {
                std::printf("MARK step: reopen\n"); std::fflush(stdout);
                pre::Document reopened;
                std::string    err;
                if (!reopened.Open(outPath.toLocal8Bit().constData(), &err)) {
                    check(false, "reopened the saved file");
                } else {
                    std::printf("MARK step: render reopened\n"); std::fflush(stdout);
                    pre::RenderedPage rp;
                    const bool rendered = pre::RenderPage(reopened.handle(), markupPage,
                                                          1.0, &rp);
                    check(rendered, "rendered the reopened page");
                    int yellow = 0;
                    if (rendered) {
                        for (int y = 0; y + 3 < rp.height; ++y) {
                            for (int x = 0; x + 3 < rp.width; ++x) {
                                const uint8_t* px =
                                    rp.pixels.data() +
                                    static_cast<size_t>(y) * rp.stride +
                                    static_cast<size_t>(x) * 4;
                                // BGRA. The mark is translucent, so accept a
                                // distinctly yellow cast rather than pure yellow.
                                if (px[2] > 200 && px[1] > 190 && px[2] - px[0] > 40) {
                                    ++yellow;
                                }
                            }
                        }
                    }
                    lines << QString("INFO  yellowish pixels in the reopened file: %1")
                                 .arg(yellow);
                    check(yellow > 200,
                          QString("the mark survived saving (%1 px)").arg(yellow));
                }
            }
            view_->clearSelection();

            // How expensive is a mark? Marking every page of a long document is
            // a synchronous walk, so the cost per page is worth knowing.
            QElapsedTimer timer;
            timer.start();
            int marks = 0;
            const int timingPages = std::min(10, index_->PageCount());
            for (int p = 0; p < timingPages; ++p) {
                std::vector<pre::MarkupRect> rects;
                for (const auto& ln : index_->Lines(p)) {
                    if (rects.size() >= 20) break;
                    rects.push_back({ln.left, ln.bottom, ln.right, ln.top});
                }
                if (rects.empty()) continue;
                if (pre::AddMarkup(document_->handle(), p, pre::MarkupKind::Highlight,
                                   rects, 0xFFF000) > 0) {
                    ++marks;
                }
            }
            const double msPerPage =
                marks > 0 ? static_cast<double>(timer.elapsed()) / marks : -1.0;
            lines << QString("INFO  markup cost: %1 ms/page over %2 page(s)")
                         .arg(msPerPage, 0, 'f', 1).arg(marks);
        }
    }

    // Features that are only reachable from a menu need that menu checked, or
    // they ship invisible. (The invert toggle was written, tested and
    // documented before anyone noticed it had never been added to the View
    // menu.)
    check(invertAction_ != nullptr &&
              invertAction_->shortcut() == QKeySequence(Qt::Key_I),
          "the View menu offers invert on I");
    check(lcdAction_ != nullptr,
          "the View menu offers the subpixel-text toggle");
    check(invertAction_ == nullptr || invertAction_->isCheckable(),
          "the invert menu entry is a checkable toggle");

    if (report) *report = lines.join('\n');
    return ok;
}

bool MainWindow::selectLine(int pageIndex, int lineIndex) {
    if (!document_) return false;
    view_->setPage(pageIndex);   // make sure the line is on screen
    const bool ok = view_->selectLine(pageIndex, lineIndex);
    view_->setFocus();
    return ok;
}

void MainWindow::selectAllText() {
    if (!document_) return;
    view_->selectAll();
    view_->setFocus();
}

void MainWindow::startFind(const QString& needle) {
    if (!document_) return;
    showFindBar();
    findEdit_->setText(needle);   // fires runFind() through textChanged
}

void MainWindow::writeSessionState(int page, double zoom) {
    if (!document_) return;
    view_->setFitMode(PdfView::FitMode::None);
    view_->setZoom(zoom);
    if (page > 0) view_->setPage(page);
    saveCurrentState();
    app::Session::SetWindowGeometry(saveGeometry());
    app::Session::Flush();
}

bool MainWindow::verifySessionRestore(int wantPage, double wantZoom, QString* detail) {
    // Settle layout before observing a restored reading position.
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();

    const int    gotPage = view_->page();
    const double gotZoom = view_->zoom();
    const bool   pageOk  = (gotPage == wantPage);
    const bool   zoomOk  = std::fabs(gotZoom - wantZoom) < 0.02;
    const bool   fitOff  = view_->fitMode() == PdfView::FitMode::None;

    if (detail) {
        *detail = QString("page %1 (want %2), zoom %3 (want %4), fitMode=%5")
                      .arg(gotPage)
                      .arg(wantPage)
                      .arg(gotZoom, 0, 'f', 3)
                      .arg(wantZoom, 0, 'f', 3)
                      .arg(fitOff ? "off" : "ON");
    }
    return pageOk && zoomOk && fitOff;
}

void MainWindow::printDocument() {
    if (!document_ || !index_) return;

    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(QFileInfo(currentPath_).fileName());

    QPrintDialog dialog(&printer, this);
    dialog.setWindowTitle(tr("打印"));
    dialog.setOption(QAbstractPrintDialog::PrintPageRange, true);
    dialog.setOption(QAbstractPrintDialog::PrintCurrentPage, true);
    if (dialog.exec() != QDialog::Accepted) return;

    const int pageCount = index_->PageCount();
    int first = 0, last = pageCount - 1;
    const QPrinter::PrintRange range = printer.printRange();
    if (range == QPrinter::PageRange) {
        first = printer.fromPage() - 1;
        last  = printer.toPage() - 1;
    } else if (range == QPrinter::CurrentPage) {
        first = last = view_->page();
    }
    first = std::max(0, first);
    last  = std::min(pageCount - 1, std::max(first, last));

    if (printer.outputFormat() == QPrinter::PdfFormat) {
        // "Printing" to a file is really an export: keep it vectorial.
        const QString path = printer.outputFileName();
        if (!path.isEmpty() &&
            app::SavePagesToPdf(document_->handle(), pageCount, path, first, last)) {
            statusBar()->showMessage(tr("已导出第 %1–%2 页到 %3")
                                         .arg(first + 1).arg(last + 1).arg(path),
                                     6000);
            return;
        }
        // Fall through to raster printing if the vector path did not work.
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const app::PrintOutcome r =
        app::PrintPages(document_->handle(), pageCount, printer, first, last);
    QApplication::restoreOverrideCursor();

    if (r.aborted) {
        QMessageBox::warning(this, tr("打印失败"), tr("打印机未接受本次打印任务。"));
        return;
    }
    statusBar()->showMessage(tr("已打印 %1 页（第 %2–%3 页）")
                                 .arg(r.pagesPrinted)
                                 .arg(r.firstPage + 1)
                                 .arg(r.lastPage + 1),
                             6000);
}

QString MainWindow::runBenchmark() {
    QStringList out;
    if (!view_ || !index_) return QStringLiteral("no document");

    const int pageCount = index_->PageCount();
    out << QString("document      : %1 page(s)").arg(pageCount);

    view_->setFitMode(PdfView::FitMode::Width);
    QCoreApplication::processEvents();

    const double zoom = view_->zoom();
    const int vw = view_->viewport()->width();
    const int vh = view_->viewport()->height();
    out << QString("viewport      : %1 x %2 px").arg(vw).arg(vh);
    out << QString("fit-width zoom: %1  (%2%)").arg(zoom, 0, 'f', 3)
               .arg(static_cast<int>(zoom * 100));

    // How big is one cached page at this zoom, and how long does it take?
    double ptW = 0, ptH = 0;
    pre::PageSize(document_->handle(), 0, &ptW, &ptH);
    const int pxW = static_cast<int>(ptW * zoom);
    const int pxH = static_cast<int>(ptH * zoom);
    out << QString("page raster   : %1 x %2 px = %3 MB")
               .arg(pxW).arg(pxH)
               .arg(pxW * 4.0 * pxH / (1024.0 * 1024.0), 0, 'f', 1);
    out << QString("cache budget  : 10 pages = %1 MB")
               .arg(pxW * 4.0 * pxH * 10 / (1024.0 * 1024.0), 0, 'f', 1);

    // Cold render: the cost that shows up as a stall when a new page scrolls in.
    {
        QElapsedTimer t;
        t.start();
        pre::RenderedPage scratch;
        int done = 0;
        const int probe = std::min(5, pageCount);
        for (int p = 0; p < probe; ++p) {
            if (pre::RenderPage(document_->handle(), p, zoom, &scratch)) ++done;
        }
        const double ms = done > 0 ? static_cast<double>(t.elapsed()) / done : -1;
        out << QString("cold render   : %1 ms/page").arg(ms, 0, 'f', 1);
    }

    // Warm repaint: cached images blitted to the viewport.
    view_->goToFirstPage();
    view_->repaint();
    {
        QElapsedTimer t;
        t.start();
        constexpr int kRepaints = 10;
        for (int i = 0; i < kRepaints; ++i) view_->repaint();
        out << QString("repaint (warm): %1 ms/frame")
                   .arg(static_cast<double>(t.elapsed()) / kRepaints, 0, 'f', 1);
    }

    // Scrolling: each step moves one viewport height, so new pages come in cold.
    {
        QElapsedTimer t;
        t.start();
        const int step = std::max(80, vh / 2);
        constexpr int kSteps = 20;
        for (int i = 0; i < kSteps; ++i) {
            view_->verticalScrollBar()->setValue(view_->verticalScrollBar()->value() + step);
            view_->repaint();
        }
        out << QString("scroll        : %1 ms/step (step=%2 px, includes cold pages)")
                   .arg(static_cast<double>(t.elapsed()) / kSteps, 0, 'f', 1).arg(step);
    }

    // Zoom: every step invalidates the whole cache and re-renders.
    {
        QElapsedTimer t;
        t.start();
        constexpr int kZooms = 5;
        for (int i = 0; i < kZooms; ++i) {
            view_->setZoom(zoom * (1.0 + 0.05 * (i + 1)));
            view_->repaint();
        }
        out << QString("zoom          : %1 ms/step (cache dropped each time)")
                   .arg(static_cast<double>(t.elapsed()) / kZooms, 0, 'f', 1);
    }

    view_->setZoom(zoom);
    view_->goToFirstPage();
    return out.join(QLatin1Char('\n'));
}

QString MainWindow::benchmarkResize() {
    // Dragging a window edge in fit mode re-runs fit + relayout + a full cache
    // drop for every resize event, so this is the path most likely to feel
    // sticky. It is measured separately because it needs the real window.
    if (!view_) return QString();

    auto runResize = [this](int steps) {
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < steps; ++i) {
            resize(1200 + (i % 4) * 60, 820 + (i % 3) * 40);
            QCoreApplication::processEvents();
        }
        return static_cast<double>(t.elapsed()) / steps;
    };

    // A/B: with the fit mode off, resizeEvent does nothing but relayout the
    // window. The difference is what the fit path actually costs.
    const PdfView::FitMode saved = view_->fitMode();
    view_->setFitMode(PdfView::FitMode::None);
    const double plain = runResize(12);
    view_->setFitMode(saved);
    const double fitting = runResize(12);

    // Third A/B, on the sidebar as a whole. The wrapped status label is the
    // suspect: height-for-width labels can force repeated layout passes.
    QString sidebarNote = QStringLiteral("no sidebar");
    if (sideTabs_) {
        sideTabs_->hide();
        const double noSidebar = runResize(12);
        sideTabs_->show();
        sidebarNote = QString("sidebar costs %1 ms of the %2 ms")
                          .arg(fitting - noSidebar, 0, 'f', 1)
                          .arg(fitting, 0, 'f', 1);
    }

    return QString("resize        : %1 ms/step fitting, %2 ms/step plain window "
                   "relayout (fit path adds %3 ms)\n"
                   "                %4")
        .arg(fitting, 0, 'f', 1)
        .arg(plain, 0, 'f', 1)
        .arg(fitting - plain, 0, 'f', 1)
        .arg(sidebarNote);
}

int MainWindow::printToPdf(const QString& outPath, int firstPage, int lastPage,
                           QString* detail) {
    if (!document_ || !index_) return -1;

    const int pageCount = index_->PageCount();
    const int first = std::max(0, std::min(firstPage, pageCount - 1));
    const int last  = std::max(first, std::min(lastPage, pageCount - 1));

    // A file destination gets the vector path: the result keeps its text layer,
    // so it stays searchable and the reader's TOC engine still works on it.
    if (!app::SavePagesToPdf(document_->handle(), pageCount, outPath, first, last)) {
        if (detail) *detail = QStringLiteral("vector page export failed");
        return -1;
    }

    // Close the loop: reopen what we produced with the same reader, count its
    // pages, and confirm the text layer survived.
    pre::Document reopened;
    std::string    err;
    if (!reopened.Open(outPath.toLocal8Bit().constData(), &err)) {
        if (detail) {
            *detail = QString("produced file cannot be reopened: %1")
                          .arg(QString::fromStdString(err));
        }
        return -1;
    }
    const int found = FPDF_GetPageCount(reopened.handle());
    const int want  = last - first + 1;

    pre::PageIndex idx(reopened.handle());
    size_t textChars = 0;
    for (int p = 0; p < found; ++p) textChars += idx.Chars(p).size();

    if (detail) {
        *detail = QString("exported %1 page(s) [%2..%3]; reopened has %4 page(s), "
                          "%5 text characters")
                      .arg(want).arg(first + 1).arg(last + 1).arg(found)
                      .arg(textChars);
    }
    if (found != want) return -1;
    // A rasterised export would pass the page-count check but lose every
    // character; the text layer is what makes this the better path.
    if (textChars == 0) {
        if (detail) *detail += QStringLiteral("  [NO TEXT LAYER]");
        return -1;
    }
    return found;
}

void MainWindow::goToPageDialog() {
    if (!document_) return;
    bool ok = false;
    const int page = QInputDialog::getInt(this, tr("跳转到页"), tr("页码："),
                                          view_->page() + 1, 1, view_->pageCount(), 1, &ok);
    if (ok) view_->setPage(page - 1);
}

void MainWindow::saveTocAsJson() {
    if (!document_ || toc_.roots.empty()) {
        QMessageBox::information(this, tr("没有目录"), tr("当前文档没有可导出的目录。"));
        return;
    }
    const QString suggested =
        QFileInfo(currentPath_).completeBaseName() + QStringLiteral(".toc.json");
    const QString path = QFileDialog::getSaveFileName(this, tr("导出目录"), suggested,
                                                      tr("JSON (*.json)"));
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("写入失败"), file.errorString());
        return;
    }
    file.write(pre::TocToJson(toc_).c_str());
    statusBar()->showMessage(tr("目录已导出到 %1").arg(path), 4000);
}

void MainWindow::onOpen() {
    QString startDir = app::Session::LastOpenDir();
    if (startDir.isEmpty() || !QFileInfo::exists(startDir)) {
        startDir = currentPath_.isEmpty() ? QString()
                                          : QFileInfo(currentPath_).absolutePath();
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("打开 PDF"), startDir, tr("PDF 文档 (*.pdf);;所有文件 (*)"));
    if (!path.isEmpty()) openDocument(path);
}
