#include "session.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <algorithm>

namespace app {

namespace {

constexpr const char* kRecentKey   = "recent/files";
constexpr const char* kLastClosed  = "recent/lastClosed";
constexpr const char* kLastOpenDir = "recent/lastOpenDir";
constexpr const char* kGeometry    = "window/geometry";
constexpr const char* kOpenTabs    = "session/openTabs";

// Absolute, canonical where possible: the same file opened through different
// paths must map to the same saved state.
QString NormalizePath(const QString& path) {
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    const QString abs = canonical.isEmpty() ? info.absoluteFilePath() : canonical;
    return QDir::cleanPath(abs);
}

}  // namespace

QString Session::SettingsPath() {
    const QByteArray override = qgetenv("PDFREADER_SETTINGS");
    if (!override.isEmpty()) return QString::fromLocal8Bit(override);

    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty()) dir = QDir::homePath() + "/.pdfreader";
    return dir + "/settings.ini";
}

QString Session::KeyFor(const QString& path) {
    const QByteArray digest =
        QCryptographicHash::hash(NormalizePath(path).toUtf8(), QCryptographicHash::Sha1)
            .toHex();
    return QStringLiteral("doc/") + QString::fromLatin1(digest);
}

DocState Session::LoadDocState(const QString& path) {
    DocState st;
    if (path.isEmpty()) return st;

    QSettings s(SettingsPath(), QSettings::IniFormat);
    const QString key = KeyFor(path);
    s.beginGroup(key);
    // The group may have been pruned or never written; QSettings returns
    // invalid QVariants then, which the defaults below cover.
    if (!s.contains("page")) {
        s.endGroup();
        return st;
    }
    st.page     = s.value("page", 0).toInt();
    st.zoom     = s.value("zoom", 1.0).toDouble();
    // `fitWidth` was the original boolean form; keep reading it so settings
    // written by an earlier build still restore sensibly.
    if (s.contains("fitMode")) {
        st.fitMode = s.value("fitMode", 1).toInt();
    } else {
        st.fitMode = s.value("fitWidth", true).toBool() ? 1 : 0;
    }
    st.viewMode = s.value("viewMode", 0).toInt();
    st.invert   = s.value("invert", false).toBool();
    st.locator  = s.value("epubLocator").toString();
    st.valid    = true;
    s.endGroup();
    return st;
}

void Session::SaveDocState(const QString& path, const DocState& state) {
    if (path.isEmpty()) return;

    QSettings s(SettingsPath(), QSettings::IniFormat);
    const QString key = KeyFor(path);
    s.beginGroup(key);
    s.setValue("path", NormalizePath(path));   // debuggability / future pruning
    s.setValue("page", state.page);
    s.setValue("zoom", state.zoom);
    s.setValue("fitMode", state.fitMode);
    s.setValue("viewMode", state.viewMode);
    s.setValue("invert", state.invert);
    if (!state.locator.isEmpty()) s.setValue("epubLocator", state.locator);
    s.endGroup();
    s.sync();
}

QStringList Session::RecentFiles() {
    QSettings s(SettingsPath(), QSettings::IniFormat);
    QStringList list = s.value(kRecentKey).toStringList();

    // Drop entries whose file no longer exists: a stale recent list is worse
    // than a short one.
    QStringList live;
    for (const QString& p : list) {
        if (QFileInfo::exists(p)) live << p;
    }
    return live;
}

QVector<Bookmark> Session::LoadBookmarks(const QString& path) {
    QSettings s(SettingsPath(), QSettings::IniFormat);
    QVector<Bookmark> bookmarks;
    const int count = std::min(10000, s.beginReadArray(KeyFor(path) + "/bookmarks"));
    for (int i = 0; i < count; ++i) {
        s.setArrayIndex(i);
        const int page = s.value("page", -1).toInt();
        const QString title = s.value("title").toString().trimmed();
        if (page >= 0 && !title.isEmpty()) bookmarks.push_back({page, title, s.value("locator").toString()});
    }
    s.endArray();
    return bookmarks;
}

void Session::SaveBookmarks(const QString& path, const QVector<Bookmark>& bookmarks) {
    if (path.isEmpty()) return;
    QSettings s(SettingsPath(), QSettings::IniFormat);
    const QString key = KeyFor(path) + "/bookmarks";
    s.remove(key);
    s.beginWriteArray(key, bookmarks.size());
    for (int i = 0; i < bookmarks.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue("page", bookmarks[i].page);
        s.setValue("title", bookmarks[i].title);
        if (!bookmarks[i].locator.isEmpty()) s.setValue("locator", bookmarks[i].locator);
    }
    s.endArray();
    s.sync();
}

void Session::AddRecentFile(const QString& path, int maxEntries) {
    if (path.isEmpty()) return;
    const QString norm = NormalizePath(path);

    QSettings s(SettingsPath(), QSettings::IniFormat);
    QStringList list = s.value(kRecentKey).toStringList();
    list.removeAll(norm);
    list.prepend(norm);
    while (list.size() > maxEntries) list.removeLast();
    s.setValue(kRecentKey, list);
    s.sync();
}

void Session::RemoveRecentFile(const QString& path) {
    QSettings s(SettingsPath(), QSettings::IniFormat);
    QStringList list = s.value(kRecentKey).toStringList();
    list.removeAll(NormalizePath(path));
    s.setValue(kRecentKey, list);
    s.sync();
}

void Session::ClearRecentFiles() {
    QSettings s(SettingsPath(), QSettings::IniFormat);
    s.remove(kRecentKey);
    s.sync();
}

QString Session::LastClosedFile() {
    QSettings s(SettingsPath(), QSettings::IniFormat);
    const QString p = s.value(kLastClosed).toString();
    return QFileInfo::exists(p) ? p : QString();
}

void Session::SetLastClosedFile(const QString& path) {
    QSettings s(SettingsPath(), QSettings::IniFormat);
    s.setValue(kLastClosed, NormalizePath(path));
    s.sync();
}

QString Session::LastOpenDir() {
    QSettings s(SettingsPath(), QSettings::IniFormat);
    return s.value(kLastOpenDir).toString();
}

void Session::SetLastOpenDir(const QString& dir) {
    QSettings s(SettingsPath(), QSettings::IniFormat);
    s.setValue(kLastOpenDir, dir);
    s.sync();
}

QStringList Session::OpenTabs() {
    QSettings s(SettingsPath(), QSettings::IniFormat);
    QStringList list = s.value(kOpenTabs).toStringList();
    // Only hand back documents that are still there; a missing one would just
    // produce an error tab on every launch.
    QStringList live;
    for (const QString& p : list) {
        if (QFileInfo::exists(p)) live << p;
    }
    return live;
}

void Session::SetOpenTabs(const QStringList& paths) {
    QSettings s(SettingsPath(), QSettings::IniFormat);
    QStringList norm;
    for (const QString& p : paths) norm << NormalizePath(p);
    s.setValue(kOpenTabs, norm);
    s.sync();
}

QByteArray Session::WindowGeometry() {
    QSettings s(SettingsPath(), QSettings::IniFormat);
    return s.value(kGeometry).toByteArray();
}

void Session::SetWindowGeometry(const QByteArray& geometry) {
    QSettings s(SettingsPath(), QSettings::IniFormat);
    s.setValue(kGeometry, geometry);
    s.sync();
}

QByteArray Session::SidebarState() {
    QSettings s(SettingsPath(), QSettings::IniFormat);
    return s.value("window/sidebar").toByteArray();
}

void Session::SetSidebarState(const QByteArray& state) {
    QSettings s(SettingsPath(), QSettings::IniFormat);
    s.setValue("window/sidebar", state);
    s.sync();
}

void Session::Flush() {
    QSettings s(SettingsPath(), QSettings::IniFormat);
    s.sync();
}

}  // namespace app
