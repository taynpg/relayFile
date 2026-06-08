#ifndef EXPLORERCONTROL_H
#define EXPLORERCONTROL_H

#include <QDialog>

namespace Ui {
class ExplorerControl;
}

class ExplorerControl : public QDialog
{
    Q_OBJECT

public:
    explicit ExplorerControl(QWidget* parent = nullptr);
    ~ExplorerControl();

private:
    Ui::ExplorerControl* ui;
};

#endif   // EXPLORERCONTROL_H
