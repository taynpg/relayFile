#include "FileMetaInfo.h"

#include <File/FileDir.h>
#include <QClipboard>
#include <QDateTime>
#include <Utils/miniUtil.h>

#include "ui_FileMetaInfo.h"

FileMetaInfo::FileMetaInfo(QWidget* parent) : QDialog(parent), ui(new Ui::FileMetaInfo)
{
    ui->setupUi(this);

    ui->pedDir->setReadOnly(true);
    ui->pedName->setReadOnly(true);

    connect(ui->btnClose, &QPushButton::clicked, this, &FileMetaInfo::close);
    connect(ui->btnCopyDirPath, &QPushButton::clicked, this, [this]() {
        QClipboard* clip = QApplication::clipboard();
        clip->setText(ui->pedDir->toPlainText());
    });
    connect(ui->btnCopyFileName, &QPushButton::clicked, this, [this]() {
        QClipboard* clip = QApplication::clipboard();
        clip->setText(ui->pedName->toPlainText());
    });
    connect(ui->btnCopyFull, &QPushButton::clicked, this, [this]() {
        auto d = ui->pedDir->toPlainText();
        auto f = ui->pedName->toPlainText();
        auto r = FileDir::Join(d, f);
        QClipboard* clip = QApplication::clipboard();
        clip->setText(r);
    });

    setWindowTitle("详细信息");
}

FileMetaInfo::~FileMetaInfo()
{
    delete ui;
}

void FileMetaInfo::setMeta(const FileMeta& meta)
{
    meta_ = meta;
}

void FileMetaInfo::showEvent(QShowEvent* event)
{
    Q_UNUSED(event);
    ui->pedDir->setPlainText(QString::fromStdString(meta_.dir));
    ui->pedName->setPlainText(QString::fromStdString(meta_.name));
    ui->lbSize->setText(QString::fromStdString(miniUtil::GetSizeInfo(meta_.size)));

    QDateTime modifyTime = QDateTime::fromMSecsSinceEpoch(meta_.lastModified);
    QString timeStr = modifyTime.toString("yyyy-MM-dd hh:mm:ss");
    ui->lbTime->setText(timeStr);

    ui->lbType->setText(meta_.type == FileType::FILE_TYPE_DIR ? "目录" : "文件");
    QDialog::showEvent(event);
}
