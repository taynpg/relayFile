#include "MessageBoxHelper.h"

#include <QInputDialog>
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

bool MessageBoxHelper::questionYesNo(QWidget* parent, const QString& title, const QString& text)
{
    QMessageBox msgBox(parent);
    msgBox.setWindowTitle(title);
    msgBox.setText(text);
    msgBox.setIcon(QMessageBox::Question);

    QPushButton* yesBtn = msgBox.addButton("是", QMessageBox::YesRole);
    QPushButton* noBtn = msgBox.addButton("否", QMessageBox::NoRole);

    msgBox.exec();
    return msgBox.clickedButton() == yesBtn;
}

bool MessageBoxHelper::getTextInput(QWidget* parent, const QString& title, const QString& label, QString& outText,
                                    const QString& defaultValue)
{
    QInputDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setLabelText(label);
    dialog.setOkButtonText("确定");
    dialog.setCancelButtonText("取消");
    dialog.setTextValue(defaultValue);

    auto size = dialog.minimumSizeHint();
    size.setWidth(size.width() + 200);
    dialog.setFixedSize(size);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    outText = dialog.textValue().trimmed();
    return !outText.isEmpty();
}

void MessageBoxHelper::information(QWidget* parent, const QString& title, const QString& text)
{
    QMessageBox msgBox(parent);
    msgBox.setWindowTitle(title);
    msgBox.setText(text);
    msgBox.setIcon(QMessageBox::Information);
    msgBox.exec();
}
