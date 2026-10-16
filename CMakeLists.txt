cmake_minimum_required(VERSION 3.20)
project(pdfreader CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Bundled toolchain locations (nothing installed system-wide).
set(QT_PREFIX  "${CMAKE_SOURCE_DIR}/tools"        CACHE PATH "Qt prefix")
set(PDFIUM_DIR "${CMAKE_SOURCE_DIR}/tools/pdfium" CACHE PATH "PDFium prefix")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets PrintSupport
             PATHS "${QT_PREFIX}/lib/cmake" NO_DEFAULT_PATH)

# PDFium ships an MSVC import library; tools/make_pdfium_implib.ps1 converts it
# into a MinGW-compatible one.
set(PDFIUM_IMPLIB "${PDFIUM_DIR}/lib/libpdfium.a")
if(NOT EXISTS "${PDFIUM_IMPLIB}")
    message(FATAL_ERROR "Missing ${PDFIUM_IMPLIB}. Run tools/make_pdfium_implib.ps1 first.")
endif()

add_library(pdfium INTERFACE)
target_include_directories(pdfium INTERFACE "${PDFIUM_DIR}/include")
target_link_libraries(pdfium INTERFACE "${PDFIUM_IMPLIB}")

# ---- core extraction layer (no Qt dependency) ----
add_library(pdfcore STATIC
    src/doc.cpp
    src/text.cpp
    src/render.cpp
    src/find.cpp
    src/annot.cpp
    src/toc.cpp
    src/toc_typography.cpp
    src/printed_toc.cpp
)
target_include_directories(pdfcore PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(pdfcore PUBLIC pdfium)

# ---- P0 probe: toolchain + raw PDFium primitives ----
add_executable(p0_probe tests/p0_pdfium_probe.cpp)
target_link_libraries(p0_probe PRIVATE pdfium Qt6::Core)

# ---- P1 probe: line reconstruction + typography report ----
add_executable(p1_lines tests/p1_structure_probe.cpp)
target_link_libraries(p1_lines PRIVATE pdfcore)

# ---- TOC engine: reconstruct a navigable table of contents ----
add_executable(toc_build tests/toc_build.cpp)
target_link_libraries(toc_build PRIVATE pdfcore)

# ---- the reader itself ----
add_executable(pdfreader WIN32
    src/app/main.cpp
    src/app/mainwindow.cpp
    src/app/pdfview.cpp
    src/app/printing.cpp
    src/app/session.cpp
    src/app/thumbnails.cpp
)
target_link_libraries(pdfreader PRIVATE pdfcore Qt6::Widgets Qt6::PrintSupport)
