#include "doc.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <limits>

#include "fpdf_text.h"

namespace pre {

Library::Library() {
    FPDF_LIBRARY_CONFIG cfg;
    std::memset(&cfg, 0, sizeof(cfg));
    cfg.version = 2;
    FPDF_InitLibraryWithConfig(&cfg);
}

Library::~Library() { FPDF_DestroyLibrary(); }

Document::~Document() { Close(); }

void Document::Close() {
    if (doc_) {
        FPDF_CloseDocument(doc_);
        doc_ = nullptr;
    }
    bytes_.clear();
    bytes_.shrink_to_fit();
}

bool Document::Open(const std::string& path, std::string* errorOut) {
    Close();
    path_ = path;

    // Read the file ourselves and hand PDFium a memory buffer first. The
    // path-based loader takes a narrow string and mishandles non-ASCII paths on
    // Windows, which matters here because the target documents routinely have
    // Chinese filenames.
    std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
    if (in) {
        in.seekg(0, std::ios::end);
        const std::streamoff size = in.tellg();
        in.seekg(0, std::ios::beg);
        if (size > 0 && size <= std::numeric_limits<int>::max()) {
            bytes_.resize(static_cast<size_t>(size));
            if (in.read(reinterpret_cast<char*>(bytes_.data()), size)) {
                doc_ = FPDF_LoadMemDocument(bytes_.data(),
                                            static_cast<int>(bytes_.size()), nullptr);
                if (doc_) {
                    usedMemFallback_ = true;
                    return true;
                }
                lastError_ = FPDF_GetLastError();
            }
            bytes_.clear();
        }
    }

    // Fall back to the path loader (ASCII paths, or files we could not read).
    doc_ = FPDF_LoadDocument(path.c_str(), nullptr);
    if (doc_) return true;

    lastError_ = FPDF_GetLastError();
    if (errorOut) {
        *errorOut = std::string("PDFium rejected the document: ") +
                    PdfiumErrorName(lastError_);
    }
    return false;
}

int Document::PageCount() const {
    return doc_ ? FPDF_GetPageCount(doc_) : 0;
}

std::vector<CharInfo> ExtractPageChars(FPDF_DOCUMENT doc, int pageIndex) {
    std::vector<CharInfo> out;
    if (!doc) return out;

    FPDF_PAGE page = FPDF_LoadPage(doc, pageIndex);
    if (!page) return out;

    FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
    if (!textPage) {
        FPDF_ClosePage(page);
        return out;
    }

    const int count = FPDFText_CountChars(textPage);
    out.reserve(count > 0 ? static_cast<size_t>(count) : 0);

    for (int i = 0; i < count; ++i) {
        CharInfo c;
        c.index = i;

        if (!FPDFText_GetCharBox(textPage, i, &c.left, &c.right, &c.bottom, &c.top)) {
            continue;  // no geometry -> unusable for layout analysis
        }

        c.codepoint  = FPDFText_GetUnicode(textPage, i);
        c.fontSize   = FPDFText_GetFontSize(textPage, i);
        c.fontWeight = FPDFText_GetFontWeight(textPage, i);
        c.generated  = FPDFText_IsGenerated(textPage, i) != 0;
        c.hyphen     = FPDFText_IsHyphen(textPage, i) != 0;
        c.angle      = FPDFText_GetCharAngle(textPage, i);
        c.hasOrigin  = FPDFText_GetCharOrigin(textPage, i, &c.originX, &c.originY) != 0;

        char fontName[256] = {0};
        int  flags         = 0;
        if (FPDFText_GetFontInfo(textPage, i, fontName, sizeof(fontName), &flags) > 0) {
            c.fontName = fontName;
            c.fontFlags = flags;
        }

        out.push_back(std::move(c));
    }

    FPDFText_ClosePage(textPage);
    FPDF_ClosePage(page);
    return out;
}

}  // namespace pre
