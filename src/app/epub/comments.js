import { Overlayer } from './foliate/overlayer.js'

// Draw outside the book DOM: adding a comment must not change later CFI paths.
// IDs are separate from CFIs so two notes can refer to the same passage.
export class EpubComments {
    items = []
    active = ''
    keys = new WeakMap()
    constructor(view, send) {
        this.view = view
        view.addEventListener('create-overlay', () => requestAnimationFrame(() => this.draw()))
        view.addEventListener('relocate', () => requestAnimationFrame(() => this.draw()))
        view.addEventListener('show-annotation', ({ detail }) => {
            if (!detail.value.startsWith('reader-comment:')) return
            const selection = detail.range.startContainer.ownerDocument.getSelection()
            // Dragging a new selection over a highlight must still work.
            if (selection && !selection.isCollapsed && selection.toString().trim()) return
            send('commentActivated', { id: detail.value.slice('reader-comment:'.length) })
        })
    }
    set(items, active) {
        this.items = items
        this.active = active
        this.draw()
    }
    draw() {
        for (const { doc, index, overlayer } of this.view.renderer?.getContents() ?? []) {
            if (!doc?.documentElement || !overlayer) continue
            for (const key of this.keys.get(overlayer) ?? []) overlayer.remove(key)
            const keys = []
            for (const item of this.items) {
                if (item.section !== index) continue
                try {
                    const resolved = this.view.resolveNavigation(item.cfi)
                    if (resolved?.index !== index) continue
                    const range = resolved.anchor(doc)
                    const key = `reader-comment:${item.id}`
                    overlayer.add(key, range, rects => {
                        const group = doc.createElementNS('http://www.w3.org/2000/svg', 'g')
                        group.dataset.commentId = item.id
                        group.append(Overlayer.highlight(rects, { color: '#edb638' }))
                        if (item.id === this.active) {
                            group.dataset.active = 'true'
                            group.append(Overlayer.outline(rects, { color: '#b87424', width: 1.5 }))
                        }
                        return group
                    })
                    keys.push(key)
                } catch (error) {
                    // A stale individual anchor must not prevent book reading.
                    console.warn('Could not draw EPUB comment', error)
                }
            }
            this.keys.set(overlayer, keys)
        }
    }
    async activate(id, navigate) {
        const item = this.items.find(item => item.id === id)
        if (!item) return
        this.active = id
        if (navigate) {
            // Opening the native sidebar resizes the reading surface first.
            await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))
            await this.view.goTo(item.cfi)
        }
        this.draw()
    }
}
