#pragma once
#include <QFile>
#include <QJsonArray>
#include <QString>

namespace translate {
bool IsDictionaryWord(const QString& text);
struct DictionaryEntry {
    QString word, phonetic, chinese, english, exchange;
    bool valid() const { return !word.isEmpty(); }
    QString text() const;
};
class Dictionary {
public:
    DictionaryEntry lookup(const QString& word);
private:
    QFile file_{":/deepseek-translate/ecdict.pdict"};
    QJsonArray index_;
    qint64 dataOffset_ = 0;
};
}
