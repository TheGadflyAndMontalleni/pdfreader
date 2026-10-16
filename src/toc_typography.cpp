// L3: find headings by typography when the document has neither an embedded
// outline nor a usable printed table of contents.
//
// The advantage over L2 is structural: a heading's jump target is the page it
// appears on, so nothing has to be resolved and no misread page number can
// point the reader at the wrong place. The risk is the opposite one -- body
// text promoted to a heading -- so the gate below is deliberately narrow.
//
// Signals, in rough order of how much they can be trusted:
//   glyphHeight vs the document's body height   (geometric; survives bogus
//                                                font sizes, which PDFium
//                                                reports for some producers)
//   bold                                        (from weight or font name)
//   numbering prefix (一、/（一）/1.2.3/第X章)
//   indent relative to the body's left margin
//   line shorter than the body column
//   not ending in sentence punctuation
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "toc.h"

namespace pre {

namespace {

// Glyph heights are bucketed at 0.5pt so near-identical values agree.
int Bucket(double h) { return static_cast<int>(std::lround(h * 2.0)); }

bool EndsWithSentencePunct(const std::string& utf8) {
    const auto cps = Utf8Decode(utf8);
    if (cps.empty()) return false;
    switch (cps.back()) {
        case 0x3002:  // 。
        case 0xFF1B:  // ；
        case 0xFF0C:  // ，
        case 0x3001:  // 、
        case 0xFF1A:  // ：
        case '.':
        case ';':
        case ',':
        case ':':
            return true;
        default:
            return false;
    }
}

bool IsCjk(uint32_t c) {
    return (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF) ||
           (c >= 0x3040 && c <= 0x30FF) || (c >= 0xAC00 && c <= 0xD7AF);
}

bool IsAsciiAlpha(uint32_t c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

// A heading carries words. Page numbers ("15"), stray fragments ("63f") and
// single glyphs are table or furniture text that happens to be set apart.
bool LooksLikeWords(const std::string& utf8) {
    int words = 0;
    for (uint32_t c : Utf8Decode(utf8)) {
        if (IsCjk(c) || IsAsciiAlpha(c)) ++words;
    }
    return words >= 2;
}

// "2025年10月31日" and friends are dates, which appear set apart on the page
// for reasons that have nothing to do with document structure.
bool LooksLikeADate(const std::string& utf8) {
    int digits = 0, dateMarks = 0, other = 0;
    for (uint32_t c : Utf8Decode(utf8)) {
        if (c >= '0' && c <= '9') ++digits;
        else if (c == 0x5E74 || c == 0x6708 || c == 0x65E5 ||   // 年 月 日
                 c == '-' || c == '/' || c == '.' || c == 0x2014) {
            ++dateMarks;
        } else if (c != ' ') {
            ++other;
        }
    }
    return digits >= 4 && other == 0 && dateMarks >= 1;
}

// Parse a leading cardinal number, CJK (一..十, 十一, 二十) or ASCII.
bool ParseCardinal(const std::vector<uint32_t>& cps, size_t* i, int* out) {
    int  val = 0;
    bool any = false;
    while (*i < cps.size()) {
        const uint32_t c = cps[*i];
        int d = -1;
        switch (c) {
            case 0x4E00: d = 1;  break;
            case 0x4E8C: d = 2;  break;
            case 0x4E09: d = 3;  break;
            case 0x56DB: d = 4;  break;
            case 0x4E94: d = 5;  break;
            case 0x516D: d = 6;  break;
            case 0x4E03: d = 7;  break;
            case 0x516B: d = 8;  break;
            case 0x4E5D: d = 9;  break;
            case 0x5341: d = 10; break;
            default: break;
        }
        if (d < 0 && c >= '0' && c <= '9') d = static_cast<int>(c - '0');
        if (d < 0) break;
        val = val * 10 + d;
        any = true;
        ++(*i);
    }
    if (!any) return false;
    *out = val;
    return true;
}

// A top-level section number: "一、" / "三." / "第2章" / "第五部分".
//
// This is the one signal that survives a document whose headings are set in
// exactly the body type. Individually such a line is indistinguishable from a
// numbered list item; collectively, an increasing run of them across the whole
// document *is* the table of contents the producer never wrote down.
bool TopLevelOrdinal(const std::string& utf8, int* valueOut) {
    const auto cps = Utf8Decode(utf8);
    size_t i = 0;
    while (i < cps.size() && (cps[i] == ' ' || cps[i] == 0x3000)) ++i;
    if (i >= cps.size()) return false;

    if (cps[i] == 0x7B2C) {   // 第
        size_t j = i + 1;
        int    val = 0;
        if (!ParseCardinal(cps, &j, &val)) return false;
        if (j < cps.size() && (cps[j] == 0x7AE0 ||   // 章
                               cps[j] == 0x8282 ||   // 节
                               cps[j] == 0x90E8)) {  // 部
            *valueOut = val;
            return true;
        }
        return false;
    }

    size_t j = i;
    int    val = 0;
    if (!ParseCardinal(cps, &j, &val)) return false;
    if (j < cps.size() && (cps[j] == 0x3001 ||    // 、
                           cps[j] == 0xFF0E ||    // ．
                           cps[j] == '.' || cps[j] == 0xFF09)) {  // ）
        *valueOut = val;
        return true;
    }
    return false;
}

struct Candidate {
    int         page = 0;
    int         line = 0;
    double      hRatio = 1.0;
    int         numbering = 0;
    bool        bold = false;
    double      score = 0.0;
    std::string title;

    bool operator<(const Candidate& o) const {
        return page != o.page ? page < o.page : line < o.line;
    }
};

// A line carrying a top-level section number, remembered so the sequential run
// can be recovered after the per-line gate has had its say.
struct OrdinalLine {
    int         page = 0;
    int         line = 0;
    int         ordinal = 0;
    std::string title;
};

// Consume flat[i] and every following node deeper than it, recursively.
TocNode BuildSubtree(const std::vector<TocNode>& flat, size_t& i) {
    TocNode   node  = flat[i];
    const int level = flat[i].level;
    ++i;
    while (i < flat.size() && flat[i].level > level) {
        node.children.push_back(BuildSubtree(flat, i));
    }
    return node;
}

// Turn a flat list of candidates into a tree. Numbering, when it carries at
// least two distinct depths, is authoritative; otherwise fall back to glyph
// height tiers, which is what visually separates a heading from a subheading.
void AssignTypographyLevels(std::vector<Candidate>& cands,
                             std::vector<TocNode>* out) {
    out->clear();
    if (cands.empty()) return;

    std::set<int> numberingLevels;
    for (const auto& c : cands) {
        if (c.numbering > 0) numberingLevels.insert(c.numbering);
    }
    const bool useNumbering = numberingLevels.size() >= 2;

    std::map<int, int> rankOf;
    if (useNumbering) {
        int rank = 1;
        for (int lvl : numberingLevels) rankOf[lvl] = rank++;
    } else {
        // Distinct glyph-height tiers, tallest first.
        std::vector<int> buckets;
        for (const auto& c : cands) buckets.push_back(Bucket(c.hRatio * 100.0));
        std::sort(buckets.begin(), buckets.end(), std::greater<int>());
        buckets.erase(std::unique(buckets.begin(), buckets.end()), buckets.end());
        int rank = 1;
        for (int b : buckets) rankOf[b] = rank++;
    }

    for (auto& c : cands) {
        TocNode n;
        n.title      = c.title;
        n.level      = useNumbering ? rankOf[c.numbering]
                                    : rankOf[Bucket(c.hRatio * 100.0)];
        n.targetPage = c.page;
        n.source     = TocSource::Typography;
        n.confidence = std::min(1.0, c.score);
        out->push_back(std::move(n));
    }

    // Normalise: the first node defines depth 1 and no step may exceed +1.
    int prev = 0;
    for (auto& n : *out) {
        if (prev == 0) n.level = 1;
        else if (n.level > prev + 1) n.level = prev + 1;
        else if (n.level < 1) n.level = 1;
        prev = n.level;
    }
}

}  // namespace

TocTree BuildTypographyToc(PageIndex& index, TypographyStats* statsOut) {
    TocTree tree;
    tree.source = TocSource::Typography;

    TypographyStats stats;
    const int pageCount = index.PageCount();
    stats.pagesScanned = pageCount;
    if (pageCount <= 0) {
        if (statsOut) *statsOut = stats;
        return tree;
    }

    // ---- pass 1: how does this document's body text look? ----
    std::map<int, long long> heightHistogram;   // bucket -> non-space chars
    std::map<int, long long> leftHistogram;     // rounded left edge -> chars
    std::map<std::string, std::set<int>> textPages;
    long long bodyChars = 0, bodyBoldChars = 0;

    for (int p = 0; p < pageCount; ++p) {
        const auto& lines = index.Lines(p);
        for (const auto& L : lines) {
            if (L.glyphHeight <= 0.0) continue;
            long long chars = 0;
            for (int ci : L.charIndices) {
                if (ci >= 0 && ci < static_cast<int>(index.Chars(p).size()) &&
                    !index.Chars(p)[static_cast<size_t>(ci)].IsSpace()) {
                    ++chars;
                }
            }
            if (chars == 0) continue;
            heightHistogram[Bucket(L.glyphHeight)] += chars;
            leftHistogram[static_cast<int>(std::lround(L.left))] += chars;
            stats.linesSeen++;

            const std::string key = NormalizeForMatch(L.text);
            if (!key.empty() && Utf8Decode(key).size() <= 60) {
                textPages[key].insert(p);
            }
        }
    }

    if (heightHistogram.empty()) {
        if (statsOut) *statsOut = stats;
        return tree;
    }

    int bestBucket = 0;
    long long bestCount = -1;
    for (const auto& kv : heightHistogram) {
        if (kv.second > bestCount) {
            bestCount = kv.second;
            bestBucket = kv.first;
        }
    }
    const double bodyHeight = bestBucket / 2.0;
    stats.bodyHeight = bodyHeight;

    // Is the body text itself bold? In a document set entirely in a bold face,
    // "bold" carries no information at all -- treating it as a heading signal
    // promotes every paragraph. The signal only counts when the body is not
    // bold, exactly as glyph height only counts relative to body height.
    for (int p = 0; p < pageCount; ++p) {
        for (const auto& L : index.Lines(p)) {
            if (L.glyphHeight <= 0.0) continue;
            if (std::fabs(L.glyphHeight - bodyHeight) > bodyHeight * 0.10) continue;
            long long chars = 0;
            for (int ci : L.charIndices) {
                if (ci >= 0 && ci < static_cast<int>(index.Chars(p).size()) &&
                    !index.Chars(p)[static_cast<size_t>(ci)].IsSpace()) {
                    ++chars;
                }
            }
            bodyChars += chars;
            if (L.bold) bodyBoldChars += chars;
        }
    }
    const bool bodyIsBold = bodyChars > 0 && (bodyBoldChars * 2 > bodyChars);

    int bestLeft = 0;
    bestCount = -1;
    for (const auto& kv : leftHistogram) {
        if (kv.second > bestCount) {
            bestCount = kv.second;
            bestLeft = kv.first;
        }
    }
    const double bodyLeft = bestLeft;

    // Running headers/footers repeat on page after page; treating one as a
    // heading would put the same entry in the TOC dozens of times.
    const size_t repeatThreshold =
        std::max<size_t>(3, static_cast<size_t>(pageCount * 0.25));
    std::set<std::string> runningText;
    for (const auto& kv : textPages) {
        if (kv.second.size() >= repeatThreshold) runningText.insert(kv.first);
    }

    // ---- pass 2: collect heading candidates ----
    std::vector<Candidate>   cands;
    std::vector<OrdinalLine> ordinals;
    constexpr int kMaxCandidates = 500;

    for (int p = 0; p < pageCount && static_cast<int>(cands.size()) < kMaxCandidates; ++p) {
        const auto& lines = index.Lines(p);

        // Local baseline: the height most lines on THIS page share.
        //
        // A single document-wide body height is not enough. In a table-heavy
        // report the character-weighted mode is the table text (say 7.5pt), so
        // narrative paragraphs at 15pt come out at "2x body" and every one of
        // them looks like a heading. Comparing against the page's own median
        // keeps paragraphs unremarkable while a genuine heading, which is set
        // apart from the text around it, still stands out.
        std::map<int, int> pageHeightCount;
        for (const auto& L : lines) {
            if (L.glyphHeight > 0.0) pageHeightCount[Bucket(L.glyphHeight)]++;
        }
        int localBucket = bestBucket;
        int localCount  = -1;
        for (const auto& kv : pageHeightCount) {
            if (kv.second > localCount) {
                localCount  = kv.second;
                localBucket = kv.first;
            }
        }
        const double localBody = localBucket / 2.0;

        // Column width of body-sized text on this page, for the "short line" test.
        double bodyColumn = 0.0;
        for (const auto& L : lines) {
            if (L.glyphHeight <= 0.0) continue;
            if (std::fabs(L.glyphHeight - localBody) <= localBody * 0.10) {
                bodyColumn = std::max(bodyColumn, L.Width());
            }
        }
        if (bodyColumn <= 0.0) {
            for (const auto& L : lines) bodyColumn = std::max(bodyColumn, L.Width());
        }

        // Typical vertical gap between lines on this page. A heading is set off
        // from the text around it; a numbered list item inside a paragraph is
        // not. This is the signal that separates them when size and weight are
        // identical to the body.
        std::vector<double> gaps;
        for (size_t i = 1; i < lines.size(); ++i) {
            const double g = lines[i - 1].bottom - lines[i].top;
            if (g > 0) gaps.push_back(g);
        }
        double medianGap = 0.0;
        if (!gaps.empty()) {
            std::sort(gaps.begin(), gaps.end());
            medianGap = gaps[gaps.size() / 2];
        }

        std::string prevAccepted;
        for (size_t li = 0; li < lines.size(); ++li) {
            const auto& L = lines[li];
            if (L.glyphHeight <= 0.0 || L.text.empty()) continue;

            const std::string key = NormalizeForMatch(L.text);
            if (key.empty()) continue;
            if (runningText.count(key)) {
                stats.runningTextDropped++;
                continue;
            }
            if (key == prevAccepted) continue;   // same heading split over two lines
            if (!LooksLikeWords(L.text)) continue;
            if (LooksLikeADate(L.text)) continue;

            // Remember top-level numbering even when the line fails the gate
            // below, so the whole sequence can be judged at once.
            int ordinal = 0;
            if (TopLevelOrdinal(L.text, &ordinal)) {
                ordinals.push_back(OrdinalLine{p, static_cast<int>(li), ordinal, L.text});
            }

            const double hRatio = localBody > 0.0 ? L.glyphHeight / localBody
                                                  : L.glyphHeight / bodyHeight;
            const bool   boldSignal = L.bold && !bodyIsBold;
            const int    numbering = NumberingLevel(L.text);

            // Four or more lines sharing this exact height is a run of body
            // text (a paragraph, a table body), never a heading.
            const bool bulkText = pageHeightCount[Bucket(L.glyphHeight)] >= 4;

            const double indentDelta = L.left - bodyLeft;
            const bool   shortLine = bodyColumn > 0.0 && L.Width() <= bodyColumn * 0.80;

            const double gapAbove = (li > 0) ? (lines[li - 1].bottom - L.top) : medianGap;
            const double gapBelow = (li + 1 < lines.size())
                                        ? (L.bottom - lines[li + 1].top)
                                        : medianGap;
            const bool isolated = medianGap > 0.0 &&
                                  (gapAbove > medianGap * 1.5 || gapBelow > medianGap * 1.5);

            const bool typographic = ((hRatio >= 1.15) || boldSignal) && !bulkText;
            const bool structural  = (numbering >= 1) && shortLine && !bulkText &&
                                     (indentDelta >= 8.0 || isolated);
            if (!typographic && !structural) continue;

            double score = 0.0;
            if (hRatio >= 2.0)       score += 0.60;
            else if (hRatio >= 1.4)  score += 0.50;
            else if (hRatio >= 1.15) score += 0.38;
            if (boldSignal)    score += 0.22;
            if (numbering >= 1) score += 0.22;
            if (shortLine)     score += 0.10;
            if (isolated)      score += 0.15;
            if (!EndsWithSentencePunct(L.text)) score += 0.06;
            if (indentDelta >= 8.0) score += 0.06;

            if (score < 0.50) continue;

            Candidate c;
            c.page      = p;
            c.line      = static_cast<int>(li);
            c.hRatio    = hRatio;
            c.numbering = numbering;
            c.bold      = boldSignal;
            c.score     = score;
            c.title     = L.text;
            // Trim trailing spaces that extraction sometimes leaves behind.
            while (!c.title.empty() && (c.title.back() == ' ' || c.title.back() == '\t')) {
                c.title.pop_back();
            }
            cands.push_back(std::move(c));
            prevAccepted = key;
            if (numbering >= 1) stats.numberedCandidates++;
            if (static_cast<int>(cands.size()) >= kMaxCandidates) break;
        }
    }

    stats.candidates = static_cast<int>(cands.size());

    // ---- pass 3: recover a sequential top-level numbering run ----
    //
    // Lines that the gate rejected may still form a clean 一、二、三… (or 第1章,
    // 第2章 …) progression across the document. A numbered list inside one
    // paragraph cannot produce that; a document's chapters can. The run is
    // accepted as level-1 headings rather than being thrown away.
    {
        std::sort(ordinals.begin(), ordinals.end(),
                  [](const OrdinalLine& a, const OrdinalLine& b) {
                      return a.page != b.page ? a.page < b.page : a.line < b.line;
                  });

        // Longest strictly increasing subsequence, greedily: ordinals appear in
        // reading order, so taking each next larger one is sufficient here.
        std::vector<size_t> run;
        int last = 0;
        for (size_t i = 0; i < ordinals.size(); ++i) {
            if (ordinals[i].ordinal > last) {
                run.push_back(i);
                last = ordinals[i].ordinal;
            }
        }

        if (run.size() >= 3) {
            std::set<std::pair<int, int>> already;
            for (const auto& c : cands) already.insert({c.page, c.line});
            for (size_t idx : run) {
                const OrdinalLine& o = ordinals[idx];
                if (already.count({o.page, o.line})) continue;
                Candidate c;
                c.page      = o.page;
                c.line      = o.line;
                c.hRatio    = 1.0;          // not visually distinguished
                c.numbering = 1;            // it is a top-level section
                c.bold      = false;
                c.score     = 0.55;
                c.title     = o.title;
                while (!c.title.empty() &&
                       (c.title.back() == ' ' || c.title.back() == '\t')) {
                    c.title.pop_back();
                }
                cands.push_back(std::move(c));
            }
            std::sort(cands.begin(), cands.end());
            stats.sequentialRun = static_cast<int>(run.size());
        }
    }

    stats.candidates = static_cast<int>(cands.size());
    if (statsOut) *statsOut = stats;

    if (cands.size() < 3) return tree;   // too little to call a structure

    std::vector<TocNode> flat;
    AssignTypographyLevels(cands, &flat);

    size_t i = 0;
    while (i < flat.size()) tree.roots.push_back(BuildSubtree(flat, i));
    return tree;
}

}  // namespace pre
