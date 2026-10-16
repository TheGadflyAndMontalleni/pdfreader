// P0 probe — validates the toolchain end-to-end and exercises exactly the
// primitives the auto-TOC engine depends on:
//   * document / page inventory
//   * embedded outline (/Outlines) detection
//   * per-character extraction with font size  -> typography-based heading detection
//   * per-character bounding boxes              -> layout / column reconstruction
//
// Build: CMake + Ninja + MinGW 13.1, Qt 6.8.3, PDFium (chromium/8100).

#include <QtCore/QCoreApplication>
#include <QtCore/QString>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "fpdfview.h"
#include "fpdf_text.h"
#include "fpdf_doc.h"

namespace {

int CountSiblings(FPDF_DOCUMENT doc, FPDF_BOOKMARK first) {
    int n = 0;
    for (FPDF_BOOKMARK b = first; b != nullptr; b = FPDFBookmark_GetNextSibling(doc, b)) {
        ++n;
    }
    return n;
}

// Recursively report the embedded outline, if any.
void DumpOutline(FPDF_DOCUMENT doc, FPDF_BOOKMARK first, int depth, int* total) {
    for (FPDF_BOOKMARK b = first; b != nullptr; b = FPDFBookmark_GetNextSibling(doc, b)) {
        unsigned long len = FPDFBookmark_GetTitle(b, nullptr, 0);
        std::wstring title(len / sizeof(unsigned short), L'\0');
        if (len > 0) {
            FPDFBookmark_GetTitle(b, title.data(), len);
            if (!title.empty() && title.back() == L'\0') title.pop_back();
        }
        std::string narrow;
        for (wchar_t c : title) narrow += (c < 128 ? static_cast<char>(c) : '?');

        int pageIndex = -1;
        FPDF_DEST dest = FPDFBookmark_GetDest(doc, b);
        if (dest) pageIndex = FPDFDest_GetDestPageIndex(doc, dest);

        std::printf("  %*s- \"%s\" -> page %d\n", depth * 2, "", narrow.c_str(), pageIndex + 1);
        ++(*total);
        DumpOutline(doc, FPDFBookmark_GetFirstChild(doc, b), depth + 1, total);
    }
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    if (argc < 2) {
        std::printf("usage: p0_probe <file.pdf> [maxPagesForStats]\n");
        return 2;
    }
    const char* path = argv[1];
    const int maxPages = (argc >= 3) ? std::atoi(argv[2]) : 3;

    FPDF_LIBRARY_CONFIG cfg;
    std::memset(&cfg, 0, sizeof(cfg));
    cfg.version = 2;
    FPDF_InitLibraryWithConfig(&cfg);

    FPDF_DOCUMENT doc = FPDF_LoadDocument(path, nullptr);
    if (!doc) {
        std::printf("FAIL load: error=%lu\n", FPDF_GetLastError());
        FPDF_DestroyLibrary();
        return 1;
    }

    const int pageCount = FPDF_GetPageCount(doc);
    std::printf("file        : %s\n", path);
    std::printf("pages       : %d\n", pageCount);

    // --- embedded outline ---
    int outlineTotal = 0;
    int topLevel = CountSiblings(doc, FPDFBookmark_GetFirstChild(doc, nullptr));
    std::printf("outline     : top-level=%d\n", topLevel);
    if (topLevel > 0) {
        DumpOutline(doc, FPDFBookmark_GetFirstChild(doc, nullptr), 1, &outlineTotal);
        std::printf("outline     : total nodes=%d\n", outlineTotal);
    } else {
        std::printf("outline     : NONE  <-- this is exactly the case our engine must handle\n");
    }

    // --- per-character typography stats (basis of heading detection) ---
    const int statsPages = (maxPages < pageCount) ? maxPages : pageCount;
    std::map<int, int> sizeHistogram;   // size*2 -> char count
    std::map<std::string, int> fontHistogram;
    int charsWithBox = 0;

    for (int p = 0; p < statsPages; ++p) {
        FPDF_PAGE page = FPDF_LoadPage(doc, p);
        if (!page) continue;
        FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
        if (textPage) {
            const int n = FPDFText_CountChars(textPage);
            for (int i = 0; i < n; ++i) {
                const double size = FPDFText_GetFontSize(textPage, i);
                if (size > 0) sizeHistogram[static_cast<int>(std::lround(size * 2))]++;

                double l, r, b, t;
                if (FPDFText_GetCharBox(textPage, i, &l, &r, &b, &t)) charsWithBox++;

                char fontName[128] = {0};
                int flags = 0;   // PDFium declares int* here, not unsigned long*
                if (FPDFText_GetFontInfo(textPage, i, fontName, sizeof(fontName), &flags) > 0) {
                    fontHistogram[std::string(fontName)]++;
                }
            }
            FPDFText_ClosePage(textPage);
        }
        FPDF_ClosePage(page);
    }

    std::printf("chars w/ box: %d over %d page(s)\n", charsWithBox, statsPages);
    std::printf("font-size histogram (size -> chars):\n");
    for (const auto& kv : sizeHistogram) {
        std::printf("    %5.1f pt : %d\n", kv.first / 2.0, kv.second);
    }
    std::printf("fonts seen:\n");
    for (const auto& kv : fontHistogram) {
        std::printf("    %-40s %d\n", kv.first.c_str(), kv.second);
    }

    FPDF_CloseDocument(doc);
    FPDF_DestroyLibrary();
    return 0;
}
