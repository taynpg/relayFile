#ifndef RELAYFILE_H
#define RELAYFILE_H

#include <QTabWidget>
#include <QWidget>

#include "Control/ConnectorControl.h"
#include "Control/ExplorerControl.h"
#include "Control/ComparisonControl.h"
#include "Control/LogControl.h"
#include "Control/RelayTask.h"


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
    void initControls();
    void initLayout();

private:
    ConnectorControl* connectorControl_{};
    ExplorerControl* localExplorerControl_{};
    ExplorerControl* remoteExplorerControl_{};
    ComparisonControl* comparisonControl_{};
    RelayTask* relayTask_{};
    LogControl* logControl_{};
    QTabWidget* tabWidget_{};

    Ui::relayFile* ui;
};
#endif   // RELAYFILE_H
