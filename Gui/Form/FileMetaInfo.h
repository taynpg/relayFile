#ifndef FILEMETAINFO_H
#define FILEMETAINFO_H

#include <QDialog>
#include <Protocol/FileMeta.h>

namespace Ui {
class FileMetaInfo;
}

class FileMetaInfo : public QDialog
{
    Q_OBJECT

public:
    explicit FileMetaInfo(QWidget* parent = nullptr);
    ~FileMetaInfo();

public:
    void setMeta(const FileMeta& meta);

protected:
    void showEvent(QShowEvent* event) override;

private:
    FileMeta meta_;
    Ui::FileMetaInfo* ui;
};

#endif   // FILEMETAINFO_H
