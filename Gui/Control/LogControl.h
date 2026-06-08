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

public:
    void Debug(const QString& msg);
    void Info(const QString& msg);
    void Warn(const QString& msg);
    void Error(const QString& msg);

private:
    void formatMsg(QString& msg);

   private:
    Ui::LogControl* ui;
};

#endif   // LOGCONTROL_H
