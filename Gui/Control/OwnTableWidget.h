#pragma once

#include <QDialog>
#include <QTableWidget>
#include <functional>

struct InfoDrop;

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
    void setItemData(int row, int col, const QString& text, bool isReplace, bool isEditable = true);
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
    // 从另一个文件浏览表格（本地/远端）放下拖拽项时回调，由所属 ExplorerControl 发起传输
    void setOnInfoDropped(std::function<void(const InfoDrop&)> onDropped);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;

private:
    std::function<QString()> getOwnRoot_;
    std::function<void(const InfoDrop&)> onInfoDropped_;
    QPoint dragStart_;
};