#include "printing.h"

#include <QFile>
#include <QImage>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>

#include "render.h"

// The vector export path needs PDFium APIs beyond fpdfview.h:
// CreateNewDocument (fpdf_edit.h), ImportPages (fpdf_ppo.h) and SaveAsCopy
// (fpdf_save.h).
#include "fpdf_edit.h"
#include "fpdf_ppo.h"
#include "fpdf_save.h"

namespace app {

namespace {

// PDFium writes the saved document through a caller-supplied callback. The
// context embeds FPDF_FILEWRITE as its first member so the callback can recover
// the stream by casting the pointer it is handed.
struct FileWriteCtx {
    FPDF_FILEWRITE base;
    std::ofstream* out = nullptr;
};

int WriteBlockToStream(FPDF_FILEWRITE* self, const void* data, unsigned long size) {
    auto* ctx = reinterpret_cast<FileWriteCtx*>(self);
    if (!ctx->out || !ctx->out->good()) return 0;
    ctx->out->write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    return ctx->out->good() ? 1 : 0;
}

}  // namespace

bool SavePagesToPdf(FPDF_DOCUMENT doc, int pageCount, const QString& outPath,
                    int firstPage, int lastPage) {
    if (!doc || pageCount <= 0 || outPath.isEmpty()) return false;

    firstPage = std::max(0, std::min(firstPage, pageCount - 1));
    lastPage  = std::max(firstPage, std::min(lastPage, pageCount - 1));

    // PDFium's page-range syntax is 1-based: "3", "3-7", "1,4,9-12".
    std::string range = std::to_string(firstPage + 1);
    if (lastPage > firstPage) range += "-" + std::to_string(lastPage + 1);

    FPDF_DOCUMENT dest = FPDF_CreateNewDocument();
    if (!dest) return false;

    const int imported = FPDF_ImportPages(dest, doc, range.c_str(), 0);
    if (imported == 0) {
        FPDF_CloseDocument(dest);
        return false;
    }

    // The path is opened through QFile so non-ASCII names work.
    const QByteArray nativePath = QFile::encodeName(outPath);
    std::ofstream out(nativePath.constData(), std::ios::binary | std::ios::trunc);
    if (!out) {
        FPDF_CloseDocument(dest);
        return false;
    }

    FileWriteCtx ctx;
    ctx.base.version   = 1;
    ctx.base.WriteBlock = &WriteBlockToStream;
    ctx.out            = &out;

    const bool ok = FPDF_SaveAsCopy(dest, &ctx.base, 0) != 0;
    out.flush();
    const bool good = out.good();
    out.close();

    FPDF_CloseDocument(dest);
    return ok && good;
}

// Rendering resolution for the raster we hand the printer.
//
// A real printer's own resolution is used as-is. PDF output is capped at 300
// dpi: QPrinter defaults that device to 1200, which produces roughly 5 MB of
// Flate-compressed raster per page and carries no legibility a 300 dpi raster
// does not already have.
double RenderDpi(const QPrinter& printer) {
    double dpi = printer.resolution() > 0 ? printer.resolution() : 300.0;
    if (printer.outputFormat() == QPrinter::PdfFormat && dpi > 300.0) dpi = 300.0;
    return dpi;
}

PrintOutcome PrintPages(FPDF_DOCUMENT doc, int pageCount, QPrinter& printer,
                        int firstPage, int lastPage) {
    PrintOutcome out;
    if (!doc || pageCount <= 0) return out;

    firstPage = std::max(0, std::min(firstPage, pageCount - 1));
    lastPage  = std::max(firstPage, std::min(lastPage, pageCount - 1));
    out.firstPage = firstPage;
    out.lastPage  = lastPage;

    QPainter painter;
    if (!painter.begin(&printer)) {
        out.aborted = true;
        return out;
    }

    // Lay the page out in PDF points and only then convert to the painter's
    // coordinate system. Mixing the two -- a printable rect in printer pixels
    // with an image in render pixels -- silently scales the output, so the two
    // resolutions are kept explicitly separate here.
    const QRectF areaPt = printer.pageLayout().paintRectPoints();
    const int    printerDpi =
        printer.resolution() > 0 ? printer.resolution() : 300;
    const double ptToDevice = printerDpi / 72.0;
    const double renderDpi  = RenderDpi(printer);

    if (areaPt.width() <= 0 || areaPt.height() <= 0) {
        painter.end();
        out.aborted = true;
        return out;
    }

    for (int p = firstPage; p <= lastPage; ++p) {
        if (p > firstPage) printer.newPage();

        double ptW = 0, ptH = 0;
        if (!pre::PageSize(doc, p, &ptW, &ptH) || ptW <= 0 || ptH <= 0) continue;

        // How much of the printable area one PDF point may occupy.
        const double fit = std::min(areaPt.width() / ptW, areaPt.height() / ptH);
        if (fit <= 0) continue;

        // Rasterise at the render resolution, then place by points.
        pre::RenderedPage page;
        if (!pre::RenderPage(doc, p, fit * renderDpi / 72.0, &page)) continue;

        // PDFium writes BGRA, which is Format_ARGB32's byte order on
        // little-endian hosts. No copy: the buffer lives until the next page.
        const QImage img(page.pixels.data(), page.width, page.height, page.stride,
                         QImage::Format_RGB32);

        const double dstW = ptW * fit * ptToDevice;
        const double dstH = ptH * fit * ptToDevice;
        const double dstX = (areaPt.x() + (areaPt.width() - ptW * fit) / 2.0) * ptToDevice;
        const double dstY = (areaPt.y() + (areaPt.height() - ptH * fit) / 2.0) * ptToDevice;

        painter.drawImage(QRectF(dstX, dstY, dstW, dstH), img);
        out.pagesPrinted++;
    }

    painter.end();
    return out;
}

}  // namespace app
