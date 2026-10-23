import './foliate/view.js'
import { EPUB } from './foliate/epub.js'
import { BlobReader, ZipReader, BlobWriter, configure } from './foliate/vendor/zip.js'

const send = (type, values = {}) => window.readerEvent({ type, ...values }).catch(() => {})
const message = document.querySelector('#message')
const view = document.createElement('foliate-view')
document.body.append(view)
let book, options, matches = [], matchIndex = -1, searchGeneration = 0, selectedDocument
const fail = error => {
    message.hidden = false
    message.textContent = `EPUB 打开失败\n${error.message ?? error}`
    send('error', { message: String(error.message ?? error) })
}
window.addEventListener('error', e => {
    // Chromium defers a resize notification to the next frame during reflow.
    if (e.message?.startsWith('ResizeObserver loop')) { e.preventDefault(); return }
    fail(e.error ?? e.message)
})
window.addEventListener('unhandledrejection', e => { e.preventDefault(); fail(e.reason) })

// BlobReader only needs size/slice/arrayBuffer. A range-backed Blob avoids
// downloading or decompressing the whole archive before displaying chapter 1.
class BookBlob {
    constructor(url, size, start = 0) { Object.assign(this, { url, size, start }) }
    slice(start = 0, end = this.size) { return new BookBlob(this.url, Math.max(0, end - start), this.start + start) }
    async arrayBuffer() {
        if (!this.size) return new ArrayBuffer(0)
        const res = await fetch(this.url, { headers: { Range: `bytes=${this.start}-${this.start + this.size - 1}` } })
        if (res.status !== 206) throw Error('无法按需读取本机书籍')
        const data = await res.arrayBuffer()
        if (data.byteLength !== this.size) throw Error('书籍内容不完整，请重新打开')
        return data
    }
}
const readText = async blob => {
    const data = new Uint8Array(await blob.arrayBuffer())
    const prefix = new TextDecoder('windows-1252').decode(data.subarray(0, 200))
    const encoding = data[0] === 0xff && data[1] === 0xfe || data[0] === 0x3c && data[1] === 0 ? 'utf-16le'
        : data[0] === 0xfe && data[1] === 0xff || data[0] === 0 && data[1] === 0x3c ? 'utf-16be'
        : prefix.match(/^<\?xml[^>]*encoding=["']([^"']+)["']/i)?.[1] ?? 'utf-8'
    return new TextDecoder(encoding).decode(data)
}
const sanitize = (text, type) => {
    if (!/html|xml|svg/.test(type)) return text
    const doc = new DOMParser().parseFromString(text, type === 'text/html' ? 'text/html' : 'application/xml')
    if (doc.querySelector('parsererror')) throw Error('书内 XML 资源格式不正确')
    for (const el of doc.querySelectorAll('script,iframe,frame,object,embed,base,form,meta[http-equiv]')) el.remove()
    for (const el of doc.querySelectorAll('*')) for (const attr of [...el.attributes]) {
        if (/^on/i.test(attr.name) || attr.name === 'srcdoc' || /^(?:javascript|vbscript):/i.test(attr.value.trim())) el.removeAttribute(attr.name)
    }
    return new XMLSerializer().serializeToString(doc)
}
const loadBook = async () => {
    const url = new URL('book.epub', location.href)
    const head = await fetch(url, { method: 'HEAD' })
    const size = Number(head.headers.get('Content-Length'))
    if (!head.ok || !Number.isSafeInteger(size) || size < 22) throw Error('无法读取 EPUB 文件')
    configure({ useWebWorkers: false, useCompressionStream: true })
    const zip = new ZipReader(new BlobReader(new BookBlob(url, size)))
    const entries = await zip.getEntries()
    if (entries.length > 20000) throw Error('书内资源过多，超出试用版限制')
    let total = 0
    const resources = new Map()
    for (const entry of entries) {
        if (entry.directory) continue
        if (entry.encrypted) throw Error('暂不支持加密或受 DRM 保护的 EPUB')
        if (entry.uncompressedSize > 64 * 1024 * 1024) throw Error('书内单个资源超过 64 MB')
        total += entry.uncompressedSize
        if (total > 512 * 1024 * 1024) throw Error('解压后的书籍超过 512 MB')
        if (resources.has(entry.filename)) throw Error('EPUB 包含重复的资源路径')
        resources.set(entry.filename, entry)
    }
    const loadText = async name => {
        const entry = resources.get(name)
        if (!entry) return null
        const blob = await entry.getData(new BlobWriter(), { checkSignature: true })
        const text = await readText(blob)
        return /\.(?:x?html?|xml|opf|ncx|svg)$/i.test(name) ? sanitize(text, 'application/xml') : text
    }
    const loadBlob = (name, type) => resources.get(name)?.getData(new BlobWriter(type), { checkSignature: true }) ?? null
    const result = await new EPUB({ loadText, loadBlob, getSize: name => resources.get(name)?.uncompressedSize ?? 0 }).init()
    // Foliate transforms resource documents before producing blob URLs. Apply
    // the same sanitizer to manifest entries with unconventional extensions.
    // Untransformed resources (e.g. Adobe XPGT) arrive as Blobs, not strings.
    result.transformTarget.addEventListener('data', event => {
        const detail = event.detail
        if (/html|xml|svg/.test(detail.type)) detail.data = Promise.resolve(detail.data)
            .then(async data => sanitize(typeof data === 'string' ? data : await readText(data), detail.type))
    })
    return result
}
const selection = () => {
    let chosen, range, text = ''
    for (const { doc } of view.renderer?.getContents() ?? []) {
        const sel = doc.getSelection()
        if (sel?.rangeCount && !sel.isCollapsed && sel.toString().trim()) { chosen = doc; range = sel.getRangeAt(0); text = sel.toString(); break }
    }
    let rect = [0, 0, 0, 0]
    if (range) {
        selectedDocument = chosen
        const frame = chosen.defaultView.frameElement
        const bounds = frame.getBoundingClientRect(), inner = range.getBoundingClientRect()
        const sx = bounds.width / frame.clientWidth, sy = bounds.height / frame.clientHeight
        const left = Math.max(0, bounds.left + inner.left * sx), top = Math.max(0, bounds.top + inner.top * sy)
        const right = Math.min(innerWidth, bounds.left + inner.right * sx), bottom = Math.min(innerHeight, bounds.top + inner.bottom * sy)
        if (right > left && bottom > top) rect = [left, top, right - left, bottom - top]
    }
    send('selection', { text: text.slice(0, 100000), rect, width: innerWidth, height: innerHeight })
}
let selectionTimer
const queueSelection = () => { clearTimeout(selectionTimer); selectionTimer = setTimeout(selection, 35) }
function reportZoom() {
    if (!view.isFixedLayout) return
    for (const { doc } of view.renderer?.getContents() ?? []) {
        const frame = doc.defaultView.frameElement
        const width = frame.getBoundingClientRect().width
        if (width > 0 && frame.clientWidth > 0) {
            send('zoom', { scale: width / frame.clientWidth })
            break
        }
    }
}
const viewportObserver = new ResizeObserver(() => {
    requestAnimationFrame(reportZoom); queueSelection()
})
viewportObserver.observe(view)
const configureLayout = () => {
    if (view.isFixedLayout) {
        view.renderer.setAttribute('zoom', options.fitMode === 1 ? 'fit-width'
            : options.fitMode === 2 ? 'fit-page' : String(options.scale))
    } else {
        view.renderer.setAttribute('flow', options.scrolled ? 'scrolled' : 'paginated')
        view.renderer.setStyles(styles())
    }
}
const shortcuts = event => {
    const { key, ctrlKey: ctrl, altKey: alt, shiftKey: shift } = event
    let command
    if (ctrl && key.toLowerCase() === 'f') command = 'find'
    else if (ctrl && key.toLowerCase() === 'o') command = 'open'
    else if (ctrl && key.toLowerCase() === 'd') command = 'bookmark'
    else if (ctrl && key.toLowerCase() === 'g') command = 'goTo'
    else if (ctrl && ['+', '='].includes(key)) command = 'larger'
    else if (ctrl && key === '-') command = 'smaller'
    else if (ctrl && key === '0') command = 'fitWidth'
    else if (ctrl && key === '1') command = 'fitPage'
    else if (alt && key.toLowerCase() === 't') command = 'translate'
    else if (ctrl && key.toLowerCase() === 'w') command = 'close'
    else if (ctrl && key === 'Tab') command = shift ? 'previousTab' : 'nextTab'
    else if (key === 'F3') command = shift ? 'findPrevious' : 'findNext'
    else if (!ctrl && !alt && key.toLowerCase() === 'i') command = 'dark'
    else if (key === 'Escape') reader.clearSelection()
    else if (!ctrl && !alt && (key === 'ArrowRight' || key === 'PageDown' || (key === ' ' && !shift))) reader.next()
    else if (!ctrl && !alt && (key === 'ArrowLeft' || key === 'PageUp' || (key === ' ' && shift))) reader.previous()
    if (command) { event.preventDefault(); send('command', { command }) }
}
window.addEventListener('keydown', shortcuts)
view.addEventListener('load', ({ detail: { doc, index } }) => {
    applyDocumentStyle(doc)
    doc.addEventListener('selectionchange', queueSelection)
    doc.addEventListener('pointerup', queueSelection)
    doc.addEventListener('keydown', shortcuts)
    send('text', { index, text: doc.body?.textContent.slice(0, 500000) ?? '' })
    requestAnimationFrame(reportZoom)
})
view.addEventListener('relocate', ({ detail }) => {
    send('location', { cfi: detail.cfi, section: detail.section?.current ?? 0, fraction: detail.fraction ?? 0 })
    queueSelection()
    requestAnimationFrame(reportZoom)
})
view.addEventListener('external-link', e => { e.preventDefault() })
view.addEventListener('link', e => { e.preventDefault(); reader.goTo(e.detail.href) })
window.addEventListener('resize', queueSelection)
const styles = () => `
    :root{color-scheme:${options.dark ? 'dark' : 'light'};font-size:${18 * options.scale}px!important}
    html,body{background:${options.dark ? '#20262b' : '#fbf8f1'}!important;color:${options.dark ? '#dce0df' : '#30383b'}!important}
    body{line-height:1.85!important;font-family:Georgia,'Microsoft YaHei','Noto Serif CJK SC',serif}
    p{orphans:2;widows:2}img,svg{max-width:100%;height:auto}a{color:${options.dark ? '#9fc6cf' : '#356e7d'}}
    ::selection{background:#9bbfc88c}
`
const applyDocumentStyle = doc => {
    if (!options || view.isFixedLayout) return
    doc.documentElement.style.setProperty('font-size', `${18 * options.scale}px`, 'important')
    for (const element of [doc.documentElement, doc.body]) {
        element.style.setProperty('background-color', options.dark ? '#20262b' : '#fbf8f1', 'important')
        element.style.setProperty('color', options.dark ? '#dce0df' : '#30383b', 'important')
    }
}
let tocCount = 0
const cleanToc = (items, depth = 0) => depth > 32 ? [] : (items ?? []).slice(0, 3000).flatMap(item =>
    ++tocCount > 3000 ? [] : [{ label: String(item.label ?? '').slice(0, 200), href: String(item.href ?? '').slice(0, 1024), subitems: cleanToc(item.subitems, depth + 1) }])
const reader = window.reader = {
    async open(config) {
        try {
            if (!Object.groupBy || !Map.groupBy || !globalThis.DecompressionStream)
                throw Error('WebView2 运行组件过旧，请更新 Microsoft Edge WebView2 后重试。')
            options = config
            book = await loadBook()
            if (!book.sections.length) throw Error('这本 EPUB 没有可阅读的章节')
            await view.open(book)
            options.scale = view.isFixedLayout ? Math.max(.1, Math.min(8, options.scale))
                : Math.max(.65, Math.min(2.5, options.scale))
            view.renderer.setAttribute('margin', '36px')
            view.renderer.setAttribute('gap', '6%')
            view.renderer.setAttribute('max-inline-size', '820px')
            view.renderer.setAttribute('max-column-count', '1')
            configureLayout()
            document.body.style.background = options.dark ? '#20262b' : '#fbf8f1'
            const toc = cleanToc(book.toc?.length ? book.toc : book.sections.map((s, i) => ({ label: `第 ${i + 1} 节`, href: s.id })))
            try { await view.init({ lastLocation: config.locator || undefined }) }
            catch (error) {
                if (!config.locator) throw error
                console.warn('Saved EPUB position could not be restored', error)
                await view.goTo(Math.max(0, book.sections.findIndex(s => s.linear !== 'no')))
            }
            if (!view.renderer.getContents().length)
                await view.goTo(Math.max(0, book.sections.findIndex(s => s.linear !== 'no')))
            if (!view.renderer.getContents().length) throw Error('没有载入可显示的 EPUB 章节')
            for (const { doc } of view.renderer.getContents()) applyDocumentStyle(doc)
            message.hidden = true
            send('loaded', { title: String(book.metadata?.title ?? ''), sections: book.sections.length, toc, fixedLayout: view.isFixedLayout, scale: options.scale })
            requestAnimationFrame(reportZoom)
        } catch (error) { fail(error) }
    },
    async configure(config) {
        const cfi = view.lastLocation?.cfi
        options = config
        document.body.style.background = options.dark ? '#20262b' : '#fbf8f1'
        configureLayout()
        for (const { doc } of view.renderer.getContents()) applyDocumentStyle(doc)
        await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))
        if (cfi) await view.goTo(cfi)
        queueSelection()
        requestAnimationFrame(reportZoom)
    },
    async goTo(target) { await view.goTo(target) },
    next() { return view.next() },
    previous() { return view.prev() },
    selectAll() {
        const doc = selectedDocument ?? view.renderer.getContents()[0]?.doc
        if (!doc) return
        const range = doc.createRange(); range.selectNodeContents(doc.body)
        const sel = doc.getSelection(); sel.removeAllRanges(); sel.addRange(range); selection()
    },
    clearSelection() { for (const { doc } of view.renderer?.getContents() ?? []) doc.getSelection()?.removeAllRanges(); selection() },
    async find(query, matchCase) {
        const generation = ++searchGeneration
        matches = []; matchIndex = -1; view.clearSearch()
        if (query) for await (const result of view.search({ query, matchCase })) {
            if (generation !== searchGeneration) return
            if (result.subitems) matches.push(...result.subitems)
            if (matches.length >= 1000) { matches = matches.slice(0, 1000); break }
        }
        if (generation !== searchGeneration) return
        if (matches.length) { matchIndex = 0; await view.goTo(matches[0].cfi) }
        send('find', { current: matchIndex + 1, total: matches.length, truncated: matches.length >= 1000 })
    },
    async findNext(backwards) {
        if (!matches.length) return
        matchIndex = (matchIndex + (backwards ? -1 : 1) + matches.length) % matches.length
        await view.goTo(matches[matchIndex].cfi)
        send('find', { current: matchIndex + 1, total: matches.length, truncated: matches.length >= 1000 })
    },
    // Gives native integration tests access to the actual DOM and layout.
    view,
}
// Native toolbar actions can arrive while a chapter is still loading. Keep
// navigation and reflow ordered so a late CFI restore cannot undo a new jump.
let operation = Promise.resolve()
for (const name of ['open', 'configure', 'goTo', 'next', 'previous', 'find', 'findNext']) {
    const action = reader[name].bind(reader)
    reader[name] = (...args) => (operation = operation.then(() => action(...args)).catch(fail))
}
send('boot')
