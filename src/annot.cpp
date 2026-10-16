#include "annot.h"

#include <algorithm>
#include <cmath>
#include <fstream>

#include "fpdf_edit.h"
#include "fpdf_save.h"

namespace pre {

namespace {

// One added mark, remembered so it can be taken back before saving.
//
// Only the object *count* is stored, never the FPDF_PAGEOBJECT handles: those
// belong to the page and go stale as soon as FPDF_ClosePage runs, so keeping
// them and destroying them later corrupts the heap. Undo re-fetches the objects
// by index from a freshly loaded page instead.
struct MarkRecord {
    int page = 0;
    int objectCount = 0;
};

std::vector<MarkRecord> g_marks;

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

// Draw one bar and return its page object, or nullptr.
FPDF_PAGEOBJECT Bar(double left, double bottom, double width, double height,
                    unsigned int rgb, unsigned int alpha) {
    if (width <= 0.0 || height <= 0.0) return nullptr;
    FPDF_PAGEOBJECT obj =
        FPDFPageObj_CreateNewRect(static_cast<float>(left), static_cast<float>(bottom),
                                  static_cast<float>(width), static_cast<float>(height));
    if (!obj) return nullptr;

    FPDFPageObj_SetFillColor(obj, (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, alpha);
    // Fill only, no stroke: a stroked rect would show a border.
    FPDFPath_SetDrawMode(obj, FPDF_FILLMODE_ALTERNATE, /*stroke=*/0);
    return obj;
}

}  // namespace

int AddMarkup(FPDF_DOCUMENT doc, int pageIndex, MarkupKind kind,
              const std::vector<MarkupRect>& rects, unsigned int rgb) {
    if (!doc || rects.empty()) return -1;

    FPDF_PAGE page = FPDF_LoadPage(doc, pageIndex);
    if (!page) return -1;

    MarkRecord record;
    record.page = pageIndex;

    int drawn = 0;
    for (const MarkupRect& r : rects) {
        const double w = r.right - r.left;
        const double h = r.top - r.bottom;
        if (w <= 0.0 || h <= 0.0) continue;

        FPDF_PAGEOBJECT obj = nullptr;
        switch (kind) {
            case MarkupKind::Highlight:
                // Translucent so the text underneath stays readable.
                obj = Bar(r.left, r.bottom, w, h, rgb, 90);
                break;
            case MarkupKind::Underline:
                obj = Bar(r.left, r.bottom, w, std::max(0.6, h * 0.07), rgb, 255);
                break;
            case MarkupKind::StrikeOut:
                obj = Bar(r.left, r.bottom + h * 0.45, w, std::max(0.6, h * 0.07), rgb, 255);
                break;
        }
        if (!obj) continue;

        if (!FPDFPage_InsertObject(page, obj)) {
            // PDFium documents that InsertObject *frees* the object when it
            // fails, so destroying it here would be a double free.
            continue;
        }
        ++record.objectCount;
        ++drawn;
    }

    if (drawn > 0) {
        // Without this the objects exist but are not part of the content stream,
        // so neither rendering nor saving would pick them up.
        FPDFPage_GenerateContent(page);
        g_marks.push_back(record);
    }

    FPDF_ClosePage(page);
    return drawn > 0 ? drawn : -1;
}

int UndoLastMarkup(FPDF_DOCUMENT doc) {
    if (!doc || g_marks.empty()) return -1;

    const MarkRecord record = g_marks.back();
    g_marks.pop_back();

    FPDF_PAGE page = FPDF_LoadPage(doc, record.page);
    if (!page) return -1;

    // The objects this mark added are the last ones on the page, so they can be
    // taken from the tail. Re-fetching by index keeps every handle fresh.
    for (int i = 0; i < record.objectCount; ++i) {
        const int count = FPDFPage_CountObjects(page);
        if (count <= 0) break;
        FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page, count - 1);
        if (!obj) break;
        FPDFPage_RemoveObject(page, obj);
        FPDFPageObj_Destroy(obj);   // ownership came back with the removal
    }
    FPDFPage_GenerateContent(page);
    FPDF_ClosePage(page);
    return record.page;
}

int MarkupCount() { return static_cast<int>(g_marks.size()); }

void ResetMarkupState() { g_marks.clear(); }

bool SaveDocumentToFile(FPDF_DOCUMENT doc, const std::string& path) {
    if (!doc || path.empty()) return false;

    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!out) return false;

    FileWriteCtx ctx;
    ctx.base.version    = 1;
    ctx.base.WriteBlock = &WriteBlockToStream;
    ctx.out             = &out;

    const bool ok = FPDF_SaveAsCopy(doc, &ctx.base, 0) != 0;
    out.flush();
    const bool good = out.good();
    out.close();
    return ok && good;
}

}  // namespace pre
