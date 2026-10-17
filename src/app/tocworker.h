#pragma once

#include <QByteArray>
#include <QString>
#include "toc.h"

namespace app {

// The worker runs in its own process: PDFium explicitly forbids concurrent
// calls from UI and background threads, even with separate documents.
int RunTocWorker(const QString& path);
QString TocCacheKey(const QString& path);
bool DecodeToc(const QByteArray& json, int pageCount, pre::TocTree* tree,
               pre::ResolveStats* stats);
bool LoadTocCache(const QString& key, int pageCount, pre::TocTree* tree,
                  pre::ResolveStats* stats);
void SaveTocCache(const QString& key, const QByteArray& json);

} // namespace app
