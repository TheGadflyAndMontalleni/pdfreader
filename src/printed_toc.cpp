// L2: parse a printed table-of-contents page, then turn its printed page
// numbers into physical page indices the sidebar can jump to.
#include "toc.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

namespace pre {
namespace {

struct Cp {
    uint32_t cp;
    size_t   byteStart;
    size_t   byteLen;
};

std::vector<Cp> DecodeWithOffsets(const std::string& s) {
    std::vector<Cp> out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char b0 = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0;
        size_t   extra = 0;
        if (b0 < 0x80)                     { cp = b0;         extra = 0; }
        else if ((b0 & 0xE0) == 0xC0)      { cp = b0 & 0x1F;  extra = 1; }
        else if ((b0 & 0xF0) == 0xE0)      { cp = b0 & 0x0F;  extra = 2; }
        else if ((b0 & 0xF8) == 0xF0)      { cp = b0 & 0x07;  extra = 3; }
        else { ++i; continue; }

        if (i + extra >= s.size()) break;
        bool ok = true;
        for (size_t k = 1; k <= extra; ++k) {
            const unsigned char bk = static_cast<unsigned char>(s[i + k]);
            if ((bk & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (bk & 0x3F);
        }
        const size_t start = i;
        i += extra + 1;
        if (ok) out.push_back({cp, start, i - start});
    }
    return out;
}

bool IsAsciiSpace(uint32_t cp) {
    return cp == ' ' || cp == '\t' || cp == '\r' || cp == '\n' || cp == 0x3000;
}

// Characters used as leader dots between a title and its page number.
bool IsLeader(uint32_t cp) {
    switch (cp) {
        case '.':       // .
        case 0xFF0E:    // ．
        case 0x00B7:    // ·
        case 0x2022:    // •
        case 0x2026:    // …
        case 0x2027:    // ‧
        case 0x22EF:    // ⋯
        case 0x30FB:    // ・
        case 0x00B0:    // °
        case '_':
            return true;
        default:
            return false;
    }
}

bool IsDigit(uint32_t cp) {
    return (cp >= '0' && cp <= '9') || (cp >= 0xFF10 && cp <= 0xFF19);
}

int DigitValue(uint32_t cp) {
    if (cp >= '0' && cp <= '9') return static_cast<int>(cp - '0');
    return static_cast<int>(cp - 0xFF10);
}

std::string TrimUtf8(const std::string& s) {
    size_t b = 0, e = s.size();
    auto sp = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    while (b < e && sp(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && sp(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// Strip a trailing run of leader characters (and spaces) from a title.
std::string StripTrailingLeaders(const std::string& s) {
    auto cps = DecodeWithOffsets(s);
    size_t end = s.size();
    for (size_t i = cps.size(); i-- > 0;) {
        if (IsLeader(cps[i].cp) || IsAsciiSpace(cps[i].cp)) {
            end = cps[i].byteStart;
        } else {
            break;
        }
    }
    return TrimUtf8(s.substr(0, end));
}

// --- numbering analysis -----------------------------------------------------

bool IsChineseNumeral(uint32_t cp) {
    switch (cp) {
        case 0x4E00: case 0x4E8C: case 0x4E09: case 0x56DB: case 0x4E94:  // 一二三四五
        case 0x516D: case 0x4E03: case 0x516B: case 0x4E5D: case 0x5341:  // 六七八九十
        case 0x767E: case 0x5343: case 0x96F6: case 0x3007:              // 百千零〇
            return true;
        default:
            return false;
    }
}

// Returns an implied hierarchy level, or 0 when the title carries no numbering.
// `prefixOut` receives the numbering text itself. Exposed to the typography
// heuristic through the NumberingLevel() wrapper at the end of this file.
int NumberingLevelImpl(const std::string& title, std::string* prefixOut) {
    const auto cps = DecodeWithOffsets(title);
    if (cps.empty()) return 0;

    size_t i = 0;
    while (i < cps.size() && IsAsciiSpace(cps[i].cp)) ++i;
    if (i >= cps.size()) return 0;

    // 第X章 / 第X节 / 第X篇 / 第X部分
    if (cps[i].cp == 0x7B2C) {  // 第
        size_t j = i + 1;
        while (j < cps.size() && (IsChineseNumeral(cps[j].cp) || IsDigit(cps[j].cp))) ++j;
        if (j < cps.size()) {
            const uint32_t unit = cps[j].cp;
            if (unit == 0x7AE0 || unit == 0x7BC7 || unit == 0x90E8) {  // 章 篇 部
                if (prefixOut) {
                    *prefixOut = title.substr(cps[i].byteStart,
                                              cps[j].byteStart + cps[j].byteLen -
                                                  cps[i].byteStart);
                }
                return 1;
            }
            if (unit == 0x8282) {  // 节
                if (prefixOut) {
                    *prefixOut = title.substr(cps[i].byteStart,
                                              cps[j].byteStart + cps[j].byteLen -
                                                  cps[i].byteStart);
                }
                return 2;
            }
        }
        return 0;
    }

    // （一） / (一)
    if (cps[i].cp == 0xFF08 || cps[i].cp == '(') {  // （ (
        size_t j = i + 1;
        while (j < cps.size() && IsChineseNumeral(cps[j].cp)) ++j;
        if (j > i + 1 && j < cps.size() &&
            (cps[j].cp == 0xFF09 || cps[j].cp == ')')) {
            if (prefixOut) {
                *prefixOut = title.substr(cps[i].byteStart,
                                          cps[j].byteStart + cps[j].byteLen -
                                              cps[i].byteStart);
            }
            return 2;
        }
    }

    // 一、 / 一. / 一
    if (IsChineseNumeral(cps[i].cp)) {
        size_t j = i;
        while (j < cps.size() && IsChineseNumeral(cps[j].cp)) ++j;
        if (j < cps.size() &&
            (cps[j].cp == 0x3001 || cps[j].cp == '.' || cps[j].cp == 0xFF0E ||
             cps[j].cp == 0x3002)) {  // 、 . ． 。
            if (prefixOut) {
                *prefixOut = title.substr(cps[i].byteStart,
                                          cps[j].byteStart + cps[j].byteLen -
                                              cps[i].byteStart);
            }
            return 1;
        }
    }

    // Arabic: 1. / 1、 / 1.2 / 1.2.3
    if (IsDigit(cps[i].cp)) {
        size_t j = i;
        int    components = 1;
        size_t lastDigitEnd = i;
        while (j < cps.size()) {
            if (IsDigit(cps[j].cp)) {
                lastDigitEnd = j;
                ++j;
                continue;
            }
            if (cps[j].cp == '.' || cps[j].cp == 0xFF0E || cps[j].cp == 0x3001) {
                // Only count as a component separator when a digit follows.
                size_t k = j + 1;
                while (k < cps.size() && IsAsciiSpace(cps[k].cp)) ++k;
                if (k < cps.size() && IsDigit(cps[k].cp)) {
                    ++components;
                    j = k;
                    continue;
                }
                // Trailing separator: "1." or "1、"
                if (prefixOut) {
                    *prefixOut = title.substr(cps[i].byteStart,
                                              cps[j].byteStart + cps[j].byteLen -
                                                  cps[i].byteStart);
                }
                return (components == 1) ? 3 : components;
            }
            break;
        }
        if (lastDigitEnd > i) {
            if (prefixOut) {
                *prefixOut = title.substr(cps[i].byteStart,
                                          cps[lastDigitEnd].byteStart +
                                              cps[lastDigitEnd].byteLen -
                                              cps[i].byteStart);
            }
            return (components == 1) ? 3 : components;
        }
    }
    return 0;
}

bool LineMentionsContents(const std::vector<LineInfo>& lines) {
    for (const auto& L : lines) {
        const std::string n = NormalizeForMatch(L.text);
        if (n == "contents" || n == "tableofcontents" || n == "目录" ||
            n == "目次" || n == "contents目录") {
            return true;
        }
        if (Utf8Decode(n).size() <= 8 &&
            (n.find("目录") != std::string::npos ||
             n.find("contents") != std::string::npos)) {
            return true;
        }
    }
    return false;
}

// Consume `flat[i]` and every following node deeper than it, recursively.
TocNode BuildSubtree(const std::vector<TocNode>& flat, size_t& i) {
    TocNode   node  = flat[i];
    const int level = flat[i].level;
    ++i;
    while (i < flat.size() && flat[i].level > level) {
        node.children.push_back(BuildSubtree(flat, i));
    }
    return node;
}

// A page that merely ends its lines with right-aligned numbers is not a table
// of contents: a province table or a data sheet looks identical line by line.
// Two cheap document-level properties separate them.
//   1. TOC page numbers are non-decreasing as you read down the page; counts
//      in a data table are arbitrary.
//   2. Every page number must be a plausible page of this document.
bool PlausibleToc(const std::vector<PrintedEntry>& entries, int pageCount) {
    if (entries.size() < 3) return false;

    int inRange = 0;
    for (const auto& e : entries) {
        if (e.printedPage >= 1 && e.printedPage <= pageCount + 2) ++inRange;
    }
    if (static_cast<double>(inRange) / static_cast<double>(entries.size()) < 0.90) {
        return false;
    }

    int nonDecreasing = 0, pairs = 0;
    for (size_t i = 1; i < entries.size(); ++i) {
        ++pairs;
        // Allow a one-page backward step: multi-page TOCs repeat numbers when
        // several headings share a page, and continuation pages can restart.
        if (entries[i].printedPage >= entries[i - 1].printedPage - 1) ++nonDecreasing;
    }
    if (pairs == 0) return false;
    return static_cast<double>(nonDecreasing) / static_cast<double>(pairs) >= 0.75;
}

}  // namespace

// Extract a single printed-TOC entry from one line, if the line looks like one.
//
// Works on the character geometry rather than on the assembled text, because
// the separation between a title and its page number is a *spatial* fact:
//   * most producers fill it with leader dots, but
//   * some (e.g. report2021.pdf) just leave a wide gap and right-align the
//     number, which no text-only rule can distinguish from ordinary prose.
bool ParsePrintedLine(const LineInfo& line, const std::vector<CharInfo>& chars,
                      PrintedEntry* out) {
    const std::vector<int>& ids = line.charIndices;
    if (ids.size() < 3) return false;

    auto blank = [&chars](int ci) {
        const CharInfo& c = chars[ci];
        return c.IsSpace() || c.codepoint < 0x20 || c.codepoint == 0x7F;
    };

    // Trailing blanks.
    size_t end = ids.size();
    while (end > 0 && blank(ids[end - 1])) --end;
    if (end == 0) return false;

    // Page number: the trailing digit run.
    size_t digitBegin = end;
    while (digitBegin > 0 && IsDigit(chars[ids[digitBegin - 1]].codepoint)) {
        --digitBegin;
    }
    const size_t digitCount = end - digitBegin;
    if (digitCount == 0 || digitCount > 5) return false;

    long printed = 0;
    for (size_t k = digitBegin; k < end; ++k) {
        printed = printed * 10 + DigitValue(chars[ids[k]].codepoint);
    }

    // Leader dots (and blanks) immediately before the number.
    size_t j = digitBegin;
    size_t leaderCount = 0;
    while (j > 0) {
        const uint32_t cp = chars[ids[j - 1]].codepoint;
        if (IsLeader(cp)) {
            ++leaderCount;
            --j;
        } else if (blank(ids[j - 1])) {
            --j;
        } else {
            break;
        }
    }
    if (j == 0) return false;

    // Spatial separation between the last title glyph and the first digit.
    const CharInfo& lastTitle  = chars[ids[j - 1]];
    const CharInfo& firstDigit = chars[ids[digitBegin]];
    const double gap = firstDigit.left - lastTitle.right;
    const double ref = std::max(line.glyphHeight, 1.0);
    const bool   wideGap = gap >= 3.0 * ref;

    if (leaderCount < 3 && !wideGap) return false;

    // Assemble the title from the surviving characters.
    std::string title;
    for (size_t k = 0; k < j; ++k) {
        uint32_t cp = chars[ids[k]].codepoint;
        if (cp < 0x20 || cp == 0x7F) cp = ' ';
        if (cp == ' ') {
            if (!title.empty() && title.back() != ' ') title.push_back(' ');
        } else {
            title += Utf8Encode(cp);
        }
    }
    while (!title.empty() && title.back() == ' ') title.pop_back();
    if (Utf8Decode(title).size() < 2) return false;

    PrintedEntry e;
    e.title       = title;
    e.printedPage = static_cast<int>(printed);
    e.left        = line.left;
    e.level       = NumberingLevelImpl(title, &e.numbering);
    *out = std::move(e);
    return true;
}

double ScoreTocPage(const std::vector<LineInfo>& lines,
                    const std::vector<CharInfo>& chars, int* entryCountOut) {
    int entries = 0;
    int nonEmpty = 0;
    for (const auto& L : lines) {
        if (Utf8Decode(NormalizeForMatch(L.text)).empty()) continue;
        ++nonEmpty;
        PrintedEntry e;
        if (ParsePrintedLine(L, chars, &e)) ++entries;
    }
    if (entryCountOut) *entryCountOut = entries;
    if (entries < 3 || nonEmpty == 0) return entries / 10.0;

    const double density = static_cast<double>(entries) / static_cast<double>(nonEmpty);
    double score = std::min(1.0, static_cast<double>(entries) / 10.0) *
                   (0.5 + 0.5 * std::min(1.0, density * 3.0));
    if (LineMentionsContents(lines)) score = std::min(1.0, score + 0.15);
    return score;
}

std::vector<PrintedEntry> ParsePrintedToc(const std::vector<LineInfo>& lines,
                                          const std::vector<CharInfo>& chars) {
    std::vector<PrintedEntry> out;
    for (const auto& L : lines) {
        PrintedEntry e;
        if (ParsePrintedLine(L, chars, &e)) out.push_back(std::move(e));
    }
    return out;
}

PrintedToc FindPrintedToc(PageIndex& index, int maxScan) {
    PrintedToc best;
    const int limit = std::min(index.PageCount(), std::max(1, maxScan));

    // 1) pick the strongest single candidate page inside the scan window
    int    seedPage = -1, seedEntries = 0;
    double seedScore = 0.0;
    for (int p = 0; p < limit; ++p) {
        int entries = 0;
        const double score = ScoreTocPage(index.Lines(p), index.Chars(p), &entries);
        if (score > seedScore) {
            seedScore   = score;
            seedPage    = p;
            seedEntries = entries;
        }
    }
    best.scannedPages = limit;
    if (seedPage < 0 || seedScore < 0.35 || seedEntries < 3) return best;

    // 2) grow over the run of contiguous TOC pages.
    //    A long table of contents routinely spills onto a second page, and
    //    parsing only the first one silently drops every trailing chapter.
    constexpr int    kMaxTocPages        = 6;
    constexpr int    kMinContinuation    = 3;    // entries needed to continue
    constexpr double kContinuationScore  = 0.20; // continuation pages often lack a "目录" heading

    auto isContinuation = [&](int p) {
        if (p < 0 || p >= index.PageCount()) return false;
        int entries = 0;
        const double score = ScoreTocPage(index.Lines(p), index.Chars(p), &entries);
        return entries >= kMinContinuation && score >= kContinuationScore;
    };

    int first = seedPage;
    while (first - 1 >= 0 && seedPage - (first - 1) < kMaxTocPages &&
           isContinuation(first - 1)) {
        --first;
    }
    int last = seedPage;
    while (last + 1 < index.PageCount() && (last + 1) - seedPage < kMaxTocPages &&
           isContinuation(last + 1)) {
        ++last;
    }

    // 3) concatenate every entry, in page order
    best.pageIndex     = first;
    best.lastPageIndex = last;
    best.score         = seedScore;
    for (int p = first; p <= last; ++p) {
        for (auto& e : ParsePrintedToc(index.Lines(p), index.Chars(p))) {
            best.entries.push_back(std::move(e));
        }
    }

    // 4) reject pages that only look like a TOC line by line
    if (!PlausibleToc(best.entries, index.PageCount())) {
        best.pageIndex     = -1;
        best.lastPageIndex = -1;
        best.entries.clear();
    }
    return best;
}

void AssignLevels(std::vector<PrintedEntry>& entries) {
    if (entries.empty()) return;

    // --- numbering ranks ---------------------------------------------------
    // Producers disagree about what a given numbering style means: "1、" is a
    // second-level heading in some documents and a third-level one in others.
    // So collapse whatever levels the numbering implies onto a contiguous
    // 1..k ranking instead of trusting the raw number.
    std::vector<int> distinct;
    for (const auto& e : entries) {
        if (e.level > 0 &&
            std::find(distinct.begin(), distinct.end(), e.level) == distinct.end()) {
            distinct.push_back(e.level);
        }
    }
    std::sort(distinct.begin(), distinct.end());
    auto rankOf = [&distinct](int lvl) {
        for (size_t i = 0; i < distinct.size(); ++i) {
            if (distinct[i] == lvl) return static_cast<int>(i) + 1;
        }
        return 0;
    };

    // --- indent tiers: geometry, portable but weaker -----------------------
    std::vector<double> lefts;
    lefts.reserve(entries.size());
    for (const auto& e : entries) lefts.push_back(e.left);
    std::sort(lefts.begin(), lefts.end());

    std::vector<double> tiers;
    for (double x : lefts) {
        if (tiers.empty() || x - tiers.back() > 4.0) tiers.push_back(x);
    }
    auto tierOf = [&tiers](double x) {
        int t = 0;
        for (size_t i = 0; i < tiers.size(); ++i) {
            if (x >= tiers[i] - 2.0) t = static_cast<int>(i);
        }
        return t;
    };
    std::set<int> usedTiers;
    for (const auto& e : entries) usedTiers.insert(tierOf(e.left));

    const bool numberingUsable = distinct.size() >= 2;
    const bool indentUsable    = usedTiers.size() >= 2;

    for (size_t i = 0; i < entries.size(); ++i) {
        PrintedEntry& e = entries[i];
        const int ranked = (e.level > 0) ? rankOf(e.level) : 0;

        if (numberingUsable && ranked > 0) {
            e.level = ranked;                       // semantics beat geometry
        } else if (indentUsable) {
            e.level = tierOf(e.left) + 1;           // geometry is all we have
        } else if (ranked > 0) {
            e.level = ranked;
        } else {
            e.level = (i > 0) ? entries[i - 1].level : 1;  // inherit
        }
    }

    // --- normalise: start at 1 and never descend by more than one ----------
    int prev = 1;
    for (size_t i = 0; i < entries.size(); ++i) {
        int& lv = entries[i].level;
        if (lv < 1) lv = 1;
        if (i == 0) {
            lv = 1;
        } else if (lv > prev + 1) {
            lv = prev + 1;
        }
        prev = lv;
    }
}

TocTree PrintedEntriesToToc(PageIndex& index, std::vector<PrintedEntry>& entries,
                            ResolveStats* stats) {
    TocTree tree;
    tree.source = TocSource::PrintedTocPage;
    if (entries.empty()) return tree;

    ResolveStats local;

    // --- L1: /PageLabels gives an authoritative printed -> physical map ----
    std::map<std::string, int> labelToPage;
    if (index.HasPageLabels()) {
        for (int p = 0; p < index.PageCount(); ++p) {
            const std::string& lab = index.Label(p);
            if (!lab.empty() && labelToPage.find(lab) == labelToPage.end()) {
                labelToPage[lab] = p;
            }
        }
    }

    // --- constant offset estimated from title matches ----------------------
    int offset = 0;
    {
        int bestHits = -1;
        for (int off = -5; off <= 20; ++off) {
            int hits = 0, tested = 0;
            for (const auto& e : entries) {
                if (e.printedPage < 0 || tested >= 15) break;
                ++tested;
                const int p = e.printedPage - 1 + off;
                if (p < 0 || p >= index.PageCount()) continue;
                const std::string norm = NormalizeForMatch(e.title);
                if (index.ScoreTitleOnLines(index.Lines(p), norm) >= 0.60) ++hits;
            }
            if (hits > bestHits) {
                bestHits = hits;
                offset   = off;
            }
        }
        if (bestHits <= 0) offset = 0;
    }
    local.offset = offset;

    // --- per-entry resolution ---------------------------------------------
    int titleHits = 0, titleTried = 0;
    std::vector<TocNode> flat;
    flat.reserve(entries.size());

    for (auto& e : entries) {
        int page = -1;
        double confidence = 0.5;

        if (!labelToPage.empty() && e.printedPage >= 0) {
            const auto it = labelToPage.find(std::to_string(e.printedPage));
            if (it != labelToPage.end()) {
                page = it->second;
                confidence = 0.95;
                ++local.resolvedByLabel;
            }
        }

        if (page < 0 && e.printedPage >= 0) {
            const int expected = e.printedPage - 1 + offset;
            if (expected >= 0 && expected < index.PageCount()) {
                page = expected;
                confidence = 0.55;
                ++local.resolvedByOffset;
            }
        }

        // Refine with a title search around the expected page. This both
        // corrects off-by-N front matter and validates the offset.
        if (e.printedPage >= 0) {
            ++titleTried;
            const int expected = (page >= 0) ? page : (e.printedPage - 1 + offset);
            int    found = -1;
            double score = 0.0;
            if (index.FindTitle(e.title, expected - 3, expected + 3, &found, &score,
                                0.75)) {
                if (found != page) ++local.resolvedByTitle;
                page = found;
                confidence = std::max(confidence, 0.6 + 0.4 * score);
                ++titleHits;
            }
        }

        if (page < 0) ++local.unresolved;

        TocNode node;
        node.title       = e.title;
        node.level       = std::max(1, e.level);
        node.targetPage  = page;
        node.printedPage = e.printedPage;
        node.source      = TocSource::PrintedTocPage;
        node.confidence  = confidence;
        flat.push_back(std::move(node));
    }

    // --- build the tree from the flat, level-tagged list -------------------
    // Done in two passes so no pointer into a growing vector is ever held.
    size_t cursor = 0;
    while (cursor < flat.size()) {
        tree.roots.push_back(BuildSubtree(flat, cursor));
    }

    if (titleTried > 0) {
        local.titleHitRate = static_cast<double>(titleHits) / titleTried;
    }
    if (stats) *stats = local;
    return tree;
}

// Public entry point declared in toc.h.
int NumberingLevel(const std::string& title, std::string* prefixOut) {
    return NumberingLevelImpl(title, prefixOut);
}

}  // namespace pre
