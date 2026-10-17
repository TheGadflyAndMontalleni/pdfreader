#include "pdfreader_plugin.h"
#include <QObject>
class WrongInterface : public QObject {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PDFREADER_PLUGIN_IID FILE "wrong_interface.json")
};
#include "wrong_interface.moc"
