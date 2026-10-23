# Pinned EPUB dependencies

These sources are vendored so building the reader does not fetch JavaScript or
change its layout engine unexpectedly. The UI and books run locally.

| Component | Source snapshot | License |
| --- | --- | --- |
| webview headers + MinGW EventToken compatibility | [webview/webview](https://github.com/webview/webview/tree/f244320ab0d82fdab98a094e02996fc26d0ffd0c), `f244320ab0d82fdab98a094e02996fc26d0ffd0c` | MIT, `webview/LICENSE` |
| Microsoft WebView2 SDK header | NuGet `Microsoft.Web.WebView2` `1.0.1150.38`, `build/native/include/WebView2.h` | `webview2/LICENSE.txt` |
| Foliate JS | [johnfactotum/foliate-js](https://github.com/johnfactotum/foliate-js/tree/78914aef4466eb960965702401634c2cb348e9b1), `78914aef4466eb960965702401634c2cb348e9b1` | MIT, `foliate-js/LICENSE` |
| zip.js browser module bundled by Foliate | Foliate's `vendor/zip.js`; lockfile pins `@zip.js/zip.js` 2.8.22 | BSD-3-Clause, `foliate-js/vendor/zip-LICENSE.txt` |

Local Foliate changes: `paginator.js` and `fixed-layout.js` remove `allow-scripts`
from book iframe sandboxes. The trusted outer document still accesses their DOM
to implement layout and selection; the book cannot execute scripts.
`fixed-layout.js` also normalizes book-level viewport strings and SVG viewBox
separators, rejects invalid dimensions, and uses safe centering so an enlarged
page's left edge remains reachable. `paginator.js` and `view.js` propagate chapter
load errors to the host instead of leaving a blank reader; the paginator also
releases its navigation lock on failure and stops safely at book boundaries.
`fixed-layout.js` also exposes section indices and overlays for text selections
and comments, scales its annotation layer with the page, and scrolls CFI ranges
into view at manual zoom levels.
`epub.js` resolves spine-only CFIs to the section start instead of attempting to
construct a DOM range from an empty path.
All other vendored sources remain as shipped upstream. `src/app/epub/reader.js` is our
adapter and sanitizes book HTML before it reaches a renderer.

The WebView2 browser runtime is a separately serviced Microsoft component. The
SDK header is compiled into the application; no Qt WebEngine or Node runtime is
distributed. Upgrading Foliate requires running the EPUB integration checks,
including CFI restoration, iframe policy and selection coordinates.
