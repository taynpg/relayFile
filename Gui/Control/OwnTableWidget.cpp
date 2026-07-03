#include "OwnTableWidget.h"

#include <File/FileDir.h>
#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QMimeData>

#include "Base/InfoDrop.h"

ComDropTable::ComDropTable(QWidget* parent) : QTableWidget(parent)
{
}

ComDropTable::~ComDropTable()
{
}

void ComDropTable::setItemData(int row, int col, const QString& text, bool isReplace, bool isEditable)
{
    auto* curItem = this->item(row, col);
    if (curItem) {
        if (isReplace) {
            curItem->setText(text);
        }
    } else {
        QTableWidgetItem* item = new QTableWidgetItem(text);
        if (!isEditable) {
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        }
        setItem(row, col, item);
    }
}

void ComDropTable::dragMoveEvent(QDragMoveEvent* event)
{
    auto have = event->mimeData()->hasFormat(MY_MIME_DROP_TYPE);
    if (!have) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

void ComDropTable::dropEvent(QDropEvent* event)
{
    auto mimeData = event->mimeData()->data(MY_MIME_DROP_TYPE);
    InfoDrop infoDrop = infoUnpack<InfoDrop>(mimeData);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QPoint pos = event->position().toPoint();
#else
    QPoint pos = event->pos();
#endif

    int startRow = rowAt(pos.y());
    int startCol = columnAt(pos.x());

    int curRow = startRow;
    if (curRow < 0) {
        curRow = rowCount();
    }
    // headers_ << "ID" << "名称" << "类型" << "标记" << "本地目录" << "远程目录";
    if (startCol != 4 && startCol != 5) {
        return;
    }
    for (int i = 0; i < infoDrop.items.size(); i++, ++curRow) {
        if (curRow >= rowCount()) {
            insertRow(rowCount());
        }
        setItemData(curRow, 0, "", false, false);
        const auto& curItem = infoDrop.items[i];
        if (curItem.type == 0) {
            setItemData(curRow, 1, curItem.fileName, false);
            setItemData(curRow, 2, "Dir", false, false);
            setItemData(curRow, 3, "Dir", false);

        } else {
            setItemData(curRow, 1, curItem.fileName, true);
            setItemData(curRow, 2, "File", true, false);
            setItemData(curRow, 3, getMarkStr(curItem.fileName, curItem.type == 0), true);
        }
        if (startCol == 4) {
            auto pathContent = (curItem.type == 0 ? FileDir::Join(infoDrop.from, curItem.fileName) : infoDrop.from);
            setItemData(curRow, 4, pathContent, true);
            setItemData(curRow, 5, "", false);
        } else if (startCol == 5) {
            auto pathContent = (curItem.type == 0 ? FileDir::Join(infoDrop.from, curItem.fileName) : infoDrop.from);
            setItemData(curRow, 5, pathContent, true);
            setItemData(curRow, 4, "", false);
        }
    }
    event->acceptProposedAction();
}

QString ComDropTable::getMarkStr(const QString& filePath, bool isDir)
{
    if (isDir) {
        return QStringLiteral("Dir");
    }
    QFileInfo fi(filePath);
    QString suffix = fi.suffix();
    return suffix.isEmpty() ? QStringLiteral("File") : suffix.toLower();
}

void ComDropTable::dragEnterEvent(QDragEnterEvent* event)
{
    auto have = event->mimeData()->hasFormat(MY_MIME_DROP_TYPE);
    if (!have) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

// ---------------------------------------------------------------------------

ExpDropTable::ExpDropTable(QWidget* parent) : QTableWidget(parent)
{
}

ExpDropTable::~ExpDropTable()
{
}

void ExpDropTable::dropEvent(QDropEvent* event)
{
    QTableWidget::dropEvent(event);
}

void ExpDropTable::dragEnterEvent(QDragEnterEvent* event)
{
    const QTableWidget* s = qobject_cast<const QTableWidget*>(event->source());
    if (this == s) {
        event->ignore();
        return;
    }
    auto have = event->mimeData()->hasFormat(MY_MIME_DROP_TYPE);
    if (!have) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

void ExpDropTable::dragMoveEvent(QDragMoveEvent* event)
{
    const QTableWidget* s = qobject_cast<const QTableWidget*>(event->source());
    if (this == s) {
        event->ignore();
        return;
    }
    auto have = event->mimeData()->hasFormat(MY_MIME_DROP_TYPE);
    if (!have) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

void ExpDropTable::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        dragStart_ = event->pos();
    }
    QTableWidget::mousePressEvent(event);
}

void ExpDropTable::mouseMoveEvent(QMouseEvent* event)
{
    if (!(event->buttons() & Qt::LeftButton)) {
        return;
    }
    if ((event->pos() - dragStart_).manhattanLength() < QApplication::startDragDistance()) {
        return;
    }
    auto items = selectedItems();
    InfoDrop infoDrop;
    infoDrop.from = getOwnRoot_();
    for (int i = 0; i < items.size() / 5; i++) {
        InfoDropItem infoDropItem;
        infoDropItem.fileName = items[i * 5 + 1]->text();
        infoDropItem.type = items[i * 5 + 3]->text() == "Dir" ? 0 : 1;
        infoDrop.items.push_back(infoDropItem);
    }
    auto packData = infoPack(infoDrop);
    auto* mimeData = new QMimeData();
    mimeData->setData(MY_MIME_DROP_TYPE, packData);

    QDrag* drag = new QDrag(this);
    drag->setMimeData(mimeData);
    drag->exec(Qt::CopyAction | Qt::MoveAction);
}

void ExpDropTable::setGetOwnRoot(std::function<QString()> getOwnRoot)
{
    getOwnRoot_ = getOwnRoot;
}
