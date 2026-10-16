// Full-document text search.
//
// The hard part of "find" in a PDF is not the string comparison, it is that a
// match has to come back as a *position* the view can highlight and scroll to.
// Characters are therefore searched as codepoints in reading order, carrying
// the index into LineInfo::charIndices alongside, so a hit converts directly
// into the same addressing the selection layer already uses.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "toc.h"

namespace pre {

struct FindMatch {
    int page   = 0;
    int line   = 0;
    int start  = 0;   // position within LineInfo::charIndices
    int length = 0;   // characters covered
};

struct FindResults {
    std::string            needle;
    bool                   matchCase = false;
    std::vector<FindMatch> matches;
    int                    scannedPages = 0;
    bool                   truncated = false;   // hit the match cap
};

// Codepoints of one line in reading order, plus the charIndices position each
// one came from. Applies the same control-character rules as line text
// building, so searching and text extraction agree on what a line contains.
void LineCodepoints(const std::vector<CharInfo>& chars, const LineInfo& line,
                    std::vector<uint32_t>* cps, std::vector<int>* charPos);

// Case-fold a single codepoint for matching. ASCII only: CJK has no case.
uint32_t FoldForFind(uint32_t cp);

// Find every non-overlapping occurrence of `needle` (UTF-8) in the document.
FindResults FindInDocument(PageIndex& index, const std::string& needle,
                           bool matchCase, int maxMatches = 20000);

}  // namespace pre
