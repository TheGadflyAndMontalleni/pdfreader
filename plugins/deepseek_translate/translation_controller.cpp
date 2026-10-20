#include "translation_controller.h"
#include "credentials.h"
#include "tls_runtime.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
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
    auto* pane = new QWidget;
    pane->setObjectName("deepseekTranslationPane");
    pane->setMinimumWidth(260);
    auto* layout = new QVBoxLayout(pane);
    layout->setContentsMargins(12, 10, 12, 10);
    language_ = new QLabel(tr("自动识别 → 中文"), pane);
    language_->setObjectName("translationLanguage");
    language_->setTextFormat(Qt::PlainText);
    layout->addWidget(language_);
    word_ = new QLabel(tr("选中生词后按 Alt+T"), pane);
    word_->setObjectName("translationWord");
    word_->setTextFormat(Qt::PlainText);
    word_->setWordWrap(true);
    auto wordFont = word_->font(); wordFont.setBold(true); wordFont.setPointSize(12); word_->setFont(wordFont);
    layout->addWidget(word_);
    result_ = new QPlainTextEdit(pane);
    result_->setObjectName("translationResult");
    result_->setReadOnly(true);
    result_->setFrameShape(QFrame::NoFrame);
    result_->setPlaceholderText(tr("中文释义会显示在这里"));
    result_->setMinimumHeight(72);
    layout->addWidget(result_, 1);
    status_ = new QLabel(pane);
    status_->setObjectName("translationStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    auto* row = new QHBoxLayout;
    auto* retry = new QPushButton(tr("翻译 / 重试"), pane);
    retry->setObjectName("translationRetry");
    copy_ = new QPushButton(tr("复制"), pane);
    copy_->setObjectName("translationCopy");
    copy_->setEnabled(false);
    auto* settings = new QPushButton(tr("设置"), pane);
    settings->setObjectName("translationSettings");
    row->addWidget(retry); row->addWidget(copy_); row->addWidget(settings);
    layout->addLayout(row);
    automatic_ = new QCheckBox(tr("自动划词翻译"), pane);
    automatic_->setObjectName("translationAuto");
    automatic_->setToolTip(tr("松开鼠标后翻译选中的短词；仅将选中文字发送给 DeepSeek。"));
    layout->addWidget(automatic_);
    auto* note = new QLabel(tr("仅发送当前选词 · 默认译为中文"), pane);
    note->setWordWrap(true);
    layout->addWidget(note);
    dock_ = host_->addPanel("translation", tr("生词翻译"), pane);
    if (!dock_) { delete pane; if (error) *error = tr("无法创建翻译窗口。"); return false; }
    dock_->setFloating(true);
    dock_->resize(330, 275);
    dock_->hide();
    auto* command = host_->addCommand("translate", tr("翻译选中词（自动 → 中文）"), [this] { translateSelection(); });
    auto* configure = host_->addCommand("settings", tr("DeepSeek API 设置…"), [this] { showSettings(); });
    if (!command || !configure) { if (error) *error = tr("无法注册翻译命令。"); return false; }
    command->setShortcut(QKeySequence(Qt::ALT | Qt::Key_T));
    host_->window()->addAction(command);
    connect(retry, &QPushButton::clicked, this, [this] { lastWord_.isEmpty() ? translateSelection() : requestWord(lastWord_, true); });
    connect(copy_, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(result_->toPlainText()); });
    connect(settings, &QPushButton::clicked, this, &TranslationController::showSettings);
    connect(dock_, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (!visible && client_->pending()) { client_->cancel(); status_->setText(tr("已取消，点击翻译重试。")); }
    });
    connect(automatic_, &QCheckBox::toggled, this, [this](bool on) {
        host_->setSetting("autoTranslate", on); setAuto(on);
    });
    poll_.setInterval(160);
    debounce_.setInterval(500); debounce_.setSingleShot(true);
    connect(&poll_, &QTimer::timeout, this, &TranslationController::observeSelection);
    connect(&debounce_, &QTimer::timeout, this, [this] {
        if (host_ && !(QApplication::mouseButtons() & Qt::LeftButton) &&
            ShortSelection(host_->selectedText()) == observed_ && !observed_.isEmpty()) requestWord(observed_);
    });
    connect(client_, &TranslationClient::succeeded, this, [this](const QString& language, const QString& translation) {
        const Result result{language, translation};
        cache_.insert(model_ + QChar(0) + lastWord_, new Result(result));
        if (ShortSelection(host_->selectedText()) != requestSelection_) {
            status_->setText(tr("选词已改变，按 Alt+T 翻译新词。")); return;
        }
        display(result);
    });
    connect(client_, &TranslationClient::failed, this, [this](const QString& message) { status_->setText(message); copy_->setEnabled(false); });
    automatic_->setChecked(host_->setting("autoTranslate", false).toBool());
    return true;
}
void TranslationController::stop() {
    poll_.stop(); debounce_.stop(); client_->cancel();
    if (settings_) delete settings_;
    if (dock_) dock_->hide();
    host_ = nullptr; apiKey_.fill(QChar(0)); apiKey_.clear(); cache_.clear();
}
void TranslationController::display(const Result& result) {
    language_->setText(tr("%1 → 中文").arg(result.language));
    result_->setPlainText(result.translation); status_->clear(); copy_->setEnabled(true);
}
void TranslationController::translateSelection() {
    debounce_.stop();
    const auto selection = ShortSelection(host_->selectedText());
    if (selection.isEmpty()) {
        client_->cancel();
        dock_->show(); dock_->raise();
        status_->setText(tr("请在 PDF 中选中生词或短语（最多 200 字符、30 个词），再按 Alt+T。"));
        return;
    }
    requestWord(selection);
    if (apiKey_.isEmpty()) { pendingAfterSettings_ = selection; showSettings(); }
}
void TranslationController::requestWord(const QString& word, bool force) {
    client_->cancel();
    debounce_.stop();
    lastWord_ = word;
    requestSelection_ = ShortSelection(host_->selectedText());
    observed_ = requestSelection_;
    word_->setText(word); language_->setText(tr("自动识别 → 中文"));
    result_->clear(); copy_->setEnabled(false);
    dock_->show(); dock_->raise();
    if (apiKey_.isEmpty()) { status_->setText(tr("请在设置中填写 DeepSeek API Key。")); return; }
    const auto key = model_ + QChar(0) + word;
    if (!force) if (const auto* cached = cache_.object(key)) { display(*cached); return; }
    status_->setText(tr("翻译中…"));
    client_->start(word, apiKey_, model_);
}
void TranslationController::setAuto(bool enabled) {
    debounce_.stop(); observed_.clear();
    if (enabled) { poll_.start(); observeSelection(); } else poll_.stop();
}
void TranslationController::observeSelection() {
    if (!host_) return;
    if (QApplication::mouseButtons() & Qt::LeftButton) { debounce_.stop(); return; }
    const auto word = ShortSelection(host_->selectedText());
    if (word == observed_) return;
    observed_ = word;
    debounce_.stop(); client_->cancel();
    if (!word.isEmpty() && !apiKey_.isEmpty()) debounce_.start();
}
void TranslationController::selectionChanged() {
    if (!host_) return;
    if (client_->pending() && ShortSelection(host_->selectedText()) != lastWord_) {
        client_->cancel(); status_->setText(tr("选词已改变，按 Alt+T 翻译新词。"));
    }
    if (automatic_->isChecked()) observeSelection();
}
void TranslationController::documentChanged() {
    client_->cancel(); debounce_.stop(); observed_.clear(); lastWord_.clear(); pendingAfterSettings_.clear();
    if (word_) word_->setText(tr("选中生词后按 Alt+T"));
    if (result_) result_->clear();
    if (status_) status_->clear();
    if (language_) language_->setText(tr("自动识别 → 中文"));
    if (copy_) copy_->setEnabled(false);
    if (dock_) dock_->hide();
}
void TranslationController::showSettings() {
    if (settings_) { settings_->show(); settings_->raise(); settings_->activateWindow(); return; }
    auto* dialog = new QDialog(host_->window());
    settings_ = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName("deepseekSettingsDialog");
    dialog->setWindowTitle(tr("DeepSeek API 设置"));
    auto* layout = new QVBoxLayout(dialog);
    auto* form = new QFormLayout;
    auto* key = new QLineEdit(apiKey_, dialog);
    key->setObjectName("deepseekApiKey");
    key->setEchoMode(QLineEdit::Password);
    key->setMaxLength(1024);
    key->setPlaceholderText(tr("输入 API Key"));
    auto* model = new QComboBox(dialog);
    model->setObjectName("deepseekModel");
    model->setEditable(true);
    model->addItems({DefaultModel, "deepseek-v4-pro"});
    model->setCurrentText(model_);
    form->addRow(tr("API Key"), key); form->addRow(tr("模型"), model);
    layout->addLayout(form);
    auto* note = new QLabel(tr("自动识别语言，默认翻译为中文。\nAPI Key 在本机加密保存；只发送选中的生词或短语。"), dialog);
    note->setWordWrap(true); layout->addWidget(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, dialog);
    buttons->button(QDialogButtonBox::Save)->setText(tr("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(dialog, &QDialog::rejected, this, [this] { pendingAfterSettings_.clear(); });
    connect(buttons, &QDialogButtonBox::accepted, this, [this, dialog, key, model] {
        const auto plain = key->text().trimmed();
        const auto selectedModel = model->currentText().trimmed();
        static const QRegularExpression name("^[A-Za-z0-9][A-Za-z0-9._:/-]{0,99}$");
        if (plain.contains('\r') || plain.contains('\n') || !name.match(selectedModel).hasMatch()) {
            QMessageBox::warning(dialog, tr("设置无效"), tr("请检查 API Key 和模型名称。")); return;
        }
        const auto encrypted = ProtectKey(plain);
        if (!plain.isEmpty() && encrypted.isEmpty()) { QMessageBox::warning(dialog, tr("无法保存"), tr("Windows 无法加密 API Key，请重试。")); return; }
        host_->setSetting("apiKeyProtected", encrypted.toBase64());
        host_->setSetting("model", selectedModel);
        apiKey_ = plain.isEmpty() ? QString::fromUtf8(qgetenv("DEEPSEEK_API_KEY")).trimmed() : plain;
        model_ = selectedModel; cache_.clear(); client_->cancel(); observed_.clear();
        status_->setText(tr("设置已保存，选词后按 Alt+T。"));
        const auto pending = pendingAfterSettings_; pendingAfterSettings_.clear();
        dialog->accept();
        if (!pending.isEmpty() && !apiKey_.isEmpty()) requestWord(pending);
    });
    dialog->resize(390, dialog->sizeHint().height());
    dialog->show();
}
}
