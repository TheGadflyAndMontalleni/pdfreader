// PDFium document lifetime + page character extraction.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "fpdfview.h"
#include "text.h"

namespace pre {

// RAII wrapper for the PDFium library (must outlive every Document).
class Library {
public:
    Library();
    ~Library();
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
};

// Owns an FPDF_DOCUMENT.
//
// Opens a UTF-8 path through std::filesystem and keeps the loaded bytes alive
// for PDFium. The narrow PDFium path loader is a fallback for readable paths.
class Document {
public:
    Document() = default;
    ~Document();
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    bool Open(const std::string& path, std::string* errorOut = nullptr);
    void Close();

    bool valid() const { return doc_ != nullptr; }
    int  PageCount() const;
    FPDF_DOCUMENT handle() const { return doc_; }
    const std::string& path() const { return path_; }

    bool usedMemFallback() const { return usedMemFallback_; }
    unsigned long lastError() const { return lastError_; }

private:
    FPDF_DOCUMENT        doc_ = nullptr;
    std::vector<uint8_t> bytes_;   // must outlive doc_ when loaded from memory
    std::string          path_;
    bool                 usedMemFallback_ = false;
    unsigned long        lastError_ = 0;
};

// Every character on one page, with box, font size, weight and name.
std::vector<CharInfo> ExtractPageChars(FPDF_DOCUMENT doc, int pageIndex);

}  // namespace pre
