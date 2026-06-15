#pragma once

#include <QDialog>
#include <QTableWidget>

class ComDropTable : public QTableWidget
{
    Q_OBJECT
public:
    explicit ComDropTable(QWidget* parent = nullptr);
    ~ComDropTable() override;

protected:
    void dropEvent(QDropEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;

private:
    void setItemData(int row, int col, const QString& text, bool isReplace);
    QString getMarkStr(const QString& filePath, bool isDir);
};

class ExpDropTable : public QTableWidget
{
    Q_OBJECT
public:
    explicit ExpDropTable(QWidget* parent = nullptr);
    ~ExpDropTable() override;

public:
    void setGetOwnRoot(std::function<QString()> getOwnRoot);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;

private:
    std::function<QString()> getOwnRoot_;
    QPoint dragStart_;
};