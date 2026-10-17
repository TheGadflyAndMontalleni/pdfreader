#include "render.h"

#include <algorithm>
#include <cmath>

namespace pre {

namespace {
// Subpixel text is OFF by default.
//
// FPDF_LCD_TEXT gives sharper stems only when glyphs land on whole device
// pixels at exact 1:1. PDF text positions are fractional, so at any real zoom
// the subpixel phase varies per glyph: some stems fringe red, others blue. The
// result reads as a coloured haze rather than sharpness.
//
// Measured on one page at fit-width (scale 1.776): subpixel AA produced 24,860
// coloured edge pixels (mean channel spread 5.18) against 0 for grayscale, and
// grayscale put 57% more ink on the page (12,063 vs 7,701 dark samples), so
// text also looks noticeably more solid. MuPDF, which SumatraPDF uses, renders
// text with grayscale antialiasing, so this also matches the look of the reader
// being compared against.
bool g_lcdText = false;
}  // namespace

void SetLcdText(bool on) { g_lcdText = on; }
bool LcdText() { return g_lcdText; }

bool PageSize(FPDF_DOCUMENT doc, int pageIndex, double* width, double* height) {
    if (!doc) return false;
    FPDF_PAGE page = FPDF_LoadPage(doc, pageIndex);
    if (!page) return false;
    if (width)  *width  = FPDF_GetPageWidthF(page);
    if (height) *height = FPDF_GetPageHeightF(page);
    FPDF_ClosePage(page);
    return true;
}

bool RenderPage(FPDF_DOCUMENT doc, int pageIndex, double scale, RenderedPage* out) {
    if (!doc || !out) return false;
    if (scale <= 0.0) scale = 1.0;

    FPDF_PAGE page = FPDF_LoadPage(doc, pageIndex);
    if (!page) return false;

    const double ptW = FPDF_GetPageWidthF(page);
    const double ptH = FPDF_GetPageHeightF(page);

    int pw = static_cast<int>(std::lround(ptW * scale));
    int ph = static_cast<int>(std::lround(ptH * scale));
    pw = std::max(1, std::min(pw, 20000));
    ph = std::max(1, std::min(ph, 20000));

    const int stride = pw * 4;
    out->width  = pw;
    out->height = ph;
    out->stride = stride;
    out->pixels.assign(static_cast<size_t>(stride) * static_cast<size_t>(ph), 0xFF);

    // BGRx, not BGRA: the fourth byte is documented as unused, so the buffer is
    // opaque and Qt can blit it with a plain copy. With BGRA the alpha channel
    // carries coverage, and Qt alpha-blends every pixel of every page on every
    // repaint -- several million blends per frame for a full-screen page.
    FPDF_BITMAP bitmap = FPDFBitmap_CreateEx(pw, ph, FPDFBitmap_BGRx,
                                             out->pixels.data(), stride);
    if (!bitmap) {
        FPDF_ClosePage(page);
        out->pixels.clear();
        return false;
    }

    FPDFBitmap_FillRect(bitmap, 0, 0, pw, ph, 0xFFFFFFFF);
    // FPDF_ANNOT draws annotations/widgets. FPDF_LCD_TEXT adds subpixel RGB
    // antialiasing, which is switchable (see SetLcdText).
    int flags = FPDF_ANNOT;
    if (g_lcdText) flags |= FPDF_LCD_TEXT;
    FPDF_RenderPageBitmap(bitmap, page, 0, 0, pw, ph, 0, flags);

    FPDFBitmap_Destroy(bitmap);
    FPDF_ClosePage(page);
    return true;
}

}  // namespace pre
