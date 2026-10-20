#include "credentials.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>

namespace translate {
namespace {
const char kEntropy[] = "PDFReader.DeepSeekTranslate.APIKey.v1";
DATA_BLOB Entropy() { return {DWORD(sizeof(kEntropy)), reinterpret_cast<BYTE*>(const_cast<char*>(kEntropy))}; }
}
QByteArray ProtectKey(const QString& key) {
    if (key.isEmpty()) return {};
    auto plain = key.toUtf8();
    DATA_BLOB input{DWORD(plain.size()), reinterpret_cast<BYTE*>(plain.data())}, output{};
    auto entropy = Entropy();
    const bool ok = CryptProtectData(&input, L"PDF Reader DeepSeek API Key", &entropy, nullptr, nullptr,
                                    CRYPTPROTECT_UI_FORBIDDEN, &output);
    plain.fill('\0');
    if (!ok) return {};
    QByteArray result(reinterpret_cast<char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    return result;
}
QString UnprotectKey(const QByteArray& encrypted) {
    if (encrypted.isEmpty()) return {};
    DATA_BLOB input{DWORD(encrypted.size()), reinterpret_cast<BYTE*>(const_cast<char*>(encrypted.constData()))}, output{};
    auto entropy = Entropy();
    if (!CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) return {};
    const auto result = QString::fromUtf8(reinterpret_cast<char*>(output.pbData), output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return result;
}
}
