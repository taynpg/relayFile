#include "BaseHelper.h"

#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QRandomGenerator>
#include <QScreen>
#include <QString>
#include <QUuid>
#include <Utils/miniUtil.h>
#include <nlohmann/json.hpp>

GlobalData* GlobalData::getInstance()
{
    static GlobalData instance;
    return &instance;
}

void GlobalData::setControlSession(std::shared_ptr<ControlSession> controlSession)
{
    std::lock_guard<std::mutex> lock(mutex_);
    controlSession_ = controlSession;
}

void GlobalData::setFileSession(std::shared_ptr<FileSession> fileSession)
{
    std::lock_guard<std::mutex> lock(mutex_);
    fileSession_ = fileSession;
}

void GlobalData::setDoubleLinker(std::shared_ptr<DoubleLinker> doubleLinker)
{
    std::lock_guard<std::mutex> lock(mutex_);
    doubleLinker_ = doubleLinker;
}

std::shared_ptr<DoubleLinker> GlobalData::getDoubleLinker()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return doubleLinker_;
}

std::shared_ptr<ControlSession> GlobalData::getControlSession()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return controlSession_;
}

std::shared_ptr<FileSession> GlobalData::getFileSession()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return fileSession_;
}

QString GlobalData::getGlobalConfigPath()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return globalConfigPath_;
}

void GlobalData::setGlobalConfigPath(const QString& globalConfigPath)
{
    std::lock_guard<std::mutex> lock(mutex_);
    globalConfigPath_ = globalConfigPath;
}

void GlobalData::setAskDfLocal(std::shared_ptr<BaseAskDF> askDfLocal)
{
    std::lock_guard<std::mutex> lock(mutex_);
    askDfLocal_ = askDfLocal;
}

std::shared_ptr<BaseAskDF> GlobalData::getAskDfLocal()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return askDfLocal_;
}

void GlobalData::setAskDfRemote(std::shared_ptr<BaseAskDF> askDfRemote)
{
    std::lock_guard<std::mutex> lock(mutex_);
    askDfRemote_ = askDfRemote;
}

std::shared_ptr<BaseAskDF> GlobalData::getAskDfRemote()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return askDfRemote_;
}

void GlobalData::setBaseConfig(std::shared_ptr<BaseConfig> baseConfig)
{
    std::lock_guard<std::mutex> lock(mutex_);
    baseConfig_ = baseConfig;
}

std::shared_ptr<BaseConfig> GlobalData::getBaseConfig()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return baseConfig_;
}

void BaseConfig::genPath()
{
    auto configDir = miniPath::Join(miniPath::GetHome().second, ".config", "relayFile");
    if (!miniPath::IsExist(configDir)) {
        miniPath::CreateDir(configDir);
    }
    baseConfigPath_ = QString::fromStdString(miniPath::Join(configDir, "relayFile"));
}

QString BaseConfig::getCurrentName()
{
    QMutexLocker locker(&mutex_);
    QFile config(baseConfigPath_);
    if (!config.exists()) {
        auto newName = generateRandomName();
        nlohmann::json json;
        json["name"] = newName.toStdString();
        config.write(json.dump().c_str());
        if (config.open(QIODevice::ReadWrite)) {
            config.write(json.dump().c_str());
            config.close();
        }
        return newName;
    }
    if (!config.open(QIODevice::ReadOnly)) {
        return QString();
    }
    QString jsonStr = config.readAll();
    nlohmann::json json = nlohmann::json::parse(jsonStr.toStdString());
    auto name = json["name"].get<std::string>();
    config.close();
    return QString::fromStdString(name);
}

QString BaseConfig::generateRandomName()
{
    static const QStringList adjectives = {
        "高兴的", "快乐的", "开心的",   "兴奋的",     "愉快的",   "幸福的",   "满足的",   "喜悦的",   "雀跃的",     "轻快的",
        "难过的", "悲伤的", "沮丧的",   "痛苦的",     "失落的",   "愤怒的",   "生气的",   "暴躁的",   "抓狂的",     "郁闷的",
        "紧张的", "焦虑的", "担心的",   "害怕的",     "惊慌的",   "平静的",   "安详的",   "放松的",   "淡定的",     "从容的",
        "害羞的", "腼腆的", "拘谨的",   "内向的",     "安静的",   "聪明的",   "机智的",   "伶俐的",   "敏锐的",     "灵活的",
        "笨拙的", "迟钝的", "呆萌的",   "迷糊的",     "糊涂的",   "勤劳的",   "努力的",   "勤奋的",   "刻苦的",     "忙碌的",
        "懒惰的", "拖沓的", "慢吞吞的", "悠闲的",     "慵懒的",   "勇敢的",   "无畏的",   "坚强的",   "刚毅的",     "果断的",
        "胆小的", "怯懦的", "谨慎的",   "小心的",     "保守的",   "善良的",   "温柔的",   "体贴的",   "暖心的",     "慈祥的",
        "冷漠的", "无情的", "冷酷的",   "冰冷的",     "疏远的",   "忠诚的",   "可靠的",   "踏实的",   "负责的",     "诚实的",
        "狡猾的", "奸诈的", "滑头的",   "精明的",     "算计的",   "神秘的",   "诡异的",   "奇妙的",   "不可思议的", "梦幻的",
        "优雅的", "高贵的", "端庄的",   "体面的",     "精致的",   "朴实的",   "简单的",   "平凡的",   "自然的",     "原始的",
        "华丽的", "炫酷的", "闪亮的",   "耀眼的",     "夺目的",   "古老的",   "陈旧的",   "沧桑的",   "历史的",     "厚重的",
        "未来的", "科技的", "机械的",   "电子的",     "智能的",   "疯狂的",   "躁动的",   "激烈的",   "热血的",     "冲动的",
        "温柔的", "柔软的", "细腻的",   "轻盈的",     "飘逸的",   "沉重的",   "压抑的",   "巨大的",   "庞大的",     "宏伟的",
        "可爱的", "萌萌的", "软萌的",   "奶凶奶凶的", "圆滚滚的", "毛茸茸的", "胖乎乎的", "小不点的", "甜甜的",     "香香的",
        "脆脆的", "糯糯的", "傻乎乎的", "呆呆的",     "愣愣的",   "慢半拍的"};

    static const QStringList nouns = {
        "小猫",   "小狗",   "小兔",   "小熊",   "小狐狸", "小狼",   "小老虎", "小狮子", "小豹子", "小熊猫", "小考拉", "小袋鼠",
        "小企鹅", "小海豚", "小鲸鱼", "小海狮", "小海龟", "小螃蟹", "小虾米", "小刺猬", "小松鼠", "小仓鼠", "小老鼠", "小浣熊",
        "小鹿",   "小马",   "小羊",   "小牛",   "小猪",   "小鸡",   "小鸭",   "小鹅",   "小鸟",   "小燕子", "小鹰",   "小鹦鹉",
        "小鸽子", "小乌鸦", "小喜鹊", "小蛇",   "小蜥蜴", "小壁虎", "小青蛙", "小蟾蜍", "小蝴蝶", "小蜜蜂", "小蜻蜓", "小瓢虫",
        "小蚂蚁", "老虎",   "狮子",   "猎豹",   "黑豹",   "灰狼",   "北极熊", "棕熊",   "巨鳄",   "鲨鱼",   "鲸鱼",   "老鹰",
        "秃鹫",   "猫头鹰", "大雁",   "信天翁", "大象",   "犀牛",   "河马",   "长颈鹿", "野牛",   "骑士",   "法师",   "巫师",
        "战士",   "刺客",   "弓箭手", "枪手",   "医生",   "护士",   "老师",   "学生",   "画家",   "作家",   "歌手",   "舞者",
        "厨师",   "裁缝",   "铁匠",   "商人",   "旅人",   "国王",   "女王",   "王子",   "公主",   "侍卫",   "天使",   "恶魔",
        "精灵",   "矮人",   "兽人",   "龙",     "凤凰",   "麒麟",   "独角兽", "神兽",   "幽灵",   "僵尸",   "骷髅",   "木乃伊",
        "吸血鬼", "星星",   "月亮",   "太阳",   "云朵",   "雨滴",   "雪花",   "风儿",   "雷电",   "彩虹",   "露珠",   "石头",
        "木头",   "火焰",   "流水",   "泥土",   "花朵",   "小草",   "树叶",   "果实",   "种子",   "书本",   "铅笔",   "蜡烛",
        "灯笼",   "镜子",   "钟表",   "钥匙",   "宝箱",   "皇冠",   "戒指"};

    auto* rg = QRandomGenerator::global();

    const QString& adj = adjectives[rg->bounded(adjectives.size())];
    const QString& noun = nouns[rg->bounded(nouns.size())];

    return adj + noun;
}

void from_json(const nlohmann::json& j, IpHistory& h)
{
    j.at("history").get_to(h.history);
    j.at("current").get_to(h.current);
}

void to_json(nlohmann::json& j, const IpHistory& h)
{
    j = nlohmann::json{{"history", h.history}, {"current", h.current}};
}

bool BaseConfig::getIpHistory(IpHistory& out)
{
    QMutexLocker locker(&mutex_);

    QFile file(baseConfigPath_);
    if (!file.exists()) {
        return false;
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    try {
        nlohmann::json j = nlohmann::json::parse(data.toStdString());
        if (!j.contains("ipHistory") || j["ipHistory"].is_null()) {
            return false;
        }
        out = j["ipHistory"].get<IpHistory>();
        return true;
    } catch (const nlohmann::json::exception& e) {
        qWarning() << "Json parse error:" << e.what();
        return false;
    }
}

bool BaseConfig::pushOneIp(const std::string& ip)
{
    QMutexLocker locker(&mutex_);

    QFile file(baseConfigPath_);
    if (!file.open(QIODevice::ReadWrite | QIODevice::Text)) {
        return false;
    }

    nlohmann::json j;
    IpHistory history;
    QByteArray data = file.readAll();

    try {
        if (!data.isEmpty()) {
            j = nlohmann::json::parse(data.toStdString());
        }
        if (j.contains("ipHistory") && !j["ipHistory"].is_null()) {
            history = j["ipHistory"].get<IpHistory>();
        }
    } catch (const nlohmann::json::exception& e) {
        qWarning() << "Json parse error:" << e.what();
        file.close();
        return false;
    }

    if (!history.current.empty()) {
        history.history.push_back(history.current);
    }

    history.current = ip;
    history.history.push_back(ip);

    std::unordered_set<std::string> seen;
    std::vector<std::string> uniqueHistory;

    for (auto it = history.history.rbegin(); it != history.history.rend(); ++it) {
        if (seen.insert(*it).second) {
            uniqueHistory.push_back(*it);
        }
    }
    std::reverse(uniqueHistory.begin(), uniqueHistory.end());
    history.history = std::move(uniqueHistory);

    constexpr size_t MaxHistory = 10;
    if (history.history.size() > MaxHistory) {
        history.history.erase(history.history.begin(), history.history.end() - MaxHistory);
    }

    j["ipHistory"] = history;

    file.resize(0);
    file.write(QByteArray(j.dump(4).c_str()));
    file.close();

    return true;
}

std::pair<int, int> BaseConfig::getWidthHeight()
{
    QMutexLocker locker(&mutex_);

    constexpr int MinWidth = 100;
    constexpr int MinHeight = 100;
    constexpr double DefaultScale = 0.7;
    constexpr double MaxScale = 0.9;

    int width = 0;
    int height = 0;

    QFile file(baseConfigPath_);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QByteArray data = file.readAll();
        if (!data.isEmpty()) {
            try {
                auto j = nlohmann::json::parse(data.toStdString());

                if (j.contains("Width") && j.contains("Height") &&
                    j["Width"].is_number() && j["Height"].is_number())
                {
                    width = j["Width"].get<int>();
                    height = j["Height"].get<int>();
                }
            } catch (const nlohmann::json::exception& e) {
                qWarning() << "Json parse error:" << e.what();
            }
        }
    }

    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        return {MinWidth, MinHeight};
    }

    const QRect geometry = screen->availableGeometry();
    const int maxWidth  = static_cast<int>(geometry.width() * MaxScale);
    const int maxHeight = static_cast<int>(geometry.height() * MaxScale);

    if (width <= 0 || height <= 0) {
        width  = static_cast<int>(geometry.width() * DefaultScale);
        height = static_cast<int>(geometry.height() * DefaultScale);
    }

    width  = qBound(MinWidth, width, maxWidth);
    height = qBound(MinHeight, height, maxHeight);

    return {width, height};
}

bool BaseConfig::saveWidthHeight(int width, int height)
{
    QMutexLocker locker(&mutex_);

    QFile file(baseConfigPath_);
    nlohmann::json j;

    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QByteArray data = file.readAll();
        try {
            if (!data.isEmpty()) {
                j = nlohmann::json::parse(data.toStdString());
            }
        } catch (const nlohmann::json::exception& e) {
            qWarning() << "Json parse error when loading config:" << e.what();
            j.clear();
        }
        file.close();
    }

    j["Width"] = width;
    j["Height"] = height;

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        qWarning() << "Cannot open config file for writing:" << baseConfigPath_;
        return false;
    }

    QByteArray jsonData(QString::fromStdString(j.dump(4)).toUtf8());
    if (file.write(jsonData) == -1) {
        qWarning() << "Failed to write config file";
        file.close();
        return false;
    }

    file.close();
    return true;
}

BaseConfig::BaseConfig()
{
    genPath();
}