// P1 probe — reconstructs text lines from PDFium's per-character data and
// reports the typography an auto-TOC engine needs:
//   * the body-text size (most characters) that headings must stand out from
//   * per-line font size / weight / boldness / horizontal extent
//
// Modes:
//   p1_lines <file.pdf> [page] [maxLines] [flags]
//     --chars     dump per-character geometry instead of lines
//     --spaces    enable gap-based space synthesis (off by default; the ink-box
//                 rule misfires on CJK)

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "doc.h"
#include "text.h"

namespace {

void DumpChars(const std::vector<pre::CharInfo>& chars, int limit) {
    // Page-level sanity stats: a degenerate originY (same value for every
    // character) or implausible font sizes break baseline clustering.
    int    withOrigin = 0;
    double minOriginY = 1e30, maxOriginY = -1e30;
    double minSize = 1e30, maxSize = -1e30;
    std::map<int, int> sizeCount;
    for (const auto& c : chars) {
        if (c.hasOrigin) {
            ++withOrigin;
            minOriginY = std::min(minOriginY, c.originY);
            maxOriginY = std::max(maxOriginY, c.originY);
        }
        if (c.fontSize > 0) {
            minSize = std::min(minSize, c.fontSize);
            maxSize = std::max(maxSize, c.fontSize);
            sizeCount[static_cast<int>(std::lround(c.fontSize * 2))] += 1;
        }
    }
    std::printf("\npage stats: chars=%zu withOrigin=%d originY=[%.2f..%.2f]\n", chars.size(),
                withOrigin, withOrigin > 0 ? minOriginY : 0.0,
                withOrigin > 0 ? maxOriginY : 0.0);
    std::printf("font size range: [%.1f .. %.1f]\n", minSize > 1e29 ? 0.0 : minSize,
                maxSize < -1e29 ? 0.0 : maxSize);
    std::printf("size counts: ");
    int printed = 0;
    for (const auto& kv : sizeCount) {
        if (printed++ >= 12) { std::printf("..."); break; }
        std::printf("%.1f=%d  ", kv.first / 2.0, kv.second);
    }
    std::printf("\n");

    std::printf("\nper-character geometry (cp, box, ink width, advance, gap):\n");
    std::printf("  %-4s %-6s %-3s %8s %8s %8s %9s %8s %6s %9s\n", "idx", "char", "sp",
                "left", "right", "ink_w", "advance", "gap", "size", "originY");
    int shown = 0;
    for (size_t i = 0; i < chars.size(); ++i) {
        if (shown >= limit) {
            std::printf("  ... (%zu more chars)\n", chars.size() - shown);
            break;
        }
        const pre::CharInfo& c = chars[i];
        double advance = 0.0, gap = 0.0;
        if (i + 1 < chars.size() && c.hasOrigin && chars[i + 1].hasOrigin) {
            advance = chars[i + 1].originX - c.originX;
            gap     = chars[i + 1].left - c.right;
        }
        std::printf("  %-4d %-6s %-3s %8.2f %8.2f %8.2f %9.2f %8.2f %6.1f %9.2f\n", c.index,
                    pre::Utf8Encode(c.codepoint).c_str(), c.IsSpace() ? "Y" : "",
                    c.left, c.right, c.Width(), advance, gap, c.fontSize,
                    c.hasOrigin ? c.originY : -999.0);
        ++shown;
    }
}

}  // namespace

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);

    if (argc < 2) {
        std::printf("usage: p1_lines <file.pdf> [page] [maxLines] [--chars] [--spaces]\n");
        return 2;
    }
    const char* path    = argv[1];
    const int   pageIdx = (argc >= 3) ? std::atoi(argv[2]) : 0;
    const int   maxOut  = (argc >= 4) ? std::atoi(argv[3]) : 40;

    pre::LineBuildOptions opts;
    bool dumpChars = false;
    for (int i = 4; i < argc; ++i) {
        if (std::strcmp(argv[i], "--chars") == 0)  dumpChars = true;
        if (std::strcmp(argv[i], "--spaces") == 0) opts.synthesizeSpaces = true;
    }

    pre::Library  lib;
    pre::Document doc;
    std::string   err;
    if (!doc.Open(path, &err)) {
        std::printf("FAIL open: %s\n", err.c_str());
        return 1;
    }

    std::printf("file        : %s\n", path);
    std::printf("opened via  : %s\n", doc.usedMemFallback() ? "memory fallback" : "path");
    std::printf("pages       : %d\n", doc.PageCount());
    if (pageIdx < 0 || pageIdx >= doc.PageCount()) {
        std::printf("page %d out of range\n", pageIdx);
        return 1;
    }

    const auto chars = pre::ExtractPageChars(doc.handle(), pageIdx);
    const auto lines = pre::BuildLines(pageIdx, chars, opts);
    std::printf("page %-7d: %zu chars -> %zu lines%s\n", pageIdx, chars.size(), lines.size(),
                opts.synthesizeSpaces ? "  [space synthesis ON]" : "");

    if (dumpChars) {
        DumpChars(chars, maxOut);
        return 0;
    }

    std::map<int, int> charsBySize;
    std::map<int, int> linesBySize;
    for (const auto& L : lines) {
        // Bucket by glyph height: geometry-based and immune to bogus font sizes.
        const int key = static_cast<int>(std::lround(L.glyphHeight * 2.0));
        linesBySize[key] += 1;
        charsBySize[key] += static_cast<int>(L.charIndices.size());
    }
    std::printf("\nglyph-height histogram (height -> lines / chars):\n");
    for (const auto& kv : charsBySize) {
        std::printf("  %6.1f    : %3d lines  %5d chars\n", kv.first / 2.0,
                    linesBySize[kv.first], kv.second);
    }

    std::printf("\nreconstructed lines (top, glyphH, size, weight, bold, x-range, text):\n");
    int shown = 0;
    for (const auto& L : lines) {
        if (shown >= maxOut) {
            std::printf("  ... (%zu more lines)\n", lines.size() - shown);
            break;
        }
        std::printf("  y=%7.1f h=%5.1f sz=%5.1f w=%3d%s x=%6.1f..%6.1f | %s\n", L.top,
                    L.glyphHeight, L.maxFontSize, L.maxWeight, L.bold ? " B" : "  ", L.left,
                    L.right, L.text.c_str());
        ++shown;
    }
    return 0;
}
