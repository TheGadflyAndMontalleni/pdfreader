#pragma once
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>
#include <QUrl>

namespace translate {
inline constexpr auto DefaultModel = "deepseek-flash";
QString ShortSelection(const QString& text);
QString ReadingSelection(const QString& text);

class TranslationClient : public QObject {
    Q_OBJECT
public:
    explicit TranslationClient(QObject* parent = nullptr,
        QUrl endpoint = QUrl("https://api.deepseek.com/chat/completions"), int timeoutMs = 30000);
    ~TranslationClient() override;
    void start(const QString& word, const QString& apiKey, const QString& model);
    void cancel();
    bool pending() const { return reply_ != nullptr; }
signals:
    void succeeded(const QString& language, const QString& translation);
    void failed(const QString& message);
private:
    void fail(const QString& message);
    void finish();
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> reply_;
    QTimer deadline_;
    QByteArray response_;
    QUrl endpoint_;
    int timeoutMs_;
};
}
