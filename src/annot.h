// Text markup: highlight / underline / strikeout over a text selection.
//
// This legacy markup path draws directly into the page content. These marks
// are one-way after saving, rather than independently editable annotations.
// Anchored margin comments use PDFium's native highlight annotations and
// QuadPoints instead; see app/comments.h.
#pragma once

#include <string>
#include <vector>

#include "fpdfview.h"

namespace pre {

enum class MarkupKind {
    Highlight,
    Underline,
    StrikeOut,
};

// A rectangle in PDF page space (origin bottom-left, y up).
struct MarkupRect {
    double left = 0.0;
    double bottom = 0.0;
    double right = 0.0;
    double top = 0.0;
};

// Bake `kind` over each rectangle. One rectangle per selected line is the
// normal case. Returns the number of marks drawn, or -1 on failure.
//
// `rgb` is 0xRRGGBB. Highlight uses a translucent fill; underline and
// strikeout draw an opaque bar at the baseline or the vertical middle.
int AddMarkup(FPDF_DOCUMENT doc, int pageIndex, MarkupKind kind,
              const std::vector<MarkupRect>& rects, unsigned int rgb);

// Remove the most recently added mark. Returns the page it was on, or -1 when
// there is nothing to undo. Only marks added in this session are tracked;
// anything already in the file is untouched.
int UndoLastMarkup(FPDF_DOCUMENT doc);

// How many marks this session has added (to any page).
int MarkupCount();

// Forget all undo state, e.g. after saving or loading a different document.
void ResetMarkupState();

// Write `doc` to `path`. The in-memory document is what carries the marks, so
// this is how they reach disk.
bool SaveDocumentToFile(FPDF_DOCUMENT doc, const std::string& path);

}  // namespace pre
