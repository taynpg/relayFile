#pragma once

#include <QDialog>
#include <QProgressBar>
#include <QPushButton>

class WaitDialog : public QDialog
{
    Q_OBJECT
public:
    explicit WaitDialog(QWidget* parent = nullptr);

    bool isCancelled() const;

signals:
    void cancelRequested();

public slots:
    void Quit();
    void QuitWithNotify(const QString& msg);

private:
    QProgressBar* progressBar;
    QPushButton* cancelBtn;
    bool cancelled = false;
};