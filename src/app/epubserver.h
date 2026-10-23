#pragma once
#include <QTcpServer>
#include <QUrl>

// An ephemeral, token-scoped loopback origin. It exposes only embedded reader
// assets and the one already-open book, never arbitrary paths on disk.
class EpubServer : public QTcpServer {
    Q_OBJECT
public:
    explicit EpubServer(QObject* parent = nullptr);
    bool openBook(const QString& path, QString* error);
    QUrl url() const;
    qint64 bytesServed() const { return bytesServed_; }
private:
    void acceptClient();
    QString bookPath_, token_;
    qint64 bytesServed_ = 0;
};
