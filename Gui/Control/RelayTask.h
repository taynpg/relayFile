#ifndef RELAYTASK_H
#define RELAYTASK_H

#include <QDialog>

namespace Ui {
class RelayTask;
}

class RelayTask : public QDialog
{
    Q_OBJECT

public:
    explicit RelayTask(QWidget* parent = nullptr);
    ~RelayTask();

private:
    Ui::RelayTask* ui;
};

#endif   // RELAYTASK_H
