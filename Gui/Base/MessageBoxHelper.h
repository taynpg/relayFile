#pragma once

#include <QMessageBox>

class MessageBoxHelper
{
public:
    enum Result {
        Yes = QDialog::Accepted + 1,
        No,
        ALL,
        ALL_NO,
        Exit,
        NotAnswer
    };

    static Result questionThreeButtons(QWidget* parent, const QString& title, const QString& text);
    static Result questionFiveButtons(QWidget* parent, const QString& title, const QString& text);
    static bool questionYesNo(QWidget* parent, const QString& title, const QString& text);
    static bool getTextInput(QWidget* parent, const QString& title, const QString& label, QString& outText,
                             const QString& defaultValue = QString());
    static void information(QWidget* parent, const QString& title, const QString& text);
};
