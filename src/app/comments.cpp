#include "comments.h"
#include "session.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSettings>
#include <algorithm>
#include <cmath>
#include "fpdf_annot.h"

namespace app {
namespace {
const QString kPrefix = QStringLiteral("PDFReader.Comment.");

QString ReadString(FPDF_ANNOTATION annot, const char* key) {
    const auto bytes = FPDFAnnot_GetStringValue(annot, key, nullptr, 0);
    if (bytes < 2 || bytes > 16 * 1024 * 1024 || bytes % 2) return {};
    std::vector<FPDF_WCHAR> buffer(bytes / 2);
    if (FPDFAnnot_GetStringValue(annot, key, buffer.data(), bytes) != bytes) return {};
    return QString::fromUtf16(reinterpret_cast<const char16_t*>(buffer.data()), buffer.size() - 1);
}

bool SetString(FPDF_ANNOTATION annot, const char* key, const QString& value) {
    return FPDFAnnot_SetStringValue(annot, key,
        reinterpret_cast<FPDF_WIDESTRING>(value.utf16())) != 0;
}

bool SetContents(FPDF_ANNOTATION annot, const Comment& comment) {
    return SetString(annot, "Contents", comment.text) &&
        SetString(annot, "M", comment.modified.toUTC().toString("'D:'yyyyMMddHHmmss'Z'")) &&
        SetString(annot, "PDFReaderModified", comment.modified.toString(Qt::ISODateWithMs));
}

QString KeyFor(const QString& path) {
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    QString normalized = QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
#ifdef Q_OS_WIN
    normalized = normalized.toCaseFolded();
#endif
    return "comments/" + QString::fromLatin1(QCryptographicHash::hash(
        normalized.toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool ValidRect(const pre::MarkupRect& r) {
    return std::isfinite(r.left) && std::isfinite(r.bottom) &&
        std::isfinite(r.right) && std::isfinite(r.top) &&
        r.right > r.left && r.top > r.bottom;
}
} // namespace

QVector<Comment> ReadPdfComments(FPDF_DOCUMENT doc) {
    QVector<Comment> result;
    if (!doc) return result;
    for (int p = 0; p < FPDF_GetPageCount(doc); ++p) {
        FPDF_PAGE page = FPDF_LoadPage(doc, p);
        if (!page) continue;
        for (int i = 0; i < FPDFPage_GetAnnotCount(page); ++i) {
            FPDF_ANNOTATION annot = FPDFPage_GetAnnot(page, i);
            if (!annot) continue;
            const QString name = ReadString(annot, "NM");
            if (name.startsWith(kPrefix) && name.size() > kPrefix.size() &&
                FPDFAnnot_GetSubtype(annot) == FPDF_ANNOT_HIGHLIGHT) {
                const QString id = name.mid(kPrefix.size());
                auto found = std::find_if(result.begin(), result.end(),
                    [&](const Comment& c) { return c.id == id; });
                if (found == result.end()) {
                    Comment c;
                    c.id = id;
                    c.text = ReadString(annot, "Contents");
                    c.quote = ReadString(annot, "PDFReaderQuote");
                    c.created = QDateTime::fromString(ReadString(annot, "PDFReaderCreated"), Qt::ISODateWithMs);
                    c.modified = QDateTime::fromString(ReadString(annot, "PDFReaderModified"), Qt::ISODateWithMs);
                    result.push_back(c);
                    found = result.end() - 1;
                }
                for (size_t q = 0; q < FPDFAnnot_CountAttachmentPoints(annot); ++q) {
                    FS_QUADPOINTSF quad;
                    if (!FPDFAnnot_GetAttachmentPoints(annot, q, &quad)) continue;
                    pre::MarkupRect r{
                        std::min({quad.x1, quad.x2, quad.x3, quad.x4}),
                        std::min({quad.y1, quad.y2, quad.y3, quad.y4}),
                        std::max({quad.x1, quad.x2, quad.x3, quad.x4}),
                        std::max({quad.y1, quad.y2, quad.y3, quad.y4})};
                    if (ValidRect(r)) found->anchors[p].push_back(r);
                }
            }
            FPDFPage_CloseAnnot(annot);
        }
        FPDF_ClosePage(page);
    }
    result.erase(std::remove_if(result.begin(), result.end(),
        [](const Comment& c) { return c.anchors.empty(); }), result.end());
    return result;
}

bool AddPdfComment(FPDF_DOCUMENT doc, const Comment& comment) {
    if (!doc || comment.id.isEmpty() || comment.anchors.empty()) return false;
    bool ok = true;
    for (const auto& [p, rects] : comment.anchors) {
        if (p < 0 || p >= FPDF_GetPageCount(doc) || rects.empty() ||
            !std::all_of(rects.begin(), rects.end(), ValidRect)) { ok = false; break; }
        FPDF_PAGE page = FPDF_LoadPage(doc, p);
        if (!page) { ok = false; break; }
        FPDF_ANNOTATION annot = FPDFPage_CreateAnnot(page, FPDF_ANNOT_HIGHLIGHT);
        if (annot) {
            auto bounds = rects.front();
            for (const auto& r : rects) {
                bounds.left = std::min(bounds.left, r.left);
                bounds.bottom = std::min(bounds.bottom, r.bottom);
                bounds.right = std::max(bounds.right, r.right);
                bounds.top = std::max(bounds.top, r.top);
            }
            const FS_RECTF box{float(bounds.left), float(bounds.top), float(bounds.right), float(bounds.bottom)};
            ok = FPDFAnnot_SetRect(annot, &box) &&
                FPDFAnnot_SetColor(annot, FPDFANNOT_COLORTYPE_Color, 255, 214, 80, 100) &&
                FPDFAnnot_SetFlags(annot, FPDF_ANNOT_FLAG_PRINT) &&
                SetString(annot, "NM", kPrefix + comment.id) &&
                SetString(annot, "T", QString::fromUtf8("我")) &&
                SetString(annot, "Subj", QString::fromUtf8("阅读批注")) &&
                SetString(annot, "PDFReaderQuote", comment.quote) &&
                SetString(annot, "PDFReaderCreated", comment.created.toString(Qt::ISODateWithMs)) &&
                SetContents(annot, comment);
            for (const auto& r : rects) {
                // PDF text markup uses top-left, top-right, bottom-left,
                // bottom-right order, independently for each selected line.
                const FS_QUADPOINTSF quad{float(r.left), float(r.top), float(r.right), float(r.top),
                    float(r.left), float(r.bottom), float(r.right), float(r.bottom)};
                if (!FPDFAnnot_AppendAttachmentPoints(annot, &quad)) ok = false;
            }
            if (!ok) {
                const int index = FPDFPage_GetAnnotIndex(page, annot);
                FPDFPage_CloseAnnot(annot);
                FPDFPage_RemoveAnnot(page, index);
            } else FPDFPage_CloseAnnot(annot);
        } else ok = false;
        FPDF_ClosePage(page);
        if (!ok) break;
    }
    if (!ok) RemovePdfComment(doc, comment); // roll back a partial cross-page add
    return ok;
}

bool UpdatePdfComment(FPDF_DOCUMENT doc, const Comment& comment) {
    if (!doc) return false;
    bool ok = true;
    for (const auto& [p, rects] : comment.anchors) {
        FPDF_PAGE page = FPDF_LoadPage(doc, p);
        if (!page) { ok = false; continue; }
        bool found = false;
        for (int i = 0; i < FPDFPage_GetAnnotCount(page); ++i) {
            FPDF_ANNOTATION annot = FPDFPage_GetAnnot(page, i);
            if (!annot) continue;
            if (ReadString(annot, "NM") == kPrefix + comment.id) {
                found = true;
                if (!SetContents(annot, comment)) ok = false;
            }
            FPDFPage_CloseAnnot(annot);
        }
        if (!found) ok = false;
        FPDF_ClosePage(page);
    }
    return ok;
}

bool RemovePdfComment(FPDF_DOCUMENT doc, const Comment& comment) {
    if (!doc) return false;
    bool ok = true;
    for (const auto& [p, rects] : comment.anchors) {
        FPDF_PAGE page = FPDF_LoadPage(doc, p);
        if (!page) { ok = false; continue; }
        for (int i = FPDFPage_GetAnnotCount(page) - 1; i >= 0; --i) {
            FPDF_ANNOTATION annot = FPDFPage_GetAnnot(page, i);
            if (!annot) continue;
            const bool ours = ReadString(annot, "NM") == kPrefix + comment.id;
            FPDFPage_CloseAnnot(annot);
            if (ours && !FPDFPage_RemoveAnnot(page, i)) ok = false;
        }
        FPDF_ClosePage(page);
    }
    return ok;
}

std::optional<QVector<Comment>> LoadLocalComments(const QString& path, int pageCount) {
    QSettings settings(Session::SettingsPath(), QSettings::IniFormat);
    settings.beginGroup(KeyFor(path));
    const QFileInfo file(path);
    if (settings.value("size", -1).toLongLong() != file.size() ||
        settings.value("modified", -1).toLongLong() != file.lastModified().toMSecsSinceEpoch()) return std::nullopt;
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(settings.value("data").toByteArray(), &error);
    if (error.error != QJsonParseError::NoError || !json.isArray()) return std::nullopt;
    QVector<Comment> result;
    QSet<QString> ids;
    for (const auto& value : json.array()) {
        if (!value.isObject()) return std::nullopt;
        const auto o = value.toObject();
        Comment c;
        c.id = o["id"].toString(); c.quote = o["quote"].toString(); c.text = o["text"].toString();
        c.created = QDateTime::fromString(o["created"].toString(), Qt::ISODateWithMs);
        c.modified = QDateTime::fromString(o["modified"].toString(), Qt::ISODateWithMs);
        if (c.id.isEmpty() || ids.contains(c.id) || !o["anchors"].isArray()) return std::nullopt;
        ids.insert(c.id);
        for (const auto& anchor : o["anchors"].toArray()) {
            const auto a = anchor.toArray();
            if (a.size() != 5 || !std::all_of(a.begin(), a.end(),
                [](const QJsonValue& v) { return v.isDouble(); })) return std::nullopt;
            const int p = a[0].toInt(-1);
            const pre::MarkupRect r{a[1].toDouble(), a[2].toDouble(), a[3].toDouble(), a[4].toDouble()};
            if (p < 0 || p >= pageCount || !ValidRect(r)) return std::nullopt;
            c.anchors[p].push_back(r);
        }
        if (c.anchors.empty()) return std::nullopt;
        result.push_back(c);
    }
    return result;
}

bool SaveLocalComments(const QString& path, const QVector<Comment>& comments, qint64 sourceSize, qint64 sourceModified) {
    if (path.isEmpty()) return false;
    QJsonArray data;
    for (const auto& c : comments) {
        QJsonArray anchors;
        for (const auto& [page, rects] : c.anchors)
            for (const auto& r : rects) anchors.append(QJsonArray{page, r.left, r.bottom, r.right, r.top});
        data.append(QJsonObject{{"id", c.id}, {"quote", c.quote}, {"text", c.text},
            {"created", c.created.toString(Qt::ISODateWithMs)},
            {"modified", c.modified.toString(Qt::ISODateWithMs)}, {"anchors", anchors}});
    }
    QSettings settings(Session::SettingsPath(), QSettings::IniFormat);
    settings.beginGroup(KeyFor(path));
    const QFileInfo file(path);
    settings.setValue("size", sourceSize < 0 ? file.size() : sourceSize);
    settings.setValue("modified", sourceModified < 0 ? file.lastModified().toMSecsSinceEpoch() : sourceModified);
    settings.setValue("data", QJsonDocument(data).toJson(QJsonDocument::Compact));
    settings.endGroup();
    settings.sync();
    return settings.status() == QSettings::NoError;
}
} // namespace app
