#pragma once
#include "pdfreader_plugin.h"
#include "translation_client.h"
#include "dictionary.h"
#include "glass_popup.h"
#include <QCache>
#include <QPointer>
#include <memory>

class QCheckBox;
class QDialog;
class QComboBox;
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
protected:
    bool eventFilter(QObject* object, QEvent* event) override;
private:
    struct Result { QString language, translation, phonetic, source; };
    void observeSelection();
    void requestWord(const QString& word, bool force = false, bool offerSettings = false);
    void display(const Result& result);
    void setAuto(bool enabled);
    bool dictionaryRoute(const QString& word) const;
    QRect selectionRect() const;
    bool showPopup();
    void followSelection();
    pdfreader::HostV1* host_;
    TranslationClient* client_;
    QTimer poll_, debounce_, follow_;
    Dictionary dictionary_;
    QCache<QString, Result> cache_{128};
    QString apiKey_, model_, observed_, lastWord_, requestSelection_, pendingAfterSettings_;
    QPointer<GlassPopup> popup_;
    QPointer<QComboBox> provider_;
    QPointer<QLabel> word_, language_, status_, phonetic_, note_;
    QPointer<QPlainTextEdit> result_;
    QPointer<QPushButton> copy_;
    QPointer<QCheckBox> automatic_;
    QPointer<QDialog> settings_;
    QRect fallbackAnchor_;
    QPoint dragStart_;
    bool trackingDrag_ = false;
};
}
