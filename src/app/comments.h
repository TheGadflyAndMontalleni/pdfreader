#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>
#include <optional>
#include <map>
#include "annot.h"

namespace app {

struct Comment {
    QString id;
    QString quote;
    QString text;
    QDateTime created;
    QDateTime modified;
    std::map<int, std::vector<pre::MarkupRect>> anchors;
};

// Each page of a comment is a standard PDF highlight with /Contents and
// QuadPoints. Only annotations carrying our private name prefix are managed;
// annotations authored by other applications are left alone.
QVector<Comment> ReadPdfComments(FPDF_DOCUMENT doc);
bool AddPdfComment(FPDF_DOCUMENT doc, const Comment& comment);
bool UpdatePdfComment(FPDF_DOCUMENT doc, const Comment& comment);
bool RemovePdfComment(FPDF_DOCUMENT doc, const Comment& comment);

// A complete local snapshot, including an empty one after deleting everything.
// The file stamp prevents old anchors from being applied to a replaced PDF.
std::optional<QVector<Comment>> LoadLocalComments(const QString& path, int pageCount);
bool SaveLocalComments(const QString& path, const QVector<Comment>& comments,
    qint64 sourceSize = -1, qint64 sourceModified = -1);

} // namespace app
