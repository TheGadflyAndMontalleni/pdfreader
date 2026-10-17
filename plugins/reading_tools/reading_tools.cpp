#include "pdfreader_plugin.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDockWidget>
#include <QLabel>
#include <QObject>
#include <QPointer>
#include <QVBoxLayout>

class ReadingTools : public QObject, public pdfreader::PluginV1 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PDFREADER_PLUGIN_IID FILE "reading_tools.json")
    Q_INTERFACES(pdfreader::PluginV1)
public:
    bool initialize(pdfreader::HostV1* host, QString*) override {
        host_ = host;
        copy_ = host_->addCommand("copy-page", tr("复制当前页文字"), [this] {
            const auto text = host_->pageText(host_->document().currentPage);
            if (text.isEmpty()) host_->showMessage(tr("当前页没有可提取的文字。"));
            else { QApplication::clipboard()->setText(text); host_->showMessage(tr("已复制当前页文字。")); }
        });
        bookmark_ = host_->addCommand("bookmark", tr("将当前页加入书签"), [this] { host_->addBookmark(); });
        auto* content = new QWidget;
        auto* layout = new QVBoxLayout(content);
        info_ = new QLabel(content);
        info_->setTextFormat(Qt::PlainText);
        info_->setWordWrap(true);
        info_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(info_);
        layout->addStretch();
        auto* panel = host_->addPanel("document-info", tr("文档信息 · 插件示例"), content);
        if (!panel) delete content;
        return copy_ && bookmark_ && panel;
    }
    void shutdown() override { copy_ = nullptr; bookmark_ = nullptr; info_ = nullptr; host_ = nullptr; }
    void documentChanged(const pdfreader::DocumentInfo&) override { refresh(); }
    void pageChanged(int) override { refresh(); }
    void selectionChanged() override { refresh(); }
private:
    void refresh() {
        const auto doc = host_->document();
        copy_->setEnabled(doc.pageCount > 0);
        bookmark_->setEnabled(doc.pageCount > 0);
        info_->setText(doc.pageCount == 0 ? tr("打开 PDF 后显示文档信息。") :
            tr("%1\n\n第 %2 / %3 页\n选中文字：%4 字符")
                .arg(doc.title).arg(doc.currentPage).arg(doc.pageCount).arg(host_->selectedText().size()));
    }
    pdfreader::HostV1* host_ = nullptr;
    QPointer<QAction> copy_, bookmark_;
    QPointer<QLabel> info_;
};
#include "reading_tools.moc"
