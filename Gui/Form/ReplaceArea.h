#ifndef REPLACEAREA_H
#define REPLACEAREA_H

#include <QDialog>

namespace Ui {
class ReplaceArea;
}

struct ReplcaeResult {
    std::vector<int> areaIndexs;
    bool useCase;
    bool useRegex;
    bool useReplace{false};
};

class ReplaceArea : public QDialog
{
    Q_OBJECT

public:
    explicit ReplaceArea(QWidget* parent = nullptr);
    ~ReplaceArea();

public:
    std::shared_ptr<ReplcaeResult> getResult() const;

protected:
    void onOk();
    void onCancel();

private:
    void InitUi();

private:
    Ui::ReplaceArea* ui;
    std::shared_ptr<ReplcaeResult> result_;
};

#endif   // REPLACEAREA_H
