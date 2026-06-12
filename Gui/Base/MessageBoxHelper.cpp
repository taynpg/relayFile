#include "MessageBoxHelper.h"

#include <QMessageBox>
#include <QPushButton>

MessageBoxHelper::Result MessageBoxHelper::questionThreeButtons(QWidget* parent, const QString& title, const QString& text)
{
    QMessageBox msgBox(parent);
    msgBox.setWindowTitle(title);
    msgBox.setText(text);
    msgBox.setIcon(QMessageBox::Question);

    QPushButton* yesBtn = msgBox.addButton("是", QMessageBox::YesRole);
    QPushButton* noBtn = msgBox.addButton("否", QMessageBox::NoRole);
    QPushButton* exitBtn = msgBox.addButton("退出", QMessageBox::RejectRole);

    msgBox.exec();

    if (msgBox.clickedButton() == yesBtn) {
        return Yes;
    }
    if (msgBox.clickedButton() == noBtn) {
        return No;
    }

    return Exit;
}

MessageBoxHelper::Result MessageBoxHelper::questionFourButtons(QWidget* parent, const QString& title, const QString& text)
{
    QMessageBox msgBox(parent);
    msgBox.setWindowTitle(title);
    msgBox.setText(text);
    msgBox.setIcon(QMessageBox::Question);

    QPushButton* yesBtn = msgBox.addButton("是", QMessageBox::YesRole);
    QPushButton* allBtn = msgBox.addButton("全是", QMessageBox::AcceptRole);
    QPushButton* noBtn = msgBox.addButton("否", QMessageBox::NoRole);
    QPushButton* exitBtn = msgBox.addButton("退出", QMessageBox::RejectRole);

    msgBox.exec();

    if (msgBox.clickedButton() == yesBtn) {
        return Yes;
    }
    if (msgBox.clickedButton() == allBtn) {
        return ALL;
    }
    if (msgBox.clickedButton() == noBtn) {
        return No;
    }
    return Exit;
}