#include "pdfreader_plugin.h"
#include "translation_controller.h"
#include <QObject>
#include <memory>

class DeepSeekTranslate : public QObject, public pdfreader::PluginV1 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PDFREADER_PLUGIN_IID FILE "deepseek_translate.json")
    Q_INTERFACES(pdfreader::PluginV1)
public:
    bool initialize(pdfreader::HostV1* host, QString* error) override {
        controller_ = std::make_unique<translate::TranslationController>(host);
        return controller_->initialize(error);
    }
    void shutdown() override { controller_.reset(); }
    void documentChanged(const pdfreader::DocumentInfo&) override { if (controller_) controller_->documentChanged(); }
    void selectionChanged() override { if (controller_) controller_->selectionChanged(); }
private:
    std::unique_ptr<translate::TranslationController> controller_;
};
#include "deepseek_plugin.moc"
