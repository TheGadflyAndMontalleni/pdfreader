#include "translation_controller.h"
#include "credentials.h"
#include "tls_runtime.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace translate {
TranslationController::TranslationController(pdfreader::HostV1* host, QUrl endpoint, int timeout)
    : host_(host), client_(new TranslationClient(this, std::move(endpoint), timeout)) {}
TranslationController::~TranslationController() { stop(); }
bool TranslationController::initialize(QString* error) {
    if (!EnsureTls(host_->dataDirectory(), error)) return false;
    apiKey_ = UnprotectKey(QByteArray::fromBase64(host_->setting("apiKeyProtected").toByteArray()));
    if (apiKey_.isEmpty()) apiKey_ = QString::fromUtf8(qgetenv("DEEPSEEK_API_KEY")).trimmed();
    model_ = host_->setting("model", QString(DefaultModel)).toString();
    popup_ = new GlassPopup(host_->window());
    auto* layout = new QVBoxLayout(popup_);
    layout->setContentsMargins(26, 21, 26, 25); layout->setSpacing(8);
    auto* header = new QHBoxLayout;
    auto* title = new QLabel(tr("阅读翻译"), popup_);
    title->setStyleSheet("font-size: 11px; font-weight: 600; color: #536d88;");
    header->addWidget(title); header->addStretch();
    auto* close = new QPushButton(QStringLiteral("×"), popup_);
    close->setObjectName("translationClose"); close->setToolTip(tr("关闭（Esc）"));
    close->setFixedSize(25, 25); close->setStyleSheet("font-size: 18px; padding: 0;");
    header->addWidget(close); layout->addLayout(header);
    provider_ = new QComboBox(popup_); provider_->setObjectName("translationProvider");
    provider_->addItem(tr("自动 · 单词词典 / 上下文 AI"), "auto");
    provider_->addItem(tr("离线英汉词典"), "dictionary");
    provider_->addItem(tr("DeepSeek · 语境理解"), "deepseek");
    provider_->setCurrentIndex(std::max(0, provider_->findData(host_->setting("provider", "auto"))));
    layout->addWidget(provider_);
    word_ = new QLabel(tr("选中文字后按 Alt+T"), popup_);
    word_->setObjectName("translationWord"); word_->setTextFormat(Qt::PlainText); word_->setWordWrap(true);
    word_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    word_->setMaximumHeight(54);
    layout->addWidget(word_);
    phonetic_ = new QLabel(popup_); phonetic_->setObjectName("translationPhonetic");
    phonetic_->setTextFormat(Qt::PlainText); phonetic_->hide(); layout->addWidget(phonetic_);
    language_ = new QLabel(tr("自动识别 → 中文"), popup_);
    language_->setObjectName("translationLanguage"); language_->setTextFormat(Qt::PlainText);
    layout->addWidget(language_);
    result_ = new QPlainTextEdit(popup_); result_->setObjectName("translationResult");
    result_->setReadOnly(true); result_->setFrameShape(QFrame::NoFrame);
    result_->viewport()->setAutoFillBackground(false);
    result_->setPlaceholderText(tr("完整词典释义或上下文解释"));
    result_->setMinimumHeight(44); layout->addWidget(result_, 1);
    status_ = new QLabel(popup_); status_->setObjectName("translationStatus");
    status_->setTextFormat(Qt::PlainText); status_->setWordWrap(true); layout->addWidget(status_);
    auto* row = new QHBoxLayout;
    auto* retry = new QPushButton(tr("重新查询"), popup_); retry->setObjectName("translationRetry");
    copy_ = new QPushButton(tr("复制"), popup_); copy_->setObjectName("translationCopy"); copy_->setEnabled(false);
    auto* settings = new QPushButton(tr("设置"), popup_); settings->setObjectName("translationSettings");
    row->addWidget(retry); row->addWidget(copy_); row->addWidget(settings); layout->addLayout(row);
    auto* footer = new QHBoxLayout;
    automatic_ = new QCheckBox(tr("自动划词"), popup_); automatic_->setObjectName("translationAuto");
    automatic_->setToolTip(tr("单个英文词本地查词；短语或上下文发送给 DeepSeek，最多 8000 字符。"));
    footer->addWidget(automatic_); footer->addStretch();
    note_ = new QLabel(tr("单词本地 · 语境 AI"), popup_); note_->setObjectName("translationNote");
    footer->addWidget(note_); layout->addLayout(footer);
    auto* command = host_->addCommand("translate", tr("查询选词 / 理解上下文（Alt+T）"), [this] { translateSelection(); });
    auto* configure = host_->addCommand("settings", tr("翻译与 DeepSeek 设置…"), [this] { showSettings(); });
    if (!command || !configure) { if (error) *error = tr("无法注册翻译命令。"); return false; }
    command->setShortcut(QKeySequence(Qt::ALT | Qt::Key_T)); host_->window()->addAction(command);
    connect(close, &QPushButton::clicked, popup_, &QWidget::close);
    connect(popup_, &GlassPopup::dismissed, this, [this] {
        client_->cancel(); debounce_.stop(); follow_.stop();
        status_->setText(tr("已取消，点击重新查询。"));
    });
    connect(retry, &QPushButton::clicked, this, [this] {
        lastWord_.isEmpty() ? translateSelection() : requestWord(lastWord_, true, true);
    });
    connect(copy_, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(result_->toPlainText()); });
    connect(settings, &QPushButton::clicked, this, &TranslationController::showSettings);
    connect(provider_, &QComboBox::currentIndexChanged, this, [this] {
        host_->setSetting("provider", provider_->currentData());
        client_->cancel(); pendingAfterSettings_.clear();
        if (!lastWord_.isEmpty()) requestWord(lastWord_, false, true);
    });
    connect(automatic_, &QCheckBox::toggled, this, [this](bool on) {
        host_->setSetting("autoTranslate", on); setAuto(on);
    });
    poll_.setInterval(160); debounce_.setInterval(500); debounce_.setSingleShot(true); follow_.setInterval(200);
    connect(&poll_, &QTimer::timeout, this, &TranslationController::observeSelection);
    connect(&follow_, &QTimer::timeout, this, &TranslationController::followSelection);
    connect(&debounce_, &QTimer::timeout, this, [this] {
        if (host_ && !(QApplication::mouseButtons() & Qt::LeftButton) &&
            ReadingSelection(host_->selectedText()) == observed_ && !observed_.isEmpty()) requestWord(observed_);
    });
    connect(client_, &TranslationClient::succeeded, this, [this](const QString& language, const QString& translation) {
        const Result result{language, translation, {}, "DeepSeek"};
        cache_.insert(model_ + QChar(0) + lastWord_, new Result(result));
        if (ReadingSelection(host_->selectedText()) != requestSelection_) {
            status_->setText(tr("选区已改变，按 Alt+T 查询新内容。")); return;
        }
        display(result);
    });
    connect(client_, &TranslationClient::failed, this, [this](const QString& message) { status_->setText(message); copy_->setEnabled(false); });
    qApp->installEventFilter(this);
    automatic_->setChecked(host_->setting("autoTranslate", false).toBool());
    return true;
}
void TranslationController::stop() {
    if (qApp) qApp->removeEventFilter(this);
    poll_.stop(); debounce_.stop(); follow_.stop(); client_->cancel();
    if (settings_) delete settings_;
    if (popup_) delete popup_;
    host_ = nullptr; apiKey_.fill(QChar(0)); apiKey_.clear(); cache_.clear();
}
bool TranslationController::dictionaryRoute(const QString& word) const {
    const auto provider = provider_->currentData().toString();
    return provider == "dictionary" || (provider == "auto" && IsDictionaryWord(word));
}
QRect TranslationController::selectionRect() const {
    if (const auto* geometry = qobject_cast<pdfreader::SelectionGeometryV1*>(host_->context()))
        return geometry->selectionScreenRect();
    if (!fallbackAnchor_.isEmpty()) return fallbackAnchor_;
    return QRect(QCursor::pos() - QPoint(3, 15), QSize(8, 24));
}
bool TranslationController::showPopup() {
    if (!popup_->present(selectionRect())) {
        host_->showMessage(tr("选区不可见或周围空间不足，请滚动到选区或缩小选区后按 Alt+T。"));
        return false;
    }
    follow_.start(); return true;
}
void TranslationController::followSelection() {
    if (!host_ || !popup_->isVisible()) { follow_.stop(); return; }
    if (host_->window()->isMinimized() || !popup_->present(selectionRect())) {
        popup_->hide(); follow_.stop(); client_->cancel();
    }
}
void TranslationController::display(const Result& result) {
    language_->setText(tr("%1 · %2 → 中文").arg(result.source, result.language));
    phonetic_->setText(result.phonetic); phonetic_->setVisible(!result.phonetic.isEmpty());
    result_->setPlainText(result.translation); status_->clear(); copy_->setEnabled(true);
}
void TranslationController::translateSelection() {
    debounce_.stop();
    const auto selection = ReadingSelection(host_->selectedText());
    if (selection.isEmpty()) {
        client_->cancel(); host_->showMessage(tr("请先选中单词、短语或上下文（最多 8000 字符），再按 Alt+T。")); return;
    }
    requestWord(selection, false, true);
}
void TranslationController::requestWord(const QString& word, bool force, bool offerSettings) {
    client_->cancel(); debounce_.stop();
    lastWord_ = word; requestSelection_ = ReadingSelection(host_->selectedText()); observed_ = requestSelection_;
    word_->setText(word.size() > 85 ? word.left(82) + QStringLiteral("…") : word); word_->setToolTip(word);
    language_->setText(tr("自动识别 → 中文")); phonetic_->hide(); result_->clear(); copy_->setEnabled(false); status_->clear();
    if (!showPopup()) return;
    if (dictionaryRoute(word)) {
        note_->setText(tr("离线词典 · 不联网"));
        if (!IsDictionaryWord(word)) { status_->setText(tr("英汉词典适合单个英文词；短语或上下文请切换 DeepSeek。")); return; }
        const auto entry = dictionary_.lookup(word);
        if (!entry.valid()) { status_->setText(tr("词典未收录此词，可在上方切换 DeepSeek 查询。")); return; }
        display({tr("英语"), entry.text(), entry.phonetic.isEmpty() ? QString() : "/" + entry.phonetic + "/", "ECDICT"});
        return;
    }
    note_->setText(tr("AI · 仅发送选区"));
    if (apiKey_.isEmpty()) {
        status_->setText(tr("上下文理解需要 DeepSeek API Key；单词词典无需 Key。"));
        if (offerSettings) { pendingAfterSettings_ = word; showSettings(); }
        return;
    }
    const auto key = model_ + QChar(0) + word;
    if (!force) if (const auto* cached = cache_.object(key)) { display(*cached); return; }
    status_->setText(tr("正在理解语境…")); client_->start(word, apiKey_, model_);
}
void TranslationController::setAuto(bool enabled) {
    debounce_.stop(); observed_.clear();
    if (enabled) { poll_.start(); observeSelection(); } else poll_.stop();
}
void TranslationController::observeSelection() {
    if (!host_) return;
    if (QApplication::mouseButtons() & Qt::LeftButton) { debounce_.stop(); return; }
    const auto word = ReadingSelection(host_->selectedText());
    if (word == observed_) return;
    observed_ = word; debounce_.stop(); client_->cancel();
    if (!word.isEmpty() && (dictionaryRoute(word) || !apiKey_.isEmpty())) debounce_.start();
}
void TranslationController::selectionChanged() {
    if (!host_) return;
    const auto selection = ReadingSelection(host_->selectedText());
    if (client_->pending() && selection != lastWord_) {
        client_->cancel(); status_->setText(tr("选区已改变，按 Alt+T 查询新内容。"));
    }
    if (selection.isEmpty()) { popup_->hide(); follow_.stop(); debounce_.stop(); }
    if (automatic_->isChecked()) observeSelection();
}
void TranslationController::documentChanged() {
    client_->cancel(); debounce_.stop(); follow_.stop(); observed_.clear(); lastWord_.clear(); pendingAfterSettings_.clear();
    fallbackAnchor_ = {}; trackingDrag_ = false;
    if (result_) result_->clear();
    if (popup_) popup_->hide();
}
bool TranslationController::eventFilter(QObject* object, QEvent* event) {
    if (!host_) return false;
    auto* widget = qobject_cast<QWidget*>(object);
    if (!widget || widget == popup_ || (popup_ && popup_->isAncestorOf(widget))) return false;
    if (widget != host_->window() && !host_->window()->isAncestorOf(widget)) return false;
    bool pdf = false;
    for (auto* parent = widget; parent && parent != host_->window(); parent = parent->parentWidget())
        if (parent->inherits("PdfView")) { pdf = true; break; }
    if (pdf && event->type() == QEvent::MouseButtonPress) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) { dragStart_ = mouse->globalPosition().toPoint(); trackingDrag_ = true; }
    } else if (pdf && event->type() == QEvent::MouseButtonRelease && trackingDrag_) {
        trackingDrag_ = false;
        fallbackAnchor_ = QRect(dragStart_, static_cast<QMouseEvent*>(event)->globalPosition().toPoint()).normalized().adjusted(-4, -16, 4, 9);
    }
    if (popup_ && popup_->isVisible() && (event->type() == QEvent::Wheel || event->type() == QEvent::Resize || event->type() == QEvent::Move)) {
        QTimer::singleShot(0, this, [this] { if (host_ && popup_ && popup_->isVisible()) { followSelection(); popup_->refreshBackdrop(); } });
    }
    return false;
}
void TranslationController::showSettings() {
    if (settings_) { settings_->show(); settings_->raise(); settings_->activateWindow(); return; }
    auto* dialog = new QDialog(host_->window()); settings_ = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setObjectName("deepseekSettingsDialog");
    dialog->setWindowTitle(tr("翻译设置"));
    auto* layout = new QVBoxLayout(dialog); auto* form = new QFormLayout;
    auto* key = new QLineEdit(apiKey_, dialog); key->setObjectName("deepseekApiKey"); key->setEchoMode(QLineEdit::Password);
    key->setMaxLength(1024); key->setPlaceholderText(tr("仅 DeepSeek 语境理解需要"));
    auto* model = new QComboBox(dialog); model->setObjectName("deepseekModel"); model->setEditable(true);
    model->addItems({DefaultModel, "deepseek-v4-pro"}); model->setCurrentText(model_);
    form->addRow(tr("DeepSeek API Key"), key); form->addRow(tr("模型"), model); layout->addLayout(form);
    auto* note = new QLabel(tr("自动模式：单个英文词 → 离线英汉词典，展示完整词条。\n短语或上下文 → DeepSeek，自动识别语言并用中文解释。\n只发送选中内容，最多 8000 字符；Key 在本机加密保存。"), dialog);
    note->setWordWrap(true); layout->addWidget(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, dialog);
    buttons->button(QDialogButtonBox::Save)->setText(tr("保存")); buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(dialog, &QDialog::rejected, this, [this] { pendingAfterSettings_.clear(); });
    connect(buttons, &QDialogButtonBox::accepted, this, [this, dialog, key, model] {
        const auto plain = key->text().trimmed(); const auto selectedModel = model->currentText().trimmed();
        static const QRegularExpression name("^[A-Za-z0-9][A-Za-z0-9._:/-]{0,99}$");
        if (plain.contains('\r') || plain.contains('\n') || !name.match(selectedModel).hasMatch()) {
            QMessageBox::warning(dialog, tr("设置无效"), tr("请检查 API Key 和模型名称。")); return;
        }
        const auto encrypted = ProtectKey(plain);
        if (!plain.isEmpty() && encrypted.isEmpty()) { QMessageBox::warning(dialog, tr("无法保存"), tr("Windows 无法加密 API Key，请重试。")); return; }
        host_->setSetting("apiKeyProtected", encrypted.toBase64()); host_->setSetting("model", selectedModel);
        apiKey_ = plain.isEmpty() ? QString::fromUtf8(qgetenv("DEEPSEEK_API_KEY")).trimmed() : plain;
        model_ = selectedModel; cache_.clear(); client_->cancel(); observed_.clear();
        status_->setText(tr("设置已保存，选词后按 Alt+T。"));
        const auto pending = pendingAfterSettings_; pendingAfterSettings_.clear(); dialog->accept();
        if (!pending.isEmpty() && !apiKey_.isEmpty() && ReadingSelection(host_->selectedText()) == pending) requestWord(pending);
    });
    dialog->resize(440, dialog->sizeHint().height()); dialog->show();
}
}
