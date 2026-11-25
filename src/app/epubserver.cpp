#include "epubserver.h"
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>
#include <QUuid>

static void InitEpubResources() { Q_INIT_RESOURCE(epub); }

EpubServer::EpubServer(QObject* parent) : QTcpServer(parent) {
    InitEpubResources();
    connect(this, &QTcpServer::newConnection, this, &EpubServer::acceptClient);
}
bool EpubServer::openBook(const QString& path, QString* error) {
    QFile probe(path);
    if (!probe.open(QIODevice::ReadOnly) || probe.read(4) != QByteArray("PK\003\004", 4)) {
        if (error) *error = tr("文件不是可读取的 EPUB ZIP 容器。");
        return false;
    }
    if (probe.size() > 1024LL * 1024 * 1024) {
        if (error) *error = tr("试用版暂不支持大于 1 GB 的 EPUB。");
        return false;
    }
    bookPath_ = QFileInfo(path).absoluteFilePath();
    token_ = QUuid::createUuid().toString(QUuid::Id128);
    if (!listen(QHostAddress::LocalHost, 0)) {
        if (error) *error = errorString();
        return false;
    }
    return true;
}
QUrl EpubServer::url() const {
    return QUrl(QString("http://127.0.0.1:%1/%2/index.html").arg(serverPort()).arg(token_));
}
void EpubServer::acceptClient() {
    while (hasPendingConnections()) {
        auto* socket = nextPendingConnection();
        auto* deadline = new QTimer(socket);
        deadline->setSingleShot(true);
        connect(deadline, &QTimer::timeout, socket, &QTcpSocket::abort);
        deadline->start(30000);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        auto header = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket, header] {
            if (socket->property("responded").toBool()) { socket->readAll(); return; }
            *header += socket->readAll();
            if (header->size() > 16384) { socket->abort(); return; }
            if (!header->contains("\r\n\r\n")) return;
            socket->setProperty("responded", true);
            const auto lines = header->split('\n');
            const auto request = lines.value(0).trimmed().split(' ');
            const bool head = request.value(0) == "HEAD";
            const QString prefix = "/" + token_ + "/";
            const QString path = QString::fromLatin1(request.value(1));
            QByteArray host, range, origin;
            for (const auto& raw : lines) {
                const auto line = raw.trimmed();
                const int pos = line.indexOf(':');
                const auto name = line.left(pos).toLower();
                const auto value = line.mid(pos + 1).trimmed();
                if (name == "host") host = value;
                if (name == "range") range = value;
                if (name == "origin") origin = value;
            }
            const QByteArray authority = "127.0.0.1:" + QByteArray::number(serverPort());
            auto fail = [socket](const QByteArray& status) {
                socket->write("HTTP/1.1 " + status + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                socket->disconnectFromHost();
            };
            if (request.size() != 3 || (!head && request[0] != "GET") || host != authority ||
                (!origin.isEmpty() && origin != "http://" + authority) ||
                !path.startsWith(prefix) || path.contains('%') || path.contains("..") || path.contains('\\')) {
                fail("403 Forbidden"); return;
            }
            const QString relative = path.mid(prefix.size());
            const bool book = relative == "book.epub";
            auto file = std::make_shared<QFile>(book ? bookPath_ : ":/epub/" + relative);
            if (!file->open(QIODevice::ReadOnly)) { fail("404 Not Found"); return; }
            const qint64 size = file->size();
            qint64 start = 0, end = size - 1;
            const bool partial = book && !range.isEmpty();
            if (partial) {
                const auto match = QRegularExpression("^bytes=(\\d*)-(\\d*)$").match(QString::fromLatin1(range));
                bool ok = match.hasMatch() && (!match.captured(1).isEmpty() || !match.captured(2).isEmpty());
                if (ok && match.captured(1).isEmpty()) start = std::max(0LL, size - match.captured(2).toLongLong());
                else if (ok) {
                    start = match.captured(1).toLongLong();
                    if (!match.captured(2).isEmpty()) end = std::min(end, match.captured(2).toLongLong());
                }
                if (!ok || start < 0 || start >= size || end < start) { fail("416 Range Not Satisfiable"); return; }
            }
            const qint64 count = end - start + 1;
            QByteArray type = book ? "application/epub+zip" :
                relative.endsWith(".js") ? "text/javascript; charset=utf-8" :
                relative.endsWith(".css") ? "text/css; charset=utf-8" : "text/html; charset=utf-8";
            QByteArray response = partial ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
            response += "Content-Type: " + type + "\r\nContent-Length: " + QByteArray::number(count) +
                "\r\nAccept-Ranges: bytes\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n";
            if (partial) response += "Content-Range: bytes " + QByteArray::number(start) + "-" + QByteArray::number(end) + "/" + QByteArray::number(size) + "\r\n";
            response += "Content-Security-Policy: default-src 'none'; script-src 'self'; style-src 'unsafe-inline' blob:; img-src blob: data:; font-src blob: data:; connect-src 'self'; frame-src blob:; object-src 'none'; base-uri 'none'; form-action 'none'\r\nConnection: close\r\n\r\n";
            socket->write(response);
            if (head) { socket->disconnectFromHost(); return; }
            file->seek(start);
            auto remaining = std::make_shared<qint64>(count);
            auto pump = [this, socket, file, remaining] {
                while (*remaining > 0 && socket->bytesToWrite() < 256 * 1024) {
                    const QByteArray chunk = file->read(std::min(*remaining, 64LL * 1024));
                    if (chunk.isEmpty()) { socket->abort(); return; }
                    *remaining -= chunk.size();
                    bytesServed_ += chunk.size();
                    socket->write(chunk);
                }
                if (*remaining == 0) socket->disconnectFromHost();
            };
            connect(socket, &QTcpSocket::bytesWritten, socket, [pump](qint64) { pump(); });
            pump();
        });
    }
}
