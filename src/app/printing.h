// Printing: rasterise PDF pages onto a QPrinter.
//
// Page fidelity matters more than vector output here, so each page is rendered
// by PDFium at the printer's own resolution and blitted 1:1. That keeps the
// printed result identical to what the reader displays, which is the property
// the user actually checks.
#pragma once

#include <QPrinter>

#include "fpdfview.h"

namespace app {

struct PrintOutcome {
    int  pagesPrinted = 0;
    int  firstPage    = 0;   // 0-based, inclusive
    int  lastPage     = 0;
    bool aborted      = false;   // painter could not start
};

// Render pages [firstPage, lastPage] (0-based, inclusive) onto `printer`.
// Pages are scaled to fit the printable area and centred; aspect ratio is
// always preserved. Returns what actually happened rather than a bare bool.
PrintOutcome PrintPages(FPDF_DOCUMENT doc, int pageCount, QPrinter& printer,
                        int firstPage, int lastPage);

// Write pages [firstPage, lastPage] into a new PDF *vectorially*, keeping the
// original text, fonts and vectors intact.
//
// This is strictly better than rasterising when the destination is a file: the
// result stays searchable, is orders of magnitude smaller, and the reader's own
// table-of-contents engine still works on it. Real printers obviously still go
// through PrintPages.
bool SavePagesToPdf(FPDF_DOCUMENT doc, int pageCount, const QString& outPath,
                    int firstPage, int lastPage);

}  // namespace app
