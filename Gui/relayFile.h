#ifndef RELAYFILE_H
#define RELAYFILE_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class relayFile;
}
QT_END_NAMESPACE

class relayFile : public QWidget
{
    Q_OBJECT

public:
    relayFile(QWidget* parent = nullptr);
    ~relayFile();

private:
    Ui::relayFile* ui;
};
#endif   // RELAYFILE_H
