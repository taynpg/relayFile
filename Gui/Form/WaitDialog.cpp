#include "WaitDialog.h"

#include <QMessageBox>
#include <QHBoxLayout>

WaitDialog::WaitDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle("等待回应");
    setModal(true);

    setFixedSize(260, 100);

    auto* layout = new QHBoxLayout(this);

    progressBar = new QProgressBar(this);
    progressBar->setRange(0, 0);
    progressBar->setTextVisible(false);

    cancelBtn = new QPushButton("取消", this);

    layout->addWidget(progressBar);
    layout->addWidget(cancelBtn);

    connect(cancelBtn, &QPushButton::clicked, this, &WaitDialog::Quit);
}

bool WaitDialog::isCancelled() const
{
    return cancelled;
}

void WaitDialog::Quit()
{
    if (cancelled) {
        return;
    }
    cancelled = true;
    emit cancelRequested();
    close();
}

void WaitDialog::QuitWithNotify(const QString& msg)
{
    if (cancelled) {
        return;
    }
    cancelled = true;
    emit cancelRequested();
    close();
    QMessageBox::information(this, "提示", msg);
}
