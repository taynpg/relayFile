#ifndef CONNECTORCONTROL_H
#define CONNECTORCONTROL_H

#include <QDialog>

namespace Ui {
class ConnectorControl;
}

class ConnectorControl : public QDialog
{
    Q_OBJECT

public:
    explicit ConnectorControl(QWidget* parent = nullptr);
    ~ConnectorControl();

private:
    Ui::ConnectorControl* ui;
};

#endif   // CONNECTORCONTROL_H
