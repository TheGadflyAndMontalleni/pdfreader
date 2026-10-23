# Third-party software

PDF Reader is distributed with dynamically linked third-party libraries. Their
license texts are included in the installed `licenses/` directory. The portable
edition extracts the same notices beside its runtime files on first launch.

| Component | Version | License and source |
| --- | --- | --- |
| Qt Core, Gui, Widgets, PrintSupport, Network and plugins | 6.8.3 | LGPL-3.0; [corresponding Qt source archive](https://download.qt.io/archive/qt/6.8/6.8.3/single/qt-everywhere-src-6.8.3.zip) |
| PDFium and its bundled dependencies | chromium/8100 | BSD-style and other licenses; see `licenses/pdfium/`; [source](https://pdfium.googlesource.com/pdfium/+/refs/heads/chromium/8100/) and [binary distribution](https://github.com/bblanchon/pdfium-binaries/tree/chromium/8100) |
| GCC runtime: libstdc++ and libgcc | 13.1.0 | GPL with the GCC Runtime Library Exception; [source](https://gcc.gnu.org/git/?p=gcc.git;a=tree;hb=releases/gcc-13.1.0) |
| MinGW-w64 / winpthreads runtime | Toolchain mingw1310_64 | See `licenses/mingw/`; [source](https://www.mingw-w64.org/) |
| webview | f244320ab0d82fdab98a094e02996fc26d0ffd0c | MIT; [source](https://github.com/webview/webview), `licenses/webview-LICENSE.txt` |
| Foliate JS | 78914aef4466eb960965702401634c2cb348e9b1 | MIT; [source](https://github.com/johnfactotum/foliate-js), `licenses/foliate-js-LICENSE.txt`; book script execution disabled locally |
| zip.js | 2.8.22 (Foliate bundled module) | BSD-3-Clause; [source](https://github.com/gildas-lormeau/zip.js/tree/v2.8.22), `licenses/zip-LICENSE.txt` |
| Microsoft WebView2 SDK header | 1.0.1150.38 | BSD-3-Clause; `licenses/WebView2-SDK-LICENSE.txt`; [source package](https://www.nuget.org/packages/Microsoft.Web.WebView2/1.0.1150.38) |

The Qt DLLs and plugins are ordinary, replaceable files. Users may replace them
with ABI-compatible builds and may reverse engineer the application as necessary
to debug modifications to the LGPL-covered libraries. No additional restriction
is imposed on these rights. Qt is a trademark of The Qt Company Ltd.

The Windows installer is generated with [Inno Setup](https://jrsoftware.org/isinfo.php).
Its Simplified Chinese translation is taken from the Inno Setup 6.7.3 source tree;
the translation's original attribution is retained in `tools/installer/ChineseSimplified.isl`.

The separately distributed translation plugin embeds an indexed snapshot of
[ECDICT](https://github.com/skywind3000/ECDICT) at commit
`bc015ed2e24a7abef49fc6dbbb7fe32c1dadaf8b`. Its MIT license is included in
`licenses/ECDICT-LICENSE.txt`; the plugin archive records the source and data
checksums in `licenses/ecdict-source.json`. Runtime dictionary lookups are local.
