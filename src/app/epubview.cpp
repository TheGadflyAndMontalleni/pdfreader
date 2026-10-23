#include "epubview.h"
#include "epubserver.h"
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QResizeEvent>
#include <QTimer>
#include <algorithm>
#include <cmath>

// Include Qt first: Windows headers define names that collide with Qt APIs.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <webview/webview.h>

#ifdef __MINGW32__
__CRT_UUID_DECL(ICoreWebView2NavigationStartingEventHandler, 0x9adbe429, 0xf36d, 0x432b, 0x9d, 0xdc, 0xf8, 0x88, 0x1f, 0xbd, 0x76, 0xe3)
__CRT_UUID_DECL(ICoreWebView2NewWindowRequestedEventHandler, 0xd4c185fe, 0xc81c, 0x4989, 0x97, 0xaf, 0x2d, 0x3f, 0xa7, 0xab, 0x56, 0x51)
__CRT_UUID_DECL(ICoreWebView2PermissionRequestedEventHandler, 0x15e1c6a3, 0xc72a, 0x4df3, 0x91, 0xd7, 0xd0, 0x97, 0xfb, 0xec, 0x6b, 0xfd)
__CRT_UUID_DECL(ICoreWebView2CapturePreviewCompletedHandler, 0x697e05e9, 0x3d8f, 0x45fa, 0x96, 0xf4, 0x8f, 0xfe, 0x1e, 0xde, 0xda, 0xf5)
#endif

namespace {
template<class Interface, class... Args> class Callback final : public Interface {
public:
    explicit Callback(std::function<HRESULT(Args...)> fn) : fn_(std::move(fn)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, __uuidof(Interface))) {
            *value = static_cast<Interface*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE Invoke(Args... args) override { return fn_(args...); }
private:
    ULONG refs_ = 1;
    std::function<HRESULT(Args...)> fn_;
};
QString Json(const QJsonValue& value) {
    return QString::fromUtf8(QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
}
}
struct EpubView::Native {
    webview_t view = nullptr;
    ICoreWebView2Controller* controller = nullptr; // borrowed from webview
    ICoreWebView2Controller3* scaling = nullptr;
    ICoreWebView2* core = nullptr;
    HWND widget = nullptr;
    bool comInitialized = false;
    ~Native() {
        if (controller) controller->Close();
        if (scaling) scaling->Release();
        if (core) core->Release();
        if (view) webview_destroy(view);
        if (comInitialized) CoUninitialize();
    }
};
EpubView::EpubView(QWidget* parent) : QWidget(parent), server_(new EpubServer(this)) {
    viewportTimer_ = new QTimer(this);
    viewportTimer_->setSingleShot(true);
    connect(viewportTimer_, &QTimer::timeout, this, &EpubView::syncViewport);
    setObjectName("epubView");
    setAttribute(Qt::WA_NativeWindow);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(160, 120);
}
EpubView::~EpubView() {
    closing_ = true;
    viewportTimer_->stop();
    saveState();
    disconnect(this, nullptr, nullptr, nullptr);
    native_.reset();
}
bool EpubView::open(const QString& path, QString* error) {
    if (native_ || !server_->openBook(path, error)) return false;
    path_ = path;
    const auto saved = app::Session::LoadDocState(path);
    fontScale_ = saved.valid && std::isfinite(saved.zoom) ? std::clamp(saved.zoom, .1, 8.) : 1;
    dark_ = saved.invert;
    scrolled_ = saved.viewMode == 0;
    locator_ = saved.locator;
    fitMode_ = saved.valid ? std::clamp(saved.fitMode, 0, 2) : 2;
    native_ = std::make_unique<Native>();
    native_->comInitialized = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    native_->view = webview_create(false, reinterpret_cast<void*>(winId()));
    if (!native_->view) {
        if (error) *error = tr("EPUB 阅读需要 Microsoft Edge WebView2 Runtime。请安装后重新打开：\nhttps://go.microsoft.com/fwlink/p/?LinkId=2124703");
        native_.reset();
        return false;
    }
    native_->widget = static_cast<HWND>(webview_get_native_handle(native_->view, WEBVIEW_NATIVE_HANDLE_KIND_UI_WIDGET));
    native_->controller = static_cast<ICoreWebView2Controller*>(webview_get_native_handle(native_->view, WEBVIEW_NATIVE_HANDLE_KIND_BROWSER_CONTROLLER));
    constexpr GUID controller3Id{0xf9614724, 0x5d2b, 0x41dc, {0xae, 0xf7, 0x73, 0xd6, 0x2b, 0x51, 0x54, 0x3b}};
    native_->controller->QueryInterface(controller3Id, reinterpret_cast<void**>(&native_->scaling));
    if (native_->scaling) {
        native_->scaling->put_ShouldDetectMonitorScaleChanges(FALSE);
        native_->scaling->put_BoundsMode(COREWEBVIEW2_BOUNDS_MODE_USE_RAW_PIXELS);
    }
    native_->controller->get_CoreWebView2(&native_->core);
    ICoreWebView2Settings* settings = nullptr;
    native_->core->get_Settings(&settings);
    if (settings) {
        settings->put_AreDefaultContextMenusEnabled(FALSE);
        settings->put_AreDefaultScriptDialogsEnabled(FALSE);
        settings->put_IsZoomControlEnabled(FALSE);
        // Keep Ctrl+F / Ctrl+P and zoom under the reader's command routing.
        constexpr GUID settings3Id{0xfdb5ab74, 0xaf33, 0x4854, {0x84, 0xf0, 0x0a, 0x63, 0x1d, 0xeb, 0x5e, 0xba}};
        ICoreWebView2Settings3* settings3 = nullptr;
        if (SUCCEEDED(settings->QueryInterface(settings3Id, reinterpret_cast<void**>(&settings3)))) {
            settings3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
            settings3->Release();
        }
        settings->Release();
    }
    const QString allowed = server_->url().toString();
    using Nav = Callback<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*>;
    auto* nav = new Nav([allowed](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) {
        LPWSTR uri = nullptr; args->get_Uri(&uri);
        const QString target = QString::fromWCharArray(uri ? uri : L"");
        CoTaskMemFree(uri);
        if (target != allowed) args->put_Cancel(TRUE);
        return S_OK;
    });
    EventRegistrationToken token{};
    native_->core->add_NavigationStarting(nav, &token); nav->Release();
    auto* frames = new Nav([](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) {
        LPWSTR uri = nullptr; args->get_Uri(&uri);
        const QString target = QString::fromWCharArray(uri ? uri : L""); CoTaskMemFree(uri);
        if (!target.startsWith("blob:") && target != "about:blank") args->put_Cancel(TRUE);
        return S_OK;
    });
    native_->core->add_FrameNavigationStarting(frames, &token); frames->Release();
    using NewWindow = Callback<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*>;
    auto* windows = new NewWindow([](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) { args->put_Handled(TRUE); return S_OK; });
    native_->core->add_NewWindowRequested(windows, &token); windows->Release();
    using Permission = Callback<ICoreWebView2PermissionRequestedEventHandler, ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs*>;
    auto* permission = new Permission([](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) { args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY); return S_OK; });
    native_->core->add_PermissionRequested(permission, &token); permission->Release();
    webview_bind(native_->view, "readerEvent", [](const char* id, const char* request, void* arg) {
        auto* self = static_cast<EpubView*>(arg);
        const QByteArray data(request);
        // Return first: the callback may cause the view to close at a Qt signal.
        webview_return(self->native_->view, id, 0, "null");
        if (data.size() <= 8 * 1024 * 1024) QTimer::singleShot(0, self, [self, data] { self->receive(data); });
    }, this);
    syncViewport();
    webview_navigate(native_->view, allowed.toUtf8().constData());
    return true;
}
bool EpubView::event(QEvent* event) {
    const bool result = QWidget::event(event);
    if (viewportTimer_ && (event->type() == QEvent::DevicePixelRatioChange ||
        event->type() == QEvent::WinIdChange || event->type() == QEvent::ParentChange))
        viewportTimer_->start(0);
    return result;
}
void EpubView::resizeEvent(QResizeEvent* event) {
    if (event) QWidget::resizeEvent(event);
    // Qt can deliver resize/show before the HWND's client geometry is updated.
    // Repeat after Qt finishes the layout, including tab switches on startup.
    syncViewport();
    viewportTimer_->start(0);
}
void EpubView::syncViewport() {
    if (closing_ || !native_ || !native_->widget) return;
    const auto host = reinterpret_cast<HWND>(winId());
    if (GetParent(native_->widget) != host) SetParent(native_->widget, host);
    if (native_->scaling) native_->scaling->put_RasterizationScale(devicePixelRatioF());
    RECT rect{}; GetClientRect(host, &rect);
    MoveWindow(native_->widget, 0, 0, rect.right, rect.bottom, TRUE);
    // A same-size MoveWindow need not send WM_SIZE. Set the browser's bounds
    // explicitly, and discard persisted browser zoom (book zoom is separate).
    native_->controller->SetBoundsAndZoomFactor(rect, 1.0);
    native_->controller->put_IsVisible(isVisible());
    native_->controller->NotifyParentWindowPositionChanged();
}
void EpubView::showEvent(QShowEvent* event) { QWidget::showEvent(event); resizeEvent(nullptr); }
void EpubView::focusInEvent(QFocusEvent* event) {
    QWidget::focusInEvent(event);
    if (native_ && native_->controller) native_->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
}
void EpubView::evaluate(const QString& script) {
    if (native_ && native_->view) webview_eval(native_->view, script.toUtf8().constData());
}
void EpubView::receive(const QByteArray& json) {
    if (closing_) return;
    const auto array = QJsonDocument::fromJson(json).array();
    if (array.isEmpty()) return;
    const auto value = array.at(0).toObject();
    const QString type = value.value("type").toString();
    if (type == "boot") {
        syncViewport(); viewportTimer_->start(0);
        evaluate("reader.open(" + Json(QJsonObject{{"locator", locator_}, {"scale", fontScale_}, {"dark", dark_}, {"scrolled", scrolled_}, {"fitMode", fitMode_}}) + ")");
    } else if (type == "loaded") {
        ready_ = true; title_ = value.value("title").toString(); toc_ = value.value("toc").toArray();
        sectionCount_ = value.value("sections").toInt();
        fixedLayout_ = value.value("fixedLayout").toBool();
        fontScale_ = value.value("scale").toDouble(fontScale_);
        syncViewport(); viewportTimer_->start(0);
        emit loaded();
    } else if (type == "error") {
        error_ = value.value("message").toString(); ready_ = false; emit failed(error_);
    } else if (type == "location") {
        locator_ = value.value("cfi").toString();
        section_ = value.value("section").toInt();
        progress_ = std::clamp(value.value("fraction").toDouble(), 0., 1.);
        emit locationChanged();
    } else if (type == "selection") {
        selection_ = value.value("text").toString().left(100000);
        const auto bounds = value.value("rect").toArray();
        selectionRect_ = bounds.size() == 4 ? QRectF(bounds.at(0).toDouble(), bounds.at(1).toDouble(), bounds.at(2).toDouble(), bounds.at(3).toDouble()) : QRectF();
        cssViewport_ = QSizeF(value.value("width").toDouble(), value.value("height").toDouble());
        emit selectionChanged();
    } else if (type == "zoom") {
        const double zoom = value.value("scale").toDouble();
        if (std::isfinite(zoom) && zoom > 0 && std::abs(zoom - effectiveZoom_) > .0001) {
            effectiveZoom_ = zoom; emit locationChanged();
        }
    } else if (type == "text") {
        if (sectionTexts_.size() >= 16) sectionTexts_.erase(sectionTexts_.begin());
        sectionTexts_[value.value("index").toInt()] = value.value("text").toString().left(500000);
    } else if (type == "find") {
        emit findChanged(value.value("current").toInt(), value.value("total").toInt(), value.value("truncated").toBool());
    } else if (type == "command") emit commandRequested(value.value("command").toString());
    else if (type == "test") emit testResult(value);
}
void EpubView::configure() {
    if (ready_) evaluate("reader.configure(" + Json(QJsonObject{{"scale", fontScale_}, {"dark", dark_}, {"scrolled", scrolled_}, {"fitMode", fitMode_}}) + ")");
}
void EpubView::setFontScale(double scale) {
    fontScale_ = fixedLayout_ ? std::clamp(scale, .1, 8.) : std::clamp(scale, .65, 2.5);
    if (fixedLayout_) fitMode_ = 0;
    configure(); emit locationChanged();
}
void EpubView::setFitMode(int mode) {
    if (!fixedLayout_) return;
    fitMode_ = std::clamp(mode, 0, 2); configure(); emit locationChanged();
}
void EpubView::setDark(bool dark) { dark_ = dark; configure(); emit locationChanged(); }
void EpubView::setScrolled(bool scrolled) { scrolled_ = scrolled; configure(); emit locationChanged(); }
void EpubView::goTo(const QString& target) { if (ready_) evaluate("reader.goTo(" + Json(target) + ")"); }
void EpubView::goToSection(int index) { if (ready_ && index >= 0 && index < sectionCount_) evaluate(QString("reader.goTo(%1)").arg(index)); }
void EpubView::next() { if (ready_) evaluate("reader.next()"); }
void EpubView::previous() { if (ready_) evaluate("reader.previous()"); }
void EpubView::selectAll() { evaluate("reader.selectAll()"); }
void EpubView::clearSelection() { evaluate("reader.clearSelection()"); }
void EpubView::copySelection() { QApplication::clipboard()->setText(selection_); }
void EpubView::find(const QString& text, bool matchCase) { if (ready_) evaluate("reader.find(" + Json(text) + "," + (matchCase ? "true" : "false") + ")"); }
void EpubView::findNext(bool backwards) { if (ready_) evaluate(QString("reader.findNext(%1)").arg(backwards ? "true" : "false")); }
QString EpubView::sectionText(int index) const { return sectionTexts_.value(index); }
QRect EpubView::selectionScreenRect() const {
    if (selection_.isEmpty() || selectionRect_.isEmpty() || !isVisible() || cssViewport_.isEmpty()) return {};
    const double sx = width() / cssViewport_.width(), sy = height() / cssViewport_.height();
    const QRect rect = QRectF(selectionRect_.x() * sx, selectionRect_.y() * sy, selectionRect_.width() * sx, selectionRect_.height() * sy).toAlignedRect().intersected(this->rect());
    return rect.isEmpty() ? QRect() : QRect(mapToGlobal(rect.topLeft()), rect.size());
}
void EpubView::saveState() {
    if (path_.isEmpty() || !ready_ || locator_.isEmpty()) return;
    app::DocState state;
    state.page = section_; state.zoom = fontScale_; state.invert = dark_; state.viewMode = scrolled_ ? 0 : 1;
    state.fitMode = fitMode_;
    state.valid = true; state.locator = locator_;
    app::Session::SaveDocState(path_, state);
}
QUrl EpubView::origin() const { return server_->url(); }
qint64 EpubView::bytesServed() const { return server_->bytesServed(); }
void EpubView::capture(const QString& path) {
    if (!native_ || !native_->core) { emit captured(false); return; }
    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) { emit captured(false); return; }
    using Capture = Callback<ICoreWebView2CapturePreviewCompletedHandler, HRESULT>;
    const QPointer<EpubView> self(this);
    auto* callback = new Capture([self, stream, path](HRESULT result) {
        bool ok = false; HGLOBAL memory = nullptr;
        if (SUCCEEDED(result) && SUCCEEDED(GetHGlobalFromStream(stream, &memory))) {
            STATSTG stat{}; stream->Stat(&stat, STATFLAG_NONAME);
            if (void* bytes = GlobalLock(memory)) {
                QFile file(path);
                ok = file.open(QIODevice::WriteOnly) && file.write(static_cast<const char*>(bytes), stat.cbSize.QuadPart) == qint64(stat.cbSize.QuadPart);
                GlobalUnlock(memory);
            }
        }
        stream->Release();
        if (self) emit self->captured(ok);
        return S_OK;
    });
    const auto result = native_->core->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, stream, callback);
    callback->Release();
    if (FAILED(result)) { stream->Release(); emit captured(false); }
}
