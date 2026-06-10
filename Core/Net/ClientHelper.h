#pragma once

#include <QThread>
#include <QTimer>

#include "ControlSession.h"
#include "FileSession.h"

// 客户端双链接助手
class DoubleLinker : public QObject
{
    Q_OBJECT

public:
    DoubleLinker(QObject* parent = nullptr);
    ~DoubleLinker();

public:
    template <typename HandleResp> bool Request(FramePtr frame, HandleResp handleResp);

public:
    void Quit();
    void SetControlSession(std::shared_ptr<ControlSession> session);
    void SetFileSession(std::shared_ptr<FileSession> session);

    std::shared_ptr<ControlSession> GetControlSession() const;
    std::shared_ptr<FileSession> GetFileSession() const;

public slots:
    void onDeliverControl(FramePtr frame);
    void onDeliverFile(FramePtr frame);

private:
    std::shared_ptr<ControlSession> controlSession_{};
    std::shared_ptr<FileSession> fileSession_{};
};