#include "toc.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "fpdf_doc.h"
#include "doc.h"

namespace pre {
namespace {

// Minimum number of Unicode scalars for a title to be worth searching for.
constexpr size_t kMinTitleScalars = 3;

std::string EscapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

std::string TrimAscii(const std::string& s) {
    size_t b = 0, e = s.size();
    auto isSpace = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    while (b < e && isSpace(s[b])) ++b;
    while (e > b && isSpace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

void WriteNodeJson(const TocNode& n, int depth, std::string* out) {
    const std::string pad(static_cast<size_t>(depth) * 2, ' ');
    *out += pad + "{\n";
    *out += pad + "  \"title\": \"" + EscapeJson(n.title) + "\",\n";
    *out += pad + "  \"level\": " + std::to_string(n.level) + ",\n";
    *out += pad + "  \"targetPage\": " + std::to_string(n.targetPage) + ",\n";
    *out += pad + "  \"printedPage\": " + std::to_string(n.printedPage) + ",\n";
    *out += pad + "  \"source\": \"" + TocSourceName(n.source) + "\",\n";
    char conf[32];
    std::snprintf(conf, sizeof(conf), "%.3f", n.confidence);
    *out += pad + "  \"confidence\": " + conf;
    if (n.children.empty()) {
        *out += "\n" + pad + "}";
        return;
    }
    *out += ",\n" + pad + "  \"children\": [\n";
    for (size_t i = 0; i < n.children.size(); ++i) {
        WriteNodeJson(n.children[i], depth + 2, out);
        if (i + 1 < n.children.size()) *out += ",";
        *out += "\n";
    }
    *out += pad + "  ]\n" + pad + "}";
}

void WriteNodeText(const TocNode& n, std::string* out) {
    const std::string pad(static_cast<size_t>(n.level - 1) * 2, ' ');
    char target[64];
    if (n.targetPage >= 0) {
        std::snprintf(target, sizeof(target), "p%d", n.targetPage + 1);
    } else {
        std::snprintf(target, sizeof(target), "??");
    }
    char extra[128] = "";
    if (n.printedPage >= 0 && n.targetPage >= 0 &&
        n.printedPage != n.targetPage + 1) {
        std::snprintf(extra, sizeof(extra), "  (printed %d)", n.printedPage);
    }
    *out += pad + "- " + n.title + "  [" + target + extra + "]\n";
    for (const auto& c : n.children) WriteNodeText(c, out);
}

}  // namespace

const char* TocSourceName(TocSource s) {
    switch (s) {
        case TocSource::EmbeddedOutline: return "embedded-outline";
        case TocSource::PrintedTocPage:  return "printed-toc-page";
        case TocSource::Typography:      return "typography";
        case TocSource::Manual:          return "manual";
    }
    return "unknown";
}

int TocNode::DescendantCount() const {
    int n = 0;
    for (const auto& c : children) n += 1 + c.DescendantCount();
    return n;
}

int TocTree::NodeCount() const {
    int n = 0;
    for (const auto& r : roots) n += 1 + r.DescendantCount();
    return n;
}

int TocTree::MaxDepth() const {
    int best = 0;
    for (const auto& r : roots) {
        int d = 1;
        for (const auto& c : r.children) d = std::max(d, 1 + c.level);
        best = std::max(best, d);
    }
    for (const auto& r : roots) {
        // children carry their own absolute level; use the max seen
        int d = r.level;
        std::vector<const TocNode*> stack{&r};
        while (!stack.empty()) {
            const TocNode* n = stack.back();
            stack.pop_back();
            d = std::max(d, n->level);
            for (const auto& c : n->children) stack.push_back(&c);
        }
        best = std::max(best, d);
    }
    return best;
}

void TocTree::Flatten(std::vector<const TocNode*>* out) const {
    if (!out) return;
    std::vector<const TocNode*> stack;
    for (auto it = roots.rbegin(); it != roots.rend(); ++it) stack.push_back(&*it);
    while (!stack.empty()) {
        const TocNode* n = stack.back();
        stack.pop_back();
        out->push_back(n);
        for (auto it = n->children.rbegin(); it != n->children.rend(); ++it) {
            stack.push_back(&*it);
        }
    }
}

// ---------------------------------------------------------------------------
// PageIndex
// ---------------------------------------------------------------------------

PageIndex::PageIndex(FPDF_DOCUMENT doc) : doc_(doc) {
    pageCount_ = doc_ ? FPDF_GetPageCount(doc_) : 0;
    // /PageLabels is a document-level array; probing the first pages tells us
    // whether the producer supplied one at all.
    for (int i = 0; i < pageCount_ && i < 3; ++i) {
        if (!Label(i).empty()) { hasLabels_ = true; break; }
    }
}

const std::vector<CharInfo>& PageIndex::Chars(int pageIndex) {
    auto it = charsCache_.find(pageIndex);
    if (it != charsCache_.end()) return it->second;
    return charsCache_.emplace(pageIndex, ExtractPageChars(doc_, pageIndex))
        .first->second;
}

const std::vector<LineInfo>& PageIndex::Lines(int pageIndex) {
    auto it = linesCache_.find(pageIndex);
    if (it != linesCache_.end()) return it->second;

    std::vector<LineInfo> lines = BuildLines(pageIndex, Chars(pageIndex));
    return linesCache_.emplace(pageIndex, std::move(lines)).first->second;
}

const std::string& PageIndex::PageText(int pageIndex) {
    auto it = textCache_.find(pageIndex);
    if (it != textCache_.end()) return it->second;

    std::string text;
    for (const auto& L : Lines(pageIndex)) text += NormalizeForMatch(L.text);
    return textCache_.emplace(pageIndex, std::move(text)).first->second;
}

const std::string& PageIndex::Label(int pageIndex) {
    auto it = labelCache_.find(pageIndex);
    if (it != labelCache_.end()) return it->second;

    std::string label;
    if (doc_ && pageIndex >= 0 && pageIndex < pageCount_) {
        const unsigned long need = FPDF_GetPageLabel(doc_, pageIndex, nullptr, 0);
        if (need > 1) {
            std::vector<unsigned short> buf(need / sizeof(unsigned short), 0);
            FPDF_GetPageLabel(doc_, pageIndex, buf.data(), need);
            // Convert UTF-16LE to UTF-8.
            for (size_t i = 0; i < buf.size(); ++i) {
                uint32_t cp = buf[i];
                if (cp == 0) break;
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < buf.size() &&
                    buf[i + 1] >= 0xDC00 && buf[i + 1] <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (buf[i + 1] - 0xDC00);
                    ++i;
                }
                label += Utf8Encode(cp);
            }
            label = TrimAscii(label);
        }
    }
    return labelCache_.emplace(pageIndex, std::move(label)).first->second;
}

double PageIndex::ScoreTitleOnLines(const std::vector<LineInfo>& lines,
                                    const std::string& normTitle) const {
    const size_t titleLen = Utf8Decode(normTitle).size();
    if (titleLen < kMinTitleScalars) return 0.0;

    double best = 0.0;
    for (const auto& L : lines) {
        const std::string nl = NormalizeForMatch(L.text);
        if (nl.empty()) continue;
        if (nl.find(normTitle) != std::string::npos) return 1.0;  // exact containment

        const size_t cp = CommonPrefixLength(nl, normTitle);
        const double s = static_cast<double>(cp) / static_cast<double>(titleLen);
        if (s > best) best = s;
    }
    return best;
}

bool PageIndex::FindTitle(const std::string& title, int from, int to, int* pageOut,
                          double* scoreOut, double minScore) const {
    const std::string norm = NormalizeForMatch(title);
    if (Utf8Decode(norm).size() < kMinTitleScalars) return false;

    from = std::max(0, from);
    to   = std::min(pageCount_ - 1, to);
    int    bestPage = -1;
    double bestScore = minScore;
    for (int p = from; p <= to; ++p) {
        // const_cast: Lines() caches, which is an implementation detail of a
        // logically read-only lookup.
        const auto& lines = const_cast<PageIndex*>(this)->Lines(p);
        const double s = ScoreTitleOnLines(lines, norm);
        if (s > bestScore) {
            bestScore = s;
            bestPage  = p;
        }
    }
    if (bestPage < 0) return false;
    if (pageOut)  *pageOut = bestPage;
    if (scoreOut) *scoreOut = bestScore;
    return true;
}

// ---------------------------------------------------------------------------
// L0: embedded outline
// ---------------------------------------------------------------------------

namespace {

std::string BookmarkTitle(FPDF_DOCUMENT doc, FPDF_BOOKMARK bm) {
    const unsigned long need = FPDFBookmark_GetTitle(bm, nullptr, 0);
    if (need <= 2) return std::string();
    std::vector<unsigned short> buf(need / sizeof(unsigned short), 0);
    FPDFBookmark_GetTitle(bm, buf.data(), need);

    std::string out;
    for (size_t i = 0; i < buf.size(); ++i) {
        uint32_t cp = buf[i];
        if (cp == 0) break;
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < buf.size() &&
            buf[i + 1] >= 0xDC00 && buf[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (buf[i + 1] - 0xDC00);
            ++i;
        }
        out += Utf8Encode(cp);
    }
    return TrimAscii(out);
}

void AppendBookmarks(FPDF_DOCUMENT doc, FPDF_BOOKMARK first, int level,
                     std::vector<TocNode>* out) {
    for (FPDF_BOOKMARK bm = first; bm != nullptr;
         bm = FPDFBookmark_GetNextSibling(doc, bm)) {
        TocNode node;
        node.title  = BookmarkTitle(doc, bm);
        node.level  = level;
        node.source = TocSource::EmbeddedOutline;
        node.confidence = 1.0;

        if (FPDF_DEST dest = FPDFBookmark_GetDest(doc, bm)) {
            const int page = FPDFDest_GetDestPageIndex(doc, dest);
            if (page >= 0) {
                node.targetPage  = page;
                node.printedPage = page + 1;
            }
        }
        if (node.title.empty()) node.title = "(untitled)";

        AppendBookmarks(doc, FPDFBookmark_GetFirstChild(doc, bm), level + 1,
                        &node.children);
        out->push_back(std::move(node));
    }
}

}  // namespace

TocTree OutlineToToc(FPDF_DOCUMENT doc) {
    TocTree tree;
    tree.source = TocSource::EmbeddedOutline;
    if (!doc) return tree;
    AppendBookmarks(doc, FPDFBookmark_GetFirstChild(doc, nullptr), 1, &tree.roots);
    return tree;
}

// ---------------------------------------------------------------------------
// Orchestration
// ---------------------------------------------------------------------------

TocTree BuildAutoToc(PageIndex& index, const TocOptions& opts, ResolveStats* stats) {
    TocTree tree;

    // L0 - embedded outline wins outright when the document has one.
    if (opts.useEmbeddedOutline) {
        tree = OutlineToToc(index.document());
        if (!tree.roots.empty()) return tree;
    }

    // L2 - a printed table of contents.
    if (opts.usePrintedToc) {
        PrintedToc pt = FindPrintedToc(index, opts.maxTocScanPages);
        if (pt.valid()) {
            AssignLevels(pt.entries);
            ResolveStats local;
            TocTree candidate = PrintedEntriesToToc(index, pt.entries, &local);

            // A generated TOC is only useful if its entries actually land on
            // the pages they claim. When the title search barely confirms
            // anything, the "page numbers" were almost certainly a numeric
            // column, so emit nothing rather than a misleading tree.
            const bool trustworthy =
                local.titleHitRate >= 0.35 || local.unresolved == 0;
            if (candidate.NodeCount() >= 3 && trustworthy) {
                if (stats) *stats = local;
                return candidate;
            }
            if (stats) {
                stats->rejectedPrintedToc = true;
                stats->rejectReason = "titleHitRate=" + std::to_string(local.titleHitRate) +
                                      " unresolved=" + std::to_string(local.unresolved);
            }
        }
    }

    // L3 - typography across the whole document. Last resort: it cannot point
    // at the wrong page (a heading's target is the page it sits on) but it can
    // promote body text, so it only runs when nothing better was found.
    if (opts.useTypography) {
        TypographyStats tstats;
        TocTree t = BuildTypographyToc(index, &tstats);
        if (stats) stats->typographyCandidates = tstats.candidates;
        if (t.NodeCount() >= 3) return t;
    }

    tree.source = TocSource::Typography;
    return tree;
}

std::string TocToJson(const TocTree& tree) {
    std::string out = "{\n  \"source\": \"";
    out += TocSourceName(tree.source);
    out += "\",\n  \"nodeCount\": ";
    out += std::to_string(tree.NodeCount());
    out += ",\n  \"roots\": [\n";
    for (size_t i = 0; i < tree.roots.size(); ++i) {
        WriteNodeJson(tree.roots[i], 2, &out);
        if (i + 1 < tree.roots.size()) out += ",";
        out += "\n";
    }
    out += "  ]\n}\n";
    return out;
}

std::string TocToText(const TocTree& tree) {
    std::string out;
    for (const auto& r : tree.roots) WriteNodeText(r, &out);
    return out;
}

}  // namespace pre
