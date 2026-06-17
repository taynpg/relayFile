#pragma once

#include <QMessageBox>

class MessageBoxHelper
{
public:
    enum Result {
        Yes = QDialog::Accepted + 1,
        No,
        ALL,
        Exit
    };

    static Result questionThreeButtons(QWidget* parent, const QString& title, const QString& text);
    static Result questionFourButtons(QWidget* parent, const QString& title, const QString& text);
    static bool questionYesNo(QWidget* parent, const QString& title, const QString& text);
    static bool getTextInput(QWidget* parent, const QString& title, const QString& label, QString& outText,
                      const QString& defaultValue = QString());
};
