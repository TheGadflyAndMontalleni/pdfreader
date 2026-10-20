#pragma once
#include <QByteArray>
#include <QString>

namespace translate {
QByteArray ProtectKey(const QString& key);
QString UnprotectKey(const QByteArray& encrypted);
}
