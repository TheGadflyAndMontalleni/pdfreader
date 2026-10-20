// Main window: automatic table-of-contents sidebar + page view.
#pragma once

#include <QMainWindow>
#include <QString>
#include <QSet>

#include <memory>

#include "doc.h"
#include "toc.h"
#include "session.h"
#include "comments.h"
#include "pdfreader_plugin.h"

namespace app { class PluginManager; }

class QAction;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;
class QProcess;
class QSplitter;
class QTabBar;
class QTabWidget;
class QToolBar;
class QTreeWidget;
class QTreeWidgetItem;
class PdfView;
class CommentPanel;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr, bool autoLoadPlugins = true);
    ~MainWindow() override;

    bool openDocument(const QString& path);
    bool waitForTocs(int timeoutMs = 30000); // verification only; UI never waits
    bool addBookmark(const QString& title = QString());
    bool renameBookmark(int row, const QString& title);
    bool removeBookmark(int row);
    int bookmarkCount() const;
    bool jumpToBookmark(int row);
    app::PluginManager* pluginManager() const { return plugins_.get(); }
    pdfreader::DocumentInfo pluginDocument() const;
    QString pluginSelectedText() const;
    QString pluginPageText(int pageNumber) const;
    bool addComment(const QString& text = QString());
    bool editComment(const QString& id, const QString& text);
    bool removeComment(const QString& id);
    bool activateComment(const QString& id, bool navigate = true, bool focusEditor = false);
    QVector<app::Comment> comments() const;
    bool saveDocumentToPath(const QString& path);

    // Headless checks for the parts that matter: the TOC is built, every entry
    // has a target, and clicking an entry actually moves the view there.
    bool runSelfTest(QString* report);

    // Verification aid: scroll to a page and select one whole line, so the
    // selection highlight can be inspected visually.
    bool selectLine(int pageIndex, int lineIndex);
    void selectAllText();
    // Verification aid: open the find bar pre-filled, as if the user typed it.
    void startFind(const QString& needle);

    // Verification aids for session persistence.
    void writeSessionState(int page, double zoom);
    // Returns true when the document was restored to the expected position and
    // zoom; fills `detail` with what was actually observed.
    bool verifySessionRestore(int wantPage, double wantZoom, QString* detail);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onOpen();
    void onTocItemClicked(QTreeWidgetItem* item, int column);
    void onPageChanged(int page);
    void onZoomChanged(double zoom);
    void fitWidth();
    void applyZoom(double factor);
    void goToPageDialog();
    void saveTocAsJson();
    void printDocument();
    void onNextPage();
    void onPrevPage();
    void onFirstPage();
    void onLastPage();
    void showFindBar();
    void closeFindBar();
    void runFind();
    void findNext();
    void findPrev();
    void onTabChanged(int index);
    void onTabCloseRequested(int index);
    void nextTab();
    void prevTab();

private:
    struct DocTab;
    void buildUi(bool autoLoadPlugins);
    void populateToc(const pre::TocTree& tree, const pre::ResolveStats& stats);
    void setTocStatus(const QString& text, bool warning);
    void updateStatus();
    // Persist page / zoom / fit mode for the document currently open.
    void saveCurrentState();
    void rebuildRecentMenu();
    void openRecent(const QString& path);
    void refreshBookmarks();
    void editSelectedBookmark();
    void startNextToc();
    void finishToc(quint64 documentId, QProcess* worker, int exitCode);
    void refreshToc();
    void stopToc(DocTab* entry);
    void refreshComments();
    bool flushComments();

    // Declared before the documents so the PDFium library outlives them.
    pre::Library                    library_;

    // One open document. The parsed document, its text index and its TOC stay
    // alive per tab so switching tabs never re-parses the PDF.
    struct DocTab {
        QString                         path;
        std::unique_ptr<pre::Document>  doc;
        std::unique_ptr<pre::PageIndex> index;
        pre::TocTree                    toc;
        pre::ResolveStats               stats;
        quint64                         id = 0;
        bool                            tocPending = false;
        bool                            tocFailed = false;
        QProcess*                       tocProcess = nullptr;
        QString                         tocCacheKey;
        bool                            tocStateSaved = false;
        QSet<QString>                   expandedToc;
        QVector<app::Bookmark>          bookmarks;
        QVector<app::Comment>           comments;
        QString                        activeComment;
        bool                           commentsDirty = false;
        qint64                         commentSourceSize = 0;
        qint64                         commentSourceModified = 0;
        // Reading position and view configuration, persisted on switch and exit.
        // The mode enums are stored as ints so this header needs no Qt widget
        // types (PdfView is only forward-declared here).
        int    page     = 0;
        double zoom     = 1.0;
        int    fitMode  = 1;   // PdfView::FitMode:   0 none, 1 width, 2 page
        int    viewMode = 0;   // PdfView::ViewMode:  0 continuous, 1 single
        bool   invert   = false;
    };

    DocTab* tab(int i) const;
    DocTab* cur() const;
    int     tabCount() const { return static_cast<int>(tabs_.size()); }
    void    activateTab(int index);          // stash the old state, bind the new
    void    bindTab(int index);
    void    closeTab(int index);
    void    stashActiveState();              // view state -> active DocTab
    void    refreshTabTitle(int index);

    std::vector<std::unique_ptr<DocTab>> tabs_;
    int activeTab_ = -1;
    quint64 nextDocumentId_ = 0;
    bool closing_ = false;

    // Borrowed pointers into the active tab, so the per-feature code below
    // reads the same whether or not tabs exist. `toc_` is the active tab's tree
    // by value: it is only hundreds of nodes and the sidebar is built from it.
    pre::Document*  document_ = nullptr;
    pre::PageIndex* index_ = nullptr;
    pre::TocTree    toc_;
    QString         currentPath_;

    // Extra verification entry points used by --selftest-tabs.
public:
    int  docTabCount() const { return tabCount(); }
    bool activateDocTab(int index);
    int  tabPage(int index) const;
    int  tabPageCount(int index) const;
    QString tabTitle(int index) const;
    bool closeDocTab(int index) { closeTab(index); return true; }
    bool    setActivePage(int page);
    int     tabTocNodes(int index) const;
    QString tabTocSource(int index) const;
    // View configuration of the active tab, as ints (see PdfView enums).
    int  activeViewMode() const;
    int  activeFitMode() const;
    bool setActiveViewMode(int mode);
    bool setActiveFitMode(int mode);
    int  activePageCount() const;
    bool activeInvert() const;
    // Mean luminance of what is currently on screen, 0..255. Used to prove the
    // invert toggle actually reaches the pixels.
    double viewportLuminance();
    void showSidebarPage(int index);
    void toggleInvert();
    // Markup over the current selection: 0 highlight, 1 underline, 2 strikeout.
    void applyMarkup(int kind);
    void undoMarkup();
    void saveDocumentAs();
    // Pixels in the viewport that look like a highlighter mark. Used to prove
    // the markup is actually visible rather than merely recorded.
    int viewportMarkupPixels();
    // The viewport as an image, so a test can compare before against after
    // instead of guessing at a colour threshold.
    QImage viewportImage();
    // Reopen the documents that were open when the app last exited.
    void restoreSessionTabs();

    // Time the operations a reader actually performs: rendering a page,
    // repainting the viewport, scrolling and zooming. Returns a text report.
    QString runBenchmark();
    // Resizing needs the real top-level window, so it is timed separately.
    QString benchmarkResize();

    // Verification aid: print a page range straight to a PDF file (no dialog)
    // and reopen the result to count its pages. Returns the page count, or -1.
    int printToPdf(const QString& outPath, int firstPage, int lastPage, QString* detail);

private:

    QTreeWidget*   tocTree_ = nullptr;
    QLabel*        tocStatus_ = nullptr;
    PdfView*       view_ = nullptr;
    QLabel*        pageLabel_ = nullptr;
    QSplitter*     splitter_ = nullptr;
    QSplitter*     readingSplitter_ = nullptr;
    CommentPanel*  commentPanel_ = nullptr;
    QAction*       newCommentAction_ = nullptr;
    QAction*       showCommentsAction_ = nullptr;
    QTimer*        commentSaveTimer_ = nullptr;
    QTabBar*       tabBar_ = nullptr;
    QTabWidget*    sideTabs_ = nullptr;
    QTreeWidget*   bookmarkTree_ = nullptr;
    QLabel*        bookmarkEmpty_ = nullptr;
    QAction*       addBookmarkAction_ = nullptr;
    QAction*       renameBookmarkAction_ = nullptr;
    QAction*       removeBookmarkAction_ = nullptr;
    QLineEdit*     pageEdit_ = nullptr;
    QLabel*        pageCountLabel_ = nullptr;
    QComboBox*     zoomBox_ = nullptr;
    QAction*       continuousAction_ = nullptr;
    QAction*       fitWidthAction_ = nullptr;
    QAction*       fitPageAction_ = nullptr;
    QAction*       copyAction_ = nullptr;
    QAction*       invertAction_ = nullptr;
    QAction*       lcdAction_ = nullptr;
    QMenu*         recentMenu_ = nullptr;
    QMenu*         markupMenu_ = nullptr;
    QToolBar*      findBar_ = nullptr;
    QLineEdit*     findEdit_ = nullptr;
    QCheckBox*     findCase_ = nullptr;
    QLabel*        findStatus_ = nullptr;
    std::unique_ptr<app::PluginManager> plugins_;
};
