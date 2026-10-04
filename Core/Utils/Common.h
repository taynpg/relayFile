#pragma once

#include <QByteArray>
#include <QMutex>
#include <QString>
#include <nlohmann/json.hpp>
#include <string>

struct IpHistory {
    std::vector<std::string> history;
    std::string current;
};

struct RetryCon {
    bool useRecon{false};
    int interval{100};
    int count{1};
};

// 压缩传输配置：开启后多文件/文件夹打包为单个 tar.xz 传输。
struct CompressConfig {
    bool enabled{false};
    int thresholdMB{2048};   // 单包超出此阈值（MB）则放弃本次传输
};

class Common
{
private:
    Common() = default;
    ~Common() = default;

public:
    static QString GetUUID();
    static QString GenSha256(const QString& str, bool isFile);
    static QVector<QString> GetLocalDrivers();
};

class BaseConfig
{
public:
    BaseConfig();
    ~BaseConfig() = default;

public:
    QString getCurrentName();
    QString generateRandomName();
    bool getIpHistory(IpHistory& history);
    bool pushOneIp(const std::string& ip);
    std::pair<int, int> getWidthHeight();
    bool saveWidthHeight(int width, int height);
    // 窗口完整几何（含位置、还原态矩形与最大化/全屏状态），由 QWidget::saveGeometry 生成，
    // 以 base64 文本保存。解决只存宽高时最大化关闭后重开窗口偏移出屏幕的问题。
    QByteArray getWindowGeometry();
    bool saveWindowGeometry(const QByteArray& geometry);
    bool getReconInterval(RetryCon& con);
    bool saveReconInterval(const RetryCon& con);
    bool getCompress(CompressConfig& cfg);
    bool saveCompress(const CompressConfig& cfg);

private:
    bool saveJson(const nlohmann::json& j);
    nlohmann::json loadJson();

private:
    void genPath();

public:
    QMutex mutex_;
    std::string configDir_{};
    QString baseConfigPath_;
};

class ReconHelper
{
public:
    ReconHelper();
    ~ReconHelper() = default;

public:
    void setRetryCon(const RetryCon& con);
    void markNeedRecon(bool needRecon);
    void markConnected(bool isConnected);
    bool shouleReconnct();

private:
    QMutex mutex_;
    int curRetryCount_{0};
    bool needRecon_{false};
    bool isConnected_{false};
    RetryCon retryCon_;
    // 记录当前时间戳
    uint64_t lastReconTime_{0};
};

class DumpHelper
{
public:
    DumpHelper() = default;
    ~DumpHelper() = default;

public:
    static void registerDumpSave(const std::string& dirPath);
};