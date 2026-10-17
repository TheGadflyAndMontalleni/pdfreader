// Page rasterisation: PDFium page -> BGRA pixel buffer.
//
// Kept free of any GUI toolkit so the same code backs both the reader and any
// future headless tooling (thumbnails, OCR preprocessing).
#pragma once

#include <cstdint>
#include <vector>

#include "fpdfview.h"

namespace pre {

struct RenderedPage {
    int width  = 0;
    int height = 0;
    int stride = 0;                 // bytes per row
    std::vector<uint8_t> pixels;    // BGRA, top-down, tightly packed per stride

    bool valid() const { return width > 0 && height > 0 && !pixels.empty(); }
};

// Page dimensions in PDF points (unscaled).
bool PageSize(FPDF_DOCUMENT doc, int pageIndex, double* width, double* height);

// Rasterise `pageIndex` at `scale` (1.0 == 72 dpi).
bool RenderPage(FPDF_DOCUMENT doc, int pageIndex, double scale, RenderedPage* out);

// Turn subpixel (LCD) text on or off. PDFium's FPDF_LCD_TEXT renders RGB
// fringes; those read as sharper on a normal LCD at 1:1, but the same fringes
// are what makes text look less clean next to a renderer using plain grayscale
// antialiasing. Exposed so the difference can be measured, not argued about.
void SetLcdText(bool on);
bool LcdText();

}  // namespace pre
