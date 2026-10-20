#pragma once
#include "pdfreader_plugin.h"
#include "translation_client.h"
#include <QCache>
#include <QPointer>
#include <memory>

class QCheckBox;
class QDialog;
class QDockWidget;
class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace translate {
class TranslationController : public QObject {
    Q_OBJECT
public:
    explicit TranslationController(pdfreader::HostV1* host,
        QUrl endpoint = QUrl("https://api.deepseek.com/chat/completions"), int timeoutMs = 30000);
    ~TranslationController() override;
    bool initialize(QString* error);
    void stop();
    void translateSelection();
    void selectionChanged();
    void documentChanged();
    void showSettings();
private:
    struct Result { QString language, translation; };
    void observeSelection();
    void requestWord(const QString& word, bool force = false);
    void display(const Result& result);
    void setAuto(bool enabled);
    pdfreader::HostV1* host_;
    TranslationClient* client_;
    QTimer poll_, debounce_;
    QCache<QString, Result> cache_{128};
    QString apiKey_, model_, observed_, lastWord_, requestSelection_, pendingAfterSettings_;
    QPointer<QDockWidget> dock_;
    QPointer<QLabel> word_, language_, status_;
    QPointer<QPlainTextEdit> result_;
    QPointer<QPushButton> copy_;
    QPointer<QCheckBox> automatic_;
    QPointer<QDialog> settings_;
};
}
