// Geometric reading order for text selection, search and plugin text.
#pragma once

#include "text.h"

namespace pre {

struct ReadingColumn {
    int firstLine, endLine; // half-open interval in ReadingPage::lines
    double left, right;     // ink extent across the column
    double bottom, top;     // shared vertical band of side-by-side columns
};

struct ReadingPage {
    std::vector<LineInfo> lines;
    std::vector<ReadingColumn> columns;
    int HitLine(double x, double y, int anchorLine = -1) const;
};

ReadingPage BuildReadingPage(int pageIndex, const std::vector<CharInfo>& chars);
// PDFium represents a visible line-end hyphen with control code 2.
inline uint32_t ReadingCodepoint(const CharInfo& c) {
    if (c.hyphen && c.codepoint == 2) return '-';
    return c.codepoint == '\t' ? ' ' : c.codepoint;
}

} // namespace pre
