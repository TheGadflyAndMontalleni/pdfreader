// Automatic table-of-contents construction.
//
// The user's requirement is a navigable sidebar TOC for PDFs that ship no
// embedded /Outlines. SumatraPDF has no such capability (its LoadTocTree()
// returns early when the outline is empty), so this is new work.
//
// Evidence sources, tried in order of reliability:
//   L0  embedded /Outlines                     - authoritative when present
//   L1  /PageLabels                            - maps printed numbers to pages
//   L2  a printed table-of-contents page       - strongest for outline-less books
//   L3  typography / numbering heuristics      - last resort, whole document
#pragma once

#include <map>
#include <string>
#include <vector>

#include "fpdfview.h"
#include "text.h"

namespace pre {

enum class TocSource {
    EmbeddedOutline,
    PrintedTocPage,
    Typography,
    Manual,       // reserved for user edits
};

const char* TocSourceName(TocSource s);

struct TocNode {
    std::string title;             // UTF-8, cleaned
    int         level      = 1;    // 1-based depth
    int         targetPage = -1;   // physical page index (0-based); -1 unknown
    int         printedPage = -1;  // page number as printed in the document
    TocSource   source     = TocSource::Typography;
    double      confidence = 0.0;  // 0..1
    std::vector<TocNode> children;

    int DescendantCount() const;
};

struct TocTree {
    std::vector<TocNode> roots;
    TocSource source = TocSource::Typography;

    int NodeCount() const;
    int MaxDepth() const;
    // Flat, depth-first view (useful for validation and serialisation).
    void Flatten(std::vector<const TocNode*>* out) const;
};

// ---------------------------------------------------------------------------
// PageIndex: lazy per-page access with caching, plus /PageLabels support.
// ---------------------------------------------------------------------------
class PageIndex {
public:
    explicit PageIndex(FPDF_DOCUMENT doc);

    int PageCount() const { return pageCount_; }

    FPDF_DOCUMENT document() const { return doc_; }

    // Raw characters for a page, extracted on first use.
    const std::vector<CharInfo>& Chars(int pageIndex);
    // Lines for a page, reconstructed on first use.
    const std::vector<LineInfo>& Lines(int pageIndex);
    // Normalised concatenation of the page's text (for title search).
    const std::string& PageText(int pageIndex);
    // /PageLabels entry for the page ("" when the document defines none).
    const std::string& Label(int pageIndex);

    // Does this document expose any page labels at all?
    bool HasPageLabels() const { return hasLabels_; }

    // Best page in [from, to] whose text contains `title`.
    // Returns false when nothing scores above `minScore`.
    bool FindTitle(const std::string& title, int from, int to, int* pageOut,
                   double* scoreOut, double minScore = 0.60) const;

    // Score how well one page's lines match a title, in [0, 1].
    double ScoreTitleOnLines(const std::vector<LineInfo>& lines,
                             const std::string& normTitle) const;

private:
    FPDF_DOCUMENT doc_ = nullptr;
    int           pageCount_ = 0;
    bool          hasLabels_ = false;
    std::map<int, std::vector<CharInfo>> charsCache_;
    std::map<int, std::vector<LineInfo>> linesCache_;
    std::map<int, std::string>           textCache_;
    std::map<int, std::string>           labelCache_;
};

// ---------------------------------------------------------------------------
// L0: embedded outline
// ---------------------------------------------------------------------------
TocTree OutlineToToc(FPDF_DOCUMENT doc);

// ---------------------------------------------------------------------------
// L2: printed table-of-contents page
// ---------------------------------------------------------------------------
struct PrintedEntry {
    std::string title;         // UTF-8 title without the leader dots / page number
    std::string numbering;     // leading numbering such as "一、" or "（一）"
    int         printedPage = -1;
    double      left = 0.0;    // x of the title start (indent signal)
    int         level = 1;
};

struct PrintedToc {
    int pageIndex = -1;        // physical page holding the printed TOC
    int lastPageIndex = -1;    // last page of a multi-page printed TOC
    double score = 0.0;
    int scannedPages = 0;
    std::vector<PrintedEntry> entries;

    bool valid() const { return pageIndex >= 0 && entries.size() >= 3; }
    int PageSpan() const { return (lastPageIndex >= pageIndex) ? lastPageIndex - pageIndex + 1 : 0; }
};

// How strongly a page looks like a printed table of contents.
// `entryCountOut` receives the number of detected entries.
double ScoreTocPage(const std::vector<LineInfo>& lines,
                    const std::vector<CharInfo>& chars, int* entryCountOut);

// Parse entries from a page already identified as a TOC page.
std::vector<PrintedEntry> ParsePrintedToc(const std::vector<LineInfo>& lines,
                                          const std::vector<CharInfo>& chars);

// Scan the first `maxScan` pages and return the best candidate.
PrintedToc FindPrintedToc(PageIndex& index, int maxScan);

// Assign hierarchy levels using numbering patterns and indent clustering.
void AssignLevels(std::vector<PrintedEntry>& entries);

// Implied hierarchy level from a title's numbering ("第X章" -> 1, "（一）" -> 2,
// "1.2.3" -> 3), or 0 when the title carries no numbering. `prefixOut` is
// optional and receives the numbering text itself.
int NumberingLevel(const std::string& title, std::string* prefixOut = nullptr);

// ---------------------------------------------------------------------------
// Page-number resolution: printed page number -> physical page index
// ---------------------------------------------------------------------------
struct ResolveStats {
    int resolvedByLabel = 0;   // via /PageLabels
    int resolvedByTitle = 0;   // via title search around the expected page
    int resolvedByOffset = 0;  // plain offset that fit the document
    int unresolved = 0;
    int offset = 0;            // physical = printed + offset - 1
    double titleHitRate = 0.0;
    // Set when a printed-TOC candidate was found but discarded as untrustworthy.
    bool rejectedPrintedToc = false;
    std::string rejectReason;
    // L3 diagnostics: how many heading candidates typography found.
    int typographyCandidates = 0;
};

// Fill `printedPage`-based jump targets on the entries (in place) and return
// the resulting tree.
TocTree PrintedEntriesToToc(PageIndex& index, std::vector<PrintedEntry>& entries,
                            ResolveStats* stats);

// ---------------------------------------------------------------------------
// L3: typography across the whole document
// ---------------------------------------------------------------------------
struct TypographyStats {
    int    pagesScanned = 0;
    int    linesSeen = 0;
    int    runningTextDropped = 0;   // repeated headers/footers ignored
    int    candidates = 0;
    double bodyHeight = 0.0;         // modal glyph height of body text
    int    numberedCandidates = 0;
    int    sequentialRun = 0;        // length of the 一、二、三… run recovered
};

// Last resort: find headings by typography alone. Unlike L2 this never reads
// printed page numbers -- a heading's jump target is simply the page it sits
// on, so there is nothing to resolve and nothing to get wrong.
TocTree BuildTypographyToc(PageIndex& index, TypographyStats* stats);

// ---------------------------------------------------------------------------
// Orchestration
// ---------------------------------------------------------------------------
struct TocOptions {
    int  maxTocScanPages = 12;   // how far into the document to look for a printed TOC
    bool useEmbeddedOutline = true;
    bool usePrintedToc = true;
    bool useTypography = true;
};

TocTree BuildAutoToc(PageIndex& index, const TocOptions& opts, ResolveStats* stats);

// Serialise a tree as JSON (UTF-8, no external dependency).
std::string TocToJson(const TocTree& tree);
// Human-readable tree, one node per line with its jump target.
std::string TocToText(const TocTree& tree);

}  // namespace pre
