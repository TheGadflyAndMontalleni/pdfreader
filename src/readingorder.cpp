#include "readingorder.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace pre {
namespace {

constexpr double infinity = std::numeric_limits<double>::infinity();

struct Run {
    double baseline, left = infinity, right = -infinity, bottom = infinity, top = -infinity;
    std::vector<int> indices;
};
using Runs = std::vector<const Run*>;

bool Space(const CharInfo& c) {
    return c.IsSpace() || c.codepoint == 0x3000 ||
           (c.codepoint >= 0x2000 && c.codepoint <= 0x200a);
}

double Baseline(const CharInfo& c) {
    return c.hasOrigin ? c.originY : c.bottom;
}

// Estimate column gutters from ink. A PDF producer may write objects in any
// order or place one enormous generated space between the two columns.
std::vector<Run> MakeRuns(const std::vector<CharInfo>& chars, double* gutter) {
    std::vector<int> order;
    std::vector<double> heights;
    for (int i = 0; i < static_cast<int>(chars.size()); ++i) {
        const auto& c = chars[i];
        if (ReadingCodepoint(c) < 0x20 || c.codepoint == 0x7f) continue;
        order.push_back(i);
        if (!Space(c) && c.Height() > 0) heights.push_back(c.Height());
    }
    if (heights.empty()) return {};
    auto middle = heights.begin() + heights.size() / 2;
    std::nth_element(heights.begin(), middle, heights.end());
    const double height = *middle;
    *gutter = std::max(2.0, 1.15 * height);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return Baseline(chars[a]) > Baseline(chars[b]); });

    std::vector<Run> runs;
    for (auto row = order.begin(); row != order.end();) {
        const double baseline = Baseline(chars[*row]);
        auto end = std::find_if(row + 1, order.end(), [&](int i) {
            return baseline - Baseline(chars[i]) > 0.35 * height;
        });
        std::sort(row, end, [&](int a, int b) {
            const double x = chars[a].hasOrigin ? chars[a].originX : chars[a].left;
            const double otherX = chars[b].hasOrigin ? chars[b].originX : chars[b].left;
            return x != otherX ? x < otherX : a < b;
        });
        Run run{baseline};
        bool wordSpace = false;
        for (auto it = row; it != end; ++it) {
            const auto& c = chars[*it];
            if (Space(c)) {
                wordSpace |= !c.generated;
            } else {
                // A typeset word space can be wide, especially in justified
                // or monospace text. Keep it unless the gap is exceptional.
                const double em = std::clamp(c.fontSize, height, 2.5 * height);
                const double gap = std::max(wordSpace ? 2 * *gutter : *gutter, 0.82 * em);
                if (run.left <= run.right && c.left - run.right >= gap) {
                    runs.push_back(std::move(run));
                    run = Run{baseline};
                }
                run.left = std::min(run.left, c.left);
                run.right = std::max(run.right, c.right);
                run.bottom = std::min(run.bottom, c.bottom);
                run.top = std::max(run.top, c.top);
                wordSpace = false;
            }
            run.indices.push_back(*it);
        }
        if (run.left <= run.right) runs.push_back(std::move(run));
        row = end;
    }
    return runs;
}

// Each right-hand run votes for the empty interval to its nearest left
// neighbour. Include nearby baselines because adjacent columns need not align.
// Two independent votes must overlap by at least one gutter width.
std::optional<double> FindGutter(const Runs& runs, double gutter) {
    if (runs.size() < 3 || runs.front()->baseline == runs.back()->baseline) return {};
    std::vector<std::pair<double, int>> edges;
    size_t first = 0;
    for (const auto* r : runs) {
        while (runs[first]->baseline - r->baseline > 1.8 * gutter) ++first;
        double leftEnd = -infinity;
        for (size_t j = first; j < runs.size() && r->baseline - runs[j]->baseline <= 1.8 * gutter; ++j) {
            const auto* other = runs[j];
            if (other->right <= r->left - gutter) leftEnd = std::max(leftEnd, other->right);
        }
        if (leftEnd != -infinity) {
            edges.emplace_back(leftEnd + gutter / 2, 1);
            edges.emplace_back(r->left - gutter / 2, -1);
        }
    }
    std::sort(edges.begin(), edges.end());
    std::optional<double> cut;
    int support = 0, best = 1;
    double widest = 0;
    for (size_t i = 0; i < edges.size();) {
        const double x = edges[i].first;
        do { support += edges[i++].second; } while (i < edges.size() && edges[i].first == x);
        if (i == edges.size()) break;
        const double width = edges[i].first - x;
        if (support >= 2 && (support > best || (support == best && width > widest))) {
            cut = x + width / 2;
            best = support;
            widest = width;
        }
    }
    return cut;
}

struct Band {
    double bottom = infinity, top = -infinity;
};

Band Bounds(const Runs& runs) {
    Band band;
    for (const auto* r : runs) {
        band.bottom = std::min(band.bottom, r->bottom);
        band.top = std::max(band.top, r->top);
    }
    return band;
}

class LayoutBuilder {
public:
    int page;
    const std::vector<CharInfo>& chars;
    double gutter;
    ReadingPage result;

    void appendColumn(const Runs& runs, Band band) {
        ReadingColumn col{int(result.lines.size()), 0, infinity, -infinity, band.bottom, band.top};
        for (auto row = runs.begin(); row != runs.end();) {
            const double baseline = (*row)->baseline;
            LineInfo line;
            line.pageIndex = page;
            line.left = line.bottom = infinity;
            line.right = line.top = -infinity;
            do {
                const auto& r = **row++;
                line.left = std::min(line.left, r.left);
                line.right = std::max(line.right, r.right);
                line.bottom = std::min(line.bottom, r.bottom);
                line.top = std::max(line.top, r.top);
                line.charIndices.insert(line.charIndices.end(), r.indices.begin(), r.indices.end());
            } while (row != runs.end() && (*row)->baseline == baseline);
            // Boundary spaces can bridge the entire gutter in page space.
            auto& ids = line.charIndices;
            auto ink = [&](int i) { return !Space(chars[i]); };
            ids.erase(std::find_if(ids.rbegin(), ids.rend(), ink).base(), ids.end());
            ids.erase(ids.begin(), std::find_if(ids.begin(), ids.end(), ink));
            for (int i : ids) line.text += Utf8Encode(ReadingCodepoint(chars[i]));
            col.left = std::min(col.left, line.left);
            col.right = std::max(col.right, line.right);
            result.lines.push_back(std::move(line));
        }
        col.endLine = int(result.lines.size());
        if (col.endLine > col.firstLine) result.columns.push_back(col);
    }

    void append(const Runs& runs, Band band) {
        if (runs.empty()) return;
        const auto cut = FindGutter(runs, gutter);
        if (!cut) { appendColumn(runs, band); return; }

        // A title, abstract or caption spanning the gutter separates the
        // column flows above and below it. Never move it to the end of a column.
        std::vector<std::pair<Runs, bool>> bands;
        for (auto row = runs.begin(); row != runs.end();) {
            const auto end = std::find_if(row + 1, runs.end(), [&](const Run* r) { return r->baseline != (*row)->baseline; });
            const bool spans = std::any_of(row, end, [&](const Run* r) { return r->left < *cut && r->right > *cut; });
            if (bands.empty() || bands.back().second != spans) bands.emplace_back(Runs{}, spans);
            bands.back().first.insert(bands.back().first.end(), row, end);
            row = end;
        }
        Runs unsplit;
        auto flush = [&] {
            if (!unsplit.empty()) appendColumn(unsplit, unsplit.size() == runs.size() ? band : Bounds(unsplit));
            unsplit.clear();
        };
        for (const auto& [part, spans] : bands) {
            Runs left, right;
            if (!spans) for (const auto* r : part) (r->left < *cut ? left : right).push_back(r);
            // A single row of authors, or an isolated large word space, is
            // still one line. Require overlapping multiline flows in this band.
            if (!left.empty() && !right.empty() &&
                std::min(left.front()->baseline, right.front()->baseline) >
                std::max(left.back()->baseline, right.back()->baseline)) {
                flush();
                const Band extent = part.size() == runs.size() ? band : Bounds(part);
                append(left, extent);
                append(right, extent);
            } else {
                unsplit.insert(unsplit.end(), part.begin(), part.end());
            }
        }
        flush();
    }
};

double Distance(double v, double low, double high) {
    return std::max({low - v, 0.0, v - high});
}

} // namespace

ReadingPage BuildReadingPage(int pageIndex, const std::vector<CharInfo>& chars) {
    double gutter = 0;
    const auto fragments = MakeRuns(chars, &gutter);
    Runs runs;
    for (const auto& r : fragments) runs.push_back(&r);
    LayoutBuilder layout{pageIndex, chars, gutter, {}};
    layout.append(runs, Bounds(runs));
    return std::move(layout.result);
}

int ReadingPage::HitLine(double x, double y, int anchorLine) const {
    if (columns.empty()) return -1;
    const ReadingColumn* nearest = &columns.front();
    const ReadingColumn* anchor = nullptr;
    std::pair<double, double> best{infinity, infinity};
    for (const auto& col : columns) {
        const double dx = Distance(x, col.left, col.right), dy = Distance(y, col.bottom, col.top);
        const auto distance = std::make_pair(dx * dx + dy * dy, dy);
        if (distance < best) { best = distance; nearest = &col; }
        if (col.firstLine <= anchorLine && anchorLine < col.endLine) anchor = &col;
    }
    // While dragging through the blank gutter, keep the starting column
    // until the pointer actually reaches the text area of its neighbour.
    if (anchor && nearest != anchor && nearest->top == anchor->top && nearest->bottom == anchor->bottom &&
        ((anchor->right <= nearest->left && x < nearest->left) ||
         (anchor->left >= nearest->right && x > nearest->right))) nearest = anchor;

    int line = nearest->firstLine;
    best = {infinity, infinity};
    for (int i = nearest->firstLine; i < nearest->endLine; ++i) {
        const auto& l = lines[i];
        const auto distance = std::make_pair(Distance(y, l.bottom, l.top), Distance(x, l.left, l.right));
        if (distance < best) { best = distance; line = i; }
    }
    return line;
}

} // namespace pre
