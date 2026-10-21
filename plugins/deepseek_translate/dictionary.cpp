#include "dictionary.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QtEndian>

namespace translate {
namespace {
QString Key(QString word) {
    static const QRegularExpression edges("^[\\p{P}\\p{Z}]+|[\\p{P}\\p{Z}]+$");
    return word.normalized(QString::NormalizationForm_KC).replace(QChar(0x2019), QChar('\''))
        .trimmed().remove(edges).toCaseFolded();
}
}
bool IsDictionaryWord(const QString& text) {
    static const QRegularExpression word("^[A-Za-z]+(?:[-'][A-Za-z]+)*$");
    return text.size() <= 100 && word.match(Key(text)).hasMatch();
}
QString DictionaryEntry::text() const {
    QStringList sections;
    if (!chinese.isEmpty()) sections << chinese;
    if (!english.isEmpty()) sections << QStringLiteral("英英释义\n") + english;
    static const QMap<QString, QString> labels{{"p", "过去式"}, {"d", "过去分词"},
        {"i", "现在分词"}, {"3", "第三人称单数"}, {"r", "比较级"}, {"t", "最高级"},
        {"s", "复数"}, {"0", "原形"}};
    QStringList forms;
    for (const auto& item : exchange.split('/')) {
        const int colon = item.indexOf(':');
        const auto label = labels.value(item.left(colon));
        if (colon > 0 && !label.isEmpty()) forms << label + " " + item.mid(colon + 1);
    }
    if (!forms.isEmpty()) sections << QStringLiteral("词形变化\n") + forms.join(QStringLiteral(" · "));
    return sections.join("\n\n");
}
DictionaryEntry Dictionary::lookup(const QString& word) {
    if (!IsDictionaryWord(word)) return {};
    if (!file_.isOpen()) {
        if (!file_.open(QIODevice::ReadOnly) || file_.read(8) != QByteArray("PDICT2\n\0", 8)) return {};
        const auto lengthBytes = file_.read(4);
        if (lengthBytes.size() != 4) return {};
        const auto length = qFromBigEndian<quint32>(lengthBytes.constData());
        if (length > 1024 * 1024) return {};
        index_ = QJsonDocument::fromJson(file_.read(length)).array();
        dataOffset_ = file_.pos();
    }
    const auto key = Key(word);
    int first = 0, last = index_.size();
    while (first < last) {
        const int mid = first + (last - first) / 2;
        if (index_[mid].toArray()[0].toString() < key) first = mid + 1;
        else last = mid;
    }
    if (first >= index_.size()) return {};
    const auto block = index_[first].toArray();
    if (block.size() != 3) return {};
    const auto offset = block[1].toInteger();
    const auto size = block[2].toInteger();
    if (offset < 0 || size <= 0 || size > 1024 * 1024 || !file_.seek(dataOffset_ + offset)) return {};
    const auto compressed = file_.read(size);
    if (compressed.size() != size || compressed.size() < 4 ||
        qFromBigEndian<quint32>(compressed.constData()) > 8 * 1024 * 1024) return {};
    const auto entries = QJsonDocument::fromJson(qUncompress(compressed)).array();
    for (const auto& entry : entries) {
        const auto pair = entry.toArray();
        if (pair.size() != 2 || pair[0].toString() != key) continue;
        const auto fields = pair[1].toObject();
        return {fields["w"].toString(), fields["p"].toString(), fields["t"].toString(),
                fields["d"].toString(), fields["e"].toString()};
    }
    return {};
}
}
