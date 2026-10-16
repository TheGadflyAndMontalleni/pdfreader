// toc_build — reconstruct a navigable table of contents for a PDF.
//
//   toc_build <file.pdf> [--json] [--max-scan N] [--force-printed]
//                        [--compare-outline]
//
// --compare-outline additionally builds the document's real /Outlines and
// scores the generated TOC against it. That is only possible for documents
// that ship an outline, which makes those documents a ground-truth set for
// measuring the L2 (printed table of contents) path.

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "doc.h"
#include "toc.h"

namespace {

std::string FormatDouble(double v, int decimals = 2) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

struct CompareResult {
    int    outlineNodes = 0;
    int    generatedNodes = 0;
    int    matched = 0;
    int    targetAgree = 0;
    int    targetComparable = 0;
    double titleRecall = 0.0;
    double titlePrecision = 0.0;
    double targetAgreement = 0.0;
};

CompareResult CompareTrees(const pre::TocTree& truth, const pre::TocTree& gen) {
    CompareResult r;

    std::vector<const pre::TocNode*> a, b;
    truth.Flatten(&a);
    gen.Flatten(&b);
    r.outlineNodes   = static_cast<int>(a.size());
    r.generatedNodes = static_cast<int>(b.size());

    std::map<std::string, const pre::TocNode*> index;
    for (const auto* n : b) {
        index.emplace(pre::NormalizeForMatch(n->title), n);
    }

    for (const auto* n : a) {
        const auto it = index.find(pre::NormalizeForMatch(n->title));
        if (it == index.end()) continue;
        ++r.matched;
        const pre::TocNode* m = it->second;
        if (n->targetPage >= 0 && m->targetPage >= 0) {
            ++r.targetComparable;
            if (n->targetPage == m->targetPage) ++r.targetAgree;
        }
    }
    if (r.outlineNodes > 0) {
        r.titleRecall = static_cast<double>(r.matched) / r.outlineNodes;
    }
    if (r.generatedNodes > 0) {
        r.titlePrecision = static_cast<double>(r.matched) / r.generatedNodes;
    }
    if (r.targetComparable > 0) {
        r.targetAgreement =
            static_cast<double>(r.targetAgree) / r.targetComparable;
    }
    return r;
}

}  // namespace

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);

    if (argc < 2) {
        std::printf(
            "usage: toc_build <file.pdf> [--json] [--max-scan N]\n"
            "                            [--force-printed] [--compare-outline]\n");
        return 2;
    }

    const char* path = argv[1];
    bool json = false, compare = false, forcePrinted = false;
    int  maxScan = 12;
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--json"))                 json = true;
        else if (!std::strcmp(argv[i], "--compare-outline")) compare = true;
        else if (!std::strcmp(argv[i], "--force-printed"))   forcePrinted = true;
        else if (!std::strcmp(argv[i], "--max-scan") && i + 1 < argc) {
            maxScan = std::atoi(argv[++i]);
        }
    }

    pre::Library  lib;
    pre::Document doc;
    std::string   err;
    if (!doc.Open(path, &err)) {
        std::printf("FAIL open: %s\n", err.c_str());
        return 1;
    }

    pre::PageIndex index(doc.handle());
    std::printf("file        : %s\n", path);
    std::printf("pages       : %d\n", index.PageCount());
    std::printf("page labels : %s\n", index.HasPageLabels() ? "present" : "absent");

    const pre::TocTree outline = pre::OutlineToToc(doc.handle());
    std::printf("embedded    : %d outline node(s)\n", outline.NodeCount());

    const pre::PrintedToc printed = pre::FindPrintedToc(index, maxScan);
    if (printed.valid()) {
        if (printed.PageSpan() > 1) {
            std::printf("printed TOC : pages %d-%d (%d pages), %zu entries, score %s\n",
                        printed.pageIndex + 1, printed.lastPageIndex + 1,
                        printed.PageSpan(), printed.entries.size(),
                        FormatDouble(printed.score).c_str());
        } else {
            std::printf("printed TOC : page %d, %zu entries, score %s\n",
                        printed.pageIndex + 1, printed.entries.size(),
                        FormatDouble(printed.score).c_str());
        }
    } else {
        std::printf("printed TOC : not found in first %d page(s) (best score %s)\n",
                    printed.scannedPages, FormatDouble(printed.score).c_str());
    }

    pre::TocOptions opts;
    opts.maxTocScanPages = maxScan;
    if (forcePrinted) opts.useEmbeddedOutline = false;

    pre::ResolveStats stats;
    const pre::TocTree tree = pre::BuildAutoToc(index, opts, &stats);

    if (json) {
        std::printf("%s", pre::TocToJson(tree).c_str());
        return 0;
    }

    std::printf("source      : %s\n", pre::TocSourceName(tree.source));
    std::printf("nodes       : %d   (max depth %d)\n", tree.NodeCount(), tree.MaxDepth());
    if (tree.source == pre::TocSource::PrintedTocPage) {
        std::printf("resolution  : offset=%+d  byLabel=%d byTitle=%d byOffset=%d "
                    "unresolved=%d  titleHitRate=%s\n",
                    stats.offset, stats.resolvedByLabel, stats.resolvedByTitle,
                    stats.resolvedByOffset, stats.unresolved,
                    FormatDouble(stats.titleHitRate).c_str());
    }

    std::printf("\n--- generated table of contents ---\n%s", pre::TocToText(tree).c_str());

    if (compare) {
        const CompareResult r = CompareTrees(outline, tree);
        std::printf("\n--- vs embedded outline (ground truth) ---\n");
        std::printf("  outline nodes   : %d\n", r.outlineNodes);
        std::printf("  generated nodes : %d\n", r.generatedNodes);
        std::printf("  title matched   : %d\n", r.matched);
        std::printf("  title recall    : %s\n", FormatDouble(r.titleRecall, 3).c_str());
        std::printf("  title precision : %s\n", FormatDouble(r.titlePrecision, 3).c_str());
        std::printf("  target agreement: %d/%d = %s\n", r.targetAgree, r.targetComparable,
                    FormatDouble(r.targetAgreement, 3).c_str());
    }
    return 0;
}
