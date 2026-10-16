#include "text.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

namespace pre {
namespace {

// Two glyphs share a line when their ink boxes overlap this much, measured
// against the shorter of the two boxes.
constexpr double kOverlapRatio = 0.50;

// A font size larger than this multiple of the page median is treated as
// unreliable: some producers drive PDFium's reported size far off scale (we
// observed an entire page reporting 209 pt for 11 pt text).
constexpr double kSizeOutlierFactor = 3.0;

std::string ToLowerAscii(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

double Median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    const size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + mid, v.end());
    return v[mid];
}

double InkHeight(const CharInfo& c) {
    const double h = c.top - c.bottom;
    return h > 0.0 ? h : 0.0;
}

}  // namespace

std::string Utf8Encode(uint32_t cp) {
    std::string out;
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
}

std::vector<uint32_t> Utf8Decode(const std::string& s) {
    std::vector<uint32_t> out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const unsigned char b0 = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0;
        size_t   extra = 0;
        if (b0 < 0x80)        { cp = b0;         extra = 0; }
        else if ((b0 & 0xE0) == 0xC0) { cp = b0 & 0x1F; extra = 1; }
        else if ((b0 & 0xF0) == 0xE0) { cp = b0 & 0x0F; extra = 2; }
        else if ((b0 & 0xF8) == 0xF0) { cp = b0 & 0x07; extra = 3; }
        else { ++i; continue; }  // invalid lead byte

        if (i + extra >= s.size()) break;
        bool ok = true;
        for (size_t k = 1; k <= extra; ++k) {
            const unsigned char bk = static_cast<unsigned char>(s[i + k]);
            if ((bk & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (bk & 0x3F);
        }
        i += extra + 1;
        if (ok) out.push_back(cp);
    }
    return out;
}

std::string NormalizeForMatch(const std::string& utf8) {
    std::string out;
    for (uint32_t cp : Utf8Decode(utf8)) {
        // Drop ASCII whitespace.
        if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r') continue;
        // Drop ASCII punctuation (dotted leaders, brackets, separators ...).
        if (cp < 0x80) {
            const bool alnum = (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z') ||
                               (cp >= 'A' && cp <= 'Z');
            if (!alnum) continue;
            if (cp >= 'A' && cp <= 'Z') cp = cp - 'A' + 'a';
            out.push_back(static_cast<char>(cp));
            continue;
        }
        // Drop common CJK / fullwidth punctuation that carries no identity.
        switch (cp) {
            case 0x3000:  // ideographic space
            case 0x3001:  // 、
            case 0x3002:  // 。
            case 0xFF0C:  // ，
            case 0xFF0E:  // ．
            case 0xFF1A:  // ：
            case 0xFF1B:  // ；
            case 0xFF08:  // （
            case 0xFF09:  // ）
            case 0x201C:  // “
            case 0x201D:  // ”
            case 0x2018:  // ‘
            case 0x2019:  // ’
            case 0x00B7:  // ·
                continue;
            default: break;
        }
        out += Utf8Encode(cp);
    }
    return out;
}

size_t CommonPrefixLength(const std::string& a, const std::string& b) {
    const auto va = Utf8Decode(a);
    const auto vb = Utf8Decode(b);
    const size_t n = std::min(va.size(), vb.size());
    size_t i = 0;
    while (i < n && va[i] == vb[i]) ++i;
    return i;
}

const char* PdfiumErrorName(unsigned long err) {    switch (err) {
        case 0: return "SUCCESS";
        case 1: return "UNKNOWN";
        case 2: return "FILE (not found or cannot be opened)";
        case 3: return "FORMAT (not a PDF / damaged beyond repair)";
        case 4: return "PASSWORD (password required or incorrect)";
        case 5: return "SECURITY (unsupported encryption)";
        case 6: return "PAGE (page not found or content error)";
        default: return "?";
    }
}

bool IsBoldFont(int fontWeight, int fontFlags, const std::string& fontName) {
    if (fontWeight >= 600) return true;
    if (fontFlags & kForceBold) return true;

    const std::string n = ToLowerAscii(fontName);
    static const char* kBoldMarkers[] = {"bold", "black", "heavy", "semibold",
                                         "semi-bold", "demibold", "ultra"};
    for (const char* marker : kBoldMarkers) {
        if (n.find(marker) != std::string::npos) return true;
    }
    return false;
}

std::vector<LineInfo> BuildLines(int pageIndex, const std::vector<CharInfo>& chars,
                                 const LineBuildOptions& opts) {
    std::vector<LineInfo> lines;
    if (chars.empty()) return lines;

    // --- page-level typography scale -------------------------------------
    // Clustering must not depend on the reported font size: some producers
    // make PDFium report values an order of magnitude off, which would blow up
    // any size-proportional tolerance. Ink boxes stay sane, so we cluster on
    // those and use the *ink height* as the typography signal.
    std::vector<double> sizes;
    sizes.reserve(chars.size());
    for (const CharInfo& c : chars) {
        if (c.fontSize > 0.0 && !c.IsSpace()) sizes.push_back(c.fontSize);
    }
    const double medianSize = Median(sizes);
    const double sizeCap =
        (medianSize > 0.0) ? medianSize * kSizeOutlierFactor : 0.0;
    auto plausibleSize = [&](double s) {
        return s > 0.0 && (sizeCap <= 0.0 || s <= sizeCap);
    };

    // Reading order for insertion: bottom-up, then left-to-right.
    std::vector<int> order(chars.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&chars](int a, int b) {
        if (chars[a].bottom != chars[b].bottom) return chars[a].bottom < chars[b].bottom;
        return chars[a].left < chars[b].left;
    });

    for (int idx : order) {
        const CharInfo& c = chars[idx];
        const double h = InkHeight(c);

        int    best      = -1;
        double bestScore = 0.0;

        if (h <= 0.0) {
            // Point-like glyph (usually a space). Attach it to the line whose
            // vertical span contains it; never let it open a line of its own,
            // which would flood the page with junk one-glyph lines.
            constexpr double kPointEps = 1.5;
            int    holder     = -1;
            double holderDist = 1e30;
            for (size_t li = 0; li < lines.size(); ++li) {
                const LineInfo& L = lines[li];
                if (c.bottom >= L.bottom - kPointEps && c.bottom <= L.top + kPointEps) {
                    const double dist =
                        std::fabs(c.bottom - 0.5 * (L.bottom + L.top));
                    if (dist < holderDist) {
                        holderDist = dist;
                        holder     = static_cast<int>(li);
                    }
                }
            }
            if (holder < 0) continue;  // drop it rather than invent a line
            best = holder;
        } else {
            for (size_t li = 0; li < lines.size(); ++li) {
                const LineInfo& L = lines[li];
                const double Lh = L.top - L.bottom;
                if (Lh <= 0.0) continue;
                const double overlap =
                    std::min(c.top, L.top) - std::max(c.bottom, L.bottom);
                if (overlap <= 0.0) continue;
                const double score = overlap / std::min(h, Lh);
                if (score > bestScore) {
                    bestScore = score;
                    best      = static_cast<int>(li);
                }
            }

            if (best < 0 || bestScore < kOverlapRatio) {
                LineInfo line;
                line.pageIndex = pageIndex;
                line.left = c.left;     line.right = c.right;
                line.bottom = c.bottom; line.top = c.top;
                if (plausibleSize(c.fontSize)) {
                    line.maxFontSize = c.fontSize;
                    line.avgFontSize = c.fontSize;
                }
                line.maxWeight = c.fontWeight;
                line.charIndices.push_back(idx);
                lines.push_back(std::move(line));
                continue;
            }
        }

        LineInfo& line = lines[best];
        line.left   = std::min(line.left, c.left);
        line.right  = std::max(line.right, c.right);
        line.bottom = std::min(line.bottom, c.bottom);
        line.top    = std::max(line.top, c.top);
        if (plausibleSize(c.fontSize)) {
            line.maxFontSize = std::max(line.maxFontSize, c.fontSize);
        }
        line.maxWeight = std::max(line.maxWeight, c.fontWeight);
        line.charIndices.push_back(idx);
    }

    // --- finalise each line ----------------------------------------------
    for (LineInfo& line : lines) {
        std::sort(line.charIndices.begin(), line.charIndices.end(),
                  [&chars](int a, int b) { return chars[a].left < chars[b].left; });

        std::vector<double> heights;
        std::vector<double> lineSizes;
        int  boldish = 0, visible = 0;
        double prevOriginX = 0.0;
        bool   prevHasOrigin = false;
        bool   first = true;

        for (int idx : line.charIndices) {
            const CharInfo& c = chars[idx];

            if (!c.IsSpace()) {
                ++visible;
                const double h = InkHeight(c);
                if (h > 0.0) heights.push_back(h);
                if (plausibleSize(c.fontSize)) lineSizes.push_back(c.fontSize);
                if (IsBoldFont(c.fontWeight, c.fontFlags, c.fontName)) ++boldish;
            }

            // Space synthesis is driven by the pen advance, not the ink gap:
            // CJK glyph ink boxes are narrower than the em, so ink gaps are
            // large even when characters touch.
            if (!first && opts.synthesizeSpaces && prevHasOrigin && c.hasOrigin) {
                const double em = (c.fontSize > 0.0) ? c.fontSize : line.glyphHeight;
                const double advance = c.originX - prevOriginX;
                if (em > 0.0 && advance > em * (1.0 + opts.spaceGapRatio) &&
                    !line.text.empty() && line.text.back() != ' ') {
                    line.text.push_back(' ');
                }
            }

            // Control characters must never reach the text: PDFium reports the
            // newlines and tabs that separate text-showing operations inside a
            // line, and letting them through would put embedded line breaks in
            // LineInfo::text and break every downstream text consumer.
            uint32_t cp = c.codepoint;
            if (cp < 0x20 || cp == 0x7F) cp = ' ';

            if (cp == ' ') {
                // Collapse runs, and never start a line with a space.
                if (!line.text.empty() && line.text.back() != ' ') {
                    line.text.push_back(' ');
                }
            } else {
                line.text += Utf8Encode(cp);
            }
            prevOriginX   = c.originX;
            prevHasOrigin = c.hasOrigin;
            first         = false;
        }
        // Drop a trailing space introduced by collapsed whitespace.
        while (!line.text.empty() && line.text.back() == ' ') line.text.pop_back();

        // The typography signal. The tallest glyph on the line sets the scale,
        // and glyphs below 30% of it (dotted leaders, stray marks) are excluded
        // before taking the median -- otherwise a printed-TOC line with 150
        // leader dots would report the height of a dot, not of the text.
        double maxH = 0.0;
        for (double hh : heights) maxH = std::max(maxH, hh);
        std::vector<double> kept;
        if (maxH > 0.0) {
            const double floorH = 0.30 * maxH;
            for (double hh : heights) {
                if (hh >= floorH) kept.push_back(hh);
            }
        }
        line.glyphHeight = Median(kept);
        if (!lineSizes.empty()) {
            line.avgFontSize =
                std::accumulate(lineSizes.begin(), lineSizes.end(), 0.0) /
                static_cast<double>(lineSizes.size());
        }
        line.bold = (visible > 0) && (boldish * 2 >= visible);
    }

    // Reading order: top of page first, then left to right.
    std::sort(lines.begin(), lines.end(), [](const LineInfo& a, const LineInfo& b) {
        if (std::fabs(a.top - b.top) > 0.5) return a.top > b.top;
        return a.left < b.left;
    });

    return lines;
}

}  // namespace pre
