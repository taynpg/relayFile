#ifndef LOGCONTROL_H
#define LOGCONTROL_H

#include <QDialog>

namespace Ui {
class LogControl;
}

class LogControl : public QDialog
{
    Q_OBJECT

public:
    explicit LogControl(QWidget* parent = nullptr);
    ~LogControl();

private:
    void InitMenu();

public:
    void ShowInfo(const QString& msg);

private:
    Ui::LogControl* ui;
};

#endif   // LOGCONTROL_H
