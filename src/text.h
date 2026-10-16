// Text extraction model for the auto-TOC engine.
//
// PDFium gives us one character at a time with a bounding box, a font size, a
// font weight and a font name. Everything the TOC engine reasons about
// (headings, body-text size, printed TOC pages) is derived from those
// primitives, so this is the foundation layer.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pre {

// PDF spec 1.7 table 5.20 font descriptor flags (1-based bit numbers).
enum FontFlag : int {
    kFixedPitch = 1 << 0,   // bit 1
    kSerif      = 1 << 1,   // bit 2
    kSymbolic   = 1 << 2,   // bit 3
    kScript     = 1 << 3,   // bit 4
    kNonsymbolic= 1 << 5,   // bit 6
    kItalic     = 1 << 6,   // bit 7
    kAllCap     = 1 << 16,  // bit 17
    kSmallCap   = 1 << 17,  // bit 18
    kForceBold  = 1 << 18,  // bit 19
};

// One extracted character, in PDF page space (origin bottom-left, y up).
struct CharInfo {
    int         index      = 0;
    double      left       = 0.0;
    double      right      = 0.0;
    double      bottom     = 0.0;
    double      top        = 0.0;
    double      originX    = 0.0;    // glyph origin (pen position), if known
    double      originY    = 0.0;
    bool        hasOrigin  = false;
    double      fontSize   = 0.0;
    int         fontWeight = 0;      // 100..900 typical; 0 when unknown
    int         fontFlags  = 0;
    std::string fontName;            // UTF-8, as reported by PDFium
    uint32_t    codepoint  = 0;      // Unicode scalar value
    bool        generated  = false;  // synthesized (no backing text object)
    bool        hyphen     = false;  // soft hyphen inserted at a line break
    float       angle      = 0.0f;   // rotation in radians; 0 == horizontal

    double Width()  const { return right - left; }
    double Height() const { return top - bottom; }
    bool   IsSpace() const { return codepoint == ' ' || codepoint == '\t' ||
                                    codepoint == 0x00A0; }
};

// A reconstructed text line: characters sharing a baseline.
struct LineInfo {
    int         pageIndex   = 0;
    double      left = 0.0, right = 0.0, bottom = 0.0, top = 0.0;
    // Median ink height of the non-space glyphs on this line. This is the
    // primary typography signal: it is derived from geometry, so it stays
    // correct even when the producer makes PDFium report a bogus font size.
    double      glyphHeight = 0.0;
    // Reported font size, sanitised against page-level outliers. Advisory.
    double      maxFontSize = 0.0;
    double      avgFontSize = 0.0;
    int         maxWeight   = 0;
    bool        bold        = false;
    std::string text;                   // UTF-8
    std::vector<int> charIndices;       // indices into the page's CharInfo vector

    double Width()  const { return right - left; }
    double Height() const { return top - bottom; }
};

// Encode a Unicode scalar value as UTF-8.
std::string Utf8Encode(uint32_t cp);

// Decode UTF-8 into scalar values. Invalid bytes are skipped.
std::vector<uint32_t> Utf8Decode(const std::string& s);

// Fold a string for title comparison: drops whitespace and punctuation
// (including the dotted leaders used by printed tables of contents), lowercases
// ASCII, and leaves CJK intact. Two titles that differ only in spacing or
// punctuation normalise to the same string.
std::string NormalizeForMatch(const std::string& utf8);

// Longest common prefix length, measured in decoded scalars.
size_t CommonPrefixLength(const std::string& a, const std::string& b);

// Human-readable name for an FPDF_GetLastError() value.
const char* PdfiumErrorName(unsigned long err);

// True when the font/weight combination should be treated as bold.
bool IsBoldFont(int fontWeight, int fontFlags, const std::string& fontName);

// Controls how characters are turned into line text.
struct LineBuildOptions {
    // Synthesize a space where the ink gap is wide. Off by default: PDFium
    // already reports real space characters, and the gap rule misfires on CJK
    // (glyph ink boxes are narrower than the em, so neighbours look spaced).
    bool   synthesizeSpaces = false;
    double spaceGapRatio    = 0.50;  // extra advance beyond one em that implies a gap
};

// Cluster characters into lines using baseline proximity. Characters whose
// baseline lies within a tolerance proportional to the font size join the same
// line; the result is ordered top-to-bottom, then left-to-right.
std::vector<LineInfo> BuildLines(int pageIndex, const std::vector<CharInfo>& chars,
                                 const LineBuildOptions& opts = LineBuildOptions{});

}  // namespace pre
