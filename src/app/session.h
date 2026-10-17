// Persisted per-user state.
//
// Everything durable lives in one INI file: where each document was last read,
// the recent-file list, and the window geometry. Keeping it in one place with
// an overridable path lets the tests drive the real persistence code against a
// scratch file instead of the user's live settings.
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace app {

struct Bookmark {
    int page = 0;
    QString title;
};

struct DocState {
    int    page     = 0;
    double zoom     = 1.0;
    // 0 = none, 1 = fit width, 2 = fit page (mirrors PdfView::FitMode).
    int    fitMode  = 1;
    // 0 = continuous, 1 = single page (mirrors PdfView::ViewMode).
    int    viewMode = 0;
    bool   invert   = false;
    bool   valid    = false;   // false when nothing was stored for this document
};

class Session {
public:
    // $PDFREADER_SETTINGS when set (tests), otherwise the per-user config dir.
    static QString SettingsPath();

    static DocState LoadDocState(const QString& path);
    static void     SaveDocState(const QString& path, const DocState& state);
    static QVector<Bookmark> LoadBookmarks(const QString& path);
    static void SaveBookmarks(const QString& path, const QVector<Bookmark>& bookmarks);

    static QStringList RecentFiles();
    static void        AddRecentFile(const QString& path, int maxEntries = 12);
    static void        RemoveRecentFile(const QString& path);
    static void        ClearRecentFiles();

    static QString LastClosedFile();
    static void    SetLastClosedFile(const QString& path);

    static QString LastOpenDir();
    static void    SetLastOpenDir(const QString& dir);

    // Documents that were open when the app last exited, so the next launch can
    // bring the same set back.
    static QStringList OpenTabs();
    static void        SetOpenTabs(const QStringList& paths);

    static QByteArray WindowGeometry();
    static void       SetWindowGeometry(const QByteArray& geometry);
    static QByteArray SidebarState();
    static void SetSidebarState(const QByteArray& state);

    // Force the INI to disk. QSettings buffers, so a test that writes and then
    // inspects the file needs this.
    static void Flush();

private:
    // One stable group per document. The path itself cannot be a group name
    // (slashes, length), so it is hashed and the real path stored alongside.
    static QString KeyFor(const QString& path);
};

}  // namespace app
