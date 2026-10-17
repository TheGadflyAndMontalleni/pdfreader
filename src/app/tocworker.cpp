#include "tocworker.h"
#include "doc.h"
#include "session.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cstdio>
#include <functional>

namespace app {
namespace {
pre::TocSource Source(const QString& name) {
    if (name == "embedded-outline") return pre::TocSource::EmbeddedOutline;
    if (name == "printed-toc-page") return pre::TocSource::PrintedTocPage;
    if (name == "manual") return pre::TocSource::Manual;
    return pre::TocSource::Typography;
}

QString CachePath(const QString& key) {
    return QFileInfo(Session::SettingsPath()).absolutePath() + "/toc-cache/" + key + ".json";
}
} // namespace

QString TocCacheKey(const QString& path) {
    const QFileInfo info(path);
    const QByteArray fingerprint = info.canonicalFilePath().toUtf8() + '\0' +
        QByteArray::number(info.size()) + ':' +
        QByteArray::number(info.lastModified().toMSecsSinceEpoch()) + ":toc-v1";
    return QString::fromLatin1(QCryptographicHash::hash(fingerprint,
        QCryptographicHash::Sha256).toHex());
}

bool DecodeToc(const QByteArray& json, int pageCount, pre::TocTree* tree,
               pre::ResolveStats* stats) {
    if (json.isEmpty() || json.size() > 8 * 1024 * 1024) return false;
    const auto document = QJsonDocument::fromJson(json);
    if (!document.isObject()) return false;
    const auto root = document.object();
    if (root.value("version").toInt() != 1 || !root.value("roots").isArray()) return false;
    pre::TocTree result;
    result.source = Source(root.value("source").toString());
    int count = 0;
    std::function<bool(const QJsonArray&, std::vector<pre::TocNode>*, int)> read;
    read = [&](const QJsonArray& items, std::vector<pre::TocNode>* nodes, int depth) {
        if (depth > 32) return false;
        for (const auto& value : items) {
            if (!value.isObject() || ++count > 20000) return false;
            const auto o = value.toObject();
            pre::TocNode n;
            n.title = o.value("title").toString().toStdString();
            n.level = depth;
            n.targetPage = o.value("targetPage").toInt(-1);
            n.printedPage = o.value("printedPage").toInt(-1);
            n.source = Source(o.value("source").toString());
            n.confidence = o.value("confidence").toDouble();
            if (n.title.empty() || n.targetPage < -1 || n.targetPage >= pageCount) return false;
            if (!read(o.value("children").toArray(), &n.children, depth + 1)) return false;
            nodes->push_back(std::move(n));
        }
        return true;
    };
    if (!read(root.value("roots").toArray(), &result.roots, 1)) return false;
    *tree = std::move(result);
    *stats = pre::ResolveStats{};
    stats->titleHitRate = root.value("titleHitRate").toDouble();
    return true;
}

bool LoadTocCache(const QString& key, int pageCount, pre::TocTree* tree,
                  pre::ResolveStats* stats) {
    QFile file(CachePath(key));
    return file.open(QIODevice::ReadOnly) && file.size() <= 8 * 1024 * 1024 &&
           DecodeToc(file.readAll(), pageCount, tree, stats);
}

void SaveTocCache(const QString& key, const QByteArray& json) {
    const QString path = CachePath(key);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return;
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly) && file.write(json) == json.size()) file.commit();
}

int RunTocWorker(const QString& path) {
    pre::Library library;
    pre::Document document;
    std::string error;
    if (!document.Open(path.toUtf8().constData(), &error)) return 1;
    pre::PageIndex index(document.handle());
    pre::ResolveStats stats;
    const auto tree = pre::BuildAutoToc(index, pre::TocOptions{}, &stats);
    auto json = QJsonDocument::fromJson(QByteArray::fromStdString(pre::TocToJson(tree))).object();
    json.insert("version", 1);
    json.insert("titleHitRate", stats.titleHitRate);
    const QByteArray bytes = QJsonDocument(json).toJson(QJsonDocument::Compact);
    std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stdout);
    std::fflush(stdout);
    return 0;
}
} // namespace app
