#include "tls_runtime.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QSslSocket>

namespace translate {
bool EnsureTls(const QString& directory, QString* error) {
    auto fail = [&] { if (error) *error = QStringLiteral("无法准备 HTTPS 组件，请检查插件数据目录是否可写。"); return false; };
    if (directory.isEmpty()) return fail();
    QFile resource(":/deepseek-translate/qschannelbackend.dll");
    if (!resource.open(QIODevice::ReadOnly)) return fail();
    const auto payload = resource.readAll();
    const auto root = directory + "/qt-tls-6.8.3";
    if (!QDir().mkpath(root + "/tls")) return fail();
    const auto destination = root + "/tls/qschannelbackend.dll";
    QFile current(destination);
    const bool same = current.open(QIODevice::ReadOnly) && current.readAll() == payload;
    current.close();
    if (!same) {
        QSaveFile file(destination);
        if (!file.open(QIODevice::WriteOnly) || file.write(payload) != payload.size() || !file.commit()) return fail();
    }
    // Retain this path for other QtNetwork users for the remainder of the process.
    // Do not replace an already selected TLS backend or relax certificate checks.
    QCoreApplication::addLibraryPath(root);
    if (!QSslSocket::supportsSsl()) {
        if (error) *error = QStringLiteral("HTTPS 组件无法加载，请使用与阅读器匹配的 Qt 6.8.3 插件版本。");
        return false;
    }
    return true;
}
}
