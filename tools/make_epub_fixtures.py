"""Small original EPUB fixtures: no network, copyrighted books or user data."""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED, ZIP_STORED
import argparse

def make(path, version="3.0", fixed=False, utf16=False, book_viewport=False, xml_asset=False):
    path.parent.mkdir(parents=True, exist_ok=True)
    nav = '''<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><head><title>目录</title></head><body><nav epub:type="toc"><ol><li><a href="chapter1.xhtml">A quieter way to read</a><ol><li><a href="chapter1.xhtml#details">正文锚点</a></li></ol></li><li><a href="chapter2.xhtml">第二章 · 中文阅读</a></li></ol></nav></body></html>'''
    ncx = '''<?xml version="1.0"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><head/><docTitle><text>EPUB Preview</text></docTitle><navMap><navPoint id="one" playOrder="1"><navLabel><text>First chapter</text></navLabel><content src="chapter1.xhtml"/></navPoint><navPoint id="two" playOrder="2"><navLabel><text>第二章</text></navLabel><content src="chapter2.xhtml"/></navPoint></navMap></ncx>'''
    text = "Reading is an invitation to slow down. A small moment of serendipity can change the meaning of a familiar page. Select a word to look it up, or select a sentence to understand its context."
    paragraphs = ''.join(f'<p id="p{i}">{i + 1}. {text}</p>' for i in range(100))
    first = f'''<?xml version="1.0" encoding="UTF-8"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>A quieter way to read</title><meta name="viewport" content="width=800,height=1100"/><link rel="stylesheet" href="style.css"/></head><body><p class="eyebrow">EPUB PREVIEW · 01</p><h1>A quieter way<br/>to read.</h1><p class="intro">Your books, at your own pace.</p><p id="word">A little serendipity goes a long way.</p><img src="cover.svg" alt="Mountains at dusk"/><h2 id="details">Text that moves with you</h2>{paragraphs}<script>window.__epubPwned=true;fetch('https://example.com/should-never-be-requested')</script><img src="https://example.com/tracker.png" onerror="window.__epubPwned=true"/><a href="chapter2.xhtml">阅读下一章</a></body></html>'''
    second = '''<?xml version="1.0" encoding="UTF-8"?><html xmlns="http://www.w3.org/1999/xhtml" lang="zh-CN"><head><title>第二章</title><link rel="stylesheet" href="style.css"/></head><body><p class="eyebrow">EPUB PREVIEW · 02</p><h1>把阅读的节奏，<br/>交还给你。</h1><p>这是一段中文测试正文。EPUB 的文字会随着窗口宽度和字号重新排版，目录和书签使用正文锚点，因此无需依赖固定页码。</p><p>English and 中文 can live on the same page. Select serendipity to try the dictionary.</p><a href="chapter1.xhtml#details">返回第一章的正文锚点</a></body></html>'''
    metadata = '<meta property="rendition:layout">pre-paginated</meta>' if fixed else ''
    if book_viewport:
        metadata += '<meta property="rendition:viewport">width=800, height=1100</meta><meta property="rendition:spread">none</meta>'
        first = first.replace('<meta name="viewport" content="width=800,height=1100"/>', '')
    opf = f'''<?xml version="1.0" encoding="UTF-8"?><package xmlns="http://www.idpf.org/2007/opf" version="{version}" unique-identifier="book-id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="book-id">pdfreader-test-{version}-{fixed}</dc:identifier><dc:title>EPUB Preview · 阅读试用</dc:title><dc:language>en</dc:language><dc:creator>PDF Reader</dc:creator>{metadata}</metadata><manifest><item id="one" href="chapter1.xhtml" media-type="application/xhtml+xml"/><item id="two" href="chapter2.xhtml" media-type="application/xhtml+xml"/><item id="style" href="style.css" media-type="text/css"/><item id="image" href="cover.svg" media-type="image/svg+xml"/><item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/><item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/></manifest><spine toc="ncx"><itemref idref="one"/><itemref idref="two"/></spine></package>'''
    if version == "2.0":
        opf = opf.replace('<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>', '')
    extra_resources = {}
    if xml_asset:
        # Older EPUBs link Adobe XML page templates. The resource loader hands
        # non-core XML types to the transform hook as Blobs rather than strings.
        opf = opf.replace('</manifest>', '<item id="template" href="layout.xpgt" media-type="application/vnd.adobe.page-template+xml"/></manifest>')
        first = first.replace('</head>', '<link rel="stylesheet" href="layout.xpgt" type="application/vnd.adobe.page-template+xml"/></head>')
        template = '<?xml version="1.0" encoding="UTF-8"?><ade:template xmlns:ade="http://ns.adobe.com/2006/ade"><ade:description>Original test layout · 排版</ade:description></ade:template>'
        if xml_asset == "broken":
            template = template.replace('</ade:template>', '')
        elif utf16:
            template = template.replace('encoding="UTF-8"', 'encoding="UTF-16"').encode('utf-16')
        extra_resources['layout.xpgt'] = template
    with ZipFile(path, "w", ZIP_DEFLATED) as z:
        z.writestr("mimetype", "application/epub+zip", compress_type=ZIP_STORED)
        z.writestr("META-INF/container.xml", '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OEBPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        for name, content in {"book.opf": opf, "nav.xhtml": nav, "toc.ncx": ncx, "chapter1.xhtml": first, "chapter2.xhtml": second,
            "style.css": 'body{max-width:42em;margin:auto}h1{font-size:2.7em;line-height:1.2;font-weight:normal;letter-spacing:-.035em}h2{font-weight:normal}.eyebrow{font-family:sans-serif;font-size:.68em;letter-spacing:.18em;color:#6b8782}.intro{font-style:italic;color:#6b8782}img{border-radius:12px;width:100%;max-height:220px}',
            "cover.svg": '<svg xmlns="http://www.w3.org/2000/svg" width="800" height="220" viewBox="0 0 800 220"><rect width="800" height="220" fill="#dce5de"/><circle cx="610" cy="70" r="35" fill="#f5edce"/><path d="M0 220L180 70 360 220M240 220L510 40 800 220" fill="#93aba2"/><path d="M0 220L320 130 500 220M390 220L680 110 800 180V220" fill="#68877e"/></svg>'}.items():
            if utf16 and name == "chapter2.xhtml":
                content = content.replace('encoding="UTF-8"', 'encoding="UTF-16"').encode('utf-16')
            z.writestr("OEBPS/" + name, content)
        for name, content in extra_resources.items():
            z.writestr("OEBPS/" + name, content)
        # An unused resource proves opening does not fetch the whole archive.
        import random
        z.writestr("OEBPS/unused.bin", random.Random(17).randbytes(2 * 1024 * 1024))

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    make(args.directory / "阅读试用.epub")
    make(args.directory / "epub2.epub", "2.0")
    make(args.directory / "fixed.epub", fixed=True)
    make(args.directory / "fixed-book-viewport.epub", fixed=True, book_viewport=True)
    make(args.directory / "utf16.epub", utf16=True)
    make(args.directory / "xml-asset.epub", "2.0", xml_asset=True)
    make(args.directory / "xml-asset-utf16.epub", "2.0", xml_asset=True, utf16=True)
    make(args.directory / "broken-xml-asset.epub", "2.0", xml_asset="broken")
    (args.directory / "broken.epub").write_bytes(b"PK\x03\x04broken archive")
