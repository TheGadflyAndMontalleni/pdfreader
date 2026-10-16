#include "find.h"

namespace pre {

void LineCodepoints(const std::vector<CharInfo>& chars, const LineInfo& line,
                    std::vector<uint32_t>* cps, std::vector<int>* charPos) {
    cps->clear();
    charPos->clear();
    cps->reserve(line.charIndices.size());
    charPos->reserve(line.charIndices.size());

    for (size_t k = 0; k < line.charIndices.size(); ++k) {
        const int ci = line.charIndices[k];
        if (ci < 0 || ci >= static_cast<int>(chars.size())) continue;
        uint32_t cp = chars[static_cast<size_t>(ci)].codepoint;

        if (cp == '\n' || cp == '\r' || cp == '\t') {
            cp = ' ';
        } else if (cp < 0x20) {
            // Other control characters contribute no text, matching how line
            // text is built; skipping them keeps positions aligned.
            continue;
        }
        cps->push_back(cp);
        charPos->push_back(static_cast<int>(k));
    }
}

uint32_t FoldForFind(uint32_t cp) {
    if (cp >= 'A' && cp <= 'Z') return cp + ('a' - 'A');
    return cp;
}

FindResults FindInDocument(PageIndex& index, const std::string& needle,
                           bool matchCase, int maxMatches) {
    FindResults out;
    out.needle    = needle;
    out.matchCase = matchCase;

    std::vector<uint32_t> pattern = Utf8Decode(needle);
    if (pattern.empty()) return out;
    if (!matchCase) {
        for (auto& c : pattern) c = FoldForFind(c);
    }

    for (int page = 0; page < index.PageCount(); ++page) {
        ++out.scannedPages;
        const auto& lines = index.Lines(page);
        const auto& chars = index.Chars(page);

        std::vector<uint32_t> cps;
        std::vector<int>      charPos;
        for (size_t li = 0; li < lines.size(); ++li) {
            LineCodepoints(chars, lines[li], &cps, &charPos);
            if (cps.size() < pattern.size()) continue;

            if (!matchCase) {
                for (auto& c : cps) c = FoldForFind(c);
            }

            for (size_t i = 0; i + pattern.size() <= cps.size(); ++i) {
                bool hit = true;
                for (size_t k = 0; k < pattern.size(); ++k) {
                    if (cps[i + k] != pattern[k]) {
                        hit = false;
                        break;
                    }
                }
                if (!hit) continue;

                FindMatch m;
                m.page  = page;
                m.line  = static_cast<int>(li);
                m.start = charPos[i];
                // The span may be wider than the pattern when characters in
                // between were skipped; highlight what is actually there.
                m.length = charPos[i + pattern.size() - 1] - m.start + 1;
                out.matches.push_back(m);

                if (static_cast<int>(out.matches.size()) >= maxMatches) {
                    out.truncated = true;
                    return out;
                }
                i += pattern.size() - 1;   // non-overlapping matches
            }
        }
    }
    return out;
}

}  // namespace pre
