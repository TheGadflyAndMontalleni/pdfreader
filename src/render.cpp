#include "render.h"

#include <algorithm>
#include <cmath>

namespace pre {

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
    // FPDF_ANNOT draws annotations/widgets; FPDF_LCD_TEXT improves subpixel
    // legibility on opaque backgrounds.
    FPDF_RenderPageBitmap(bitmap, page, 0, 0, pw, ph, 0, FPDF_ANNOT | FPDF_LCD_TEXT);

    FPDFBitmap_Destroy(bitmap);
    FPDF_ClosePage(page);
    return true;
}

}  // namespace pre
