#include "Common.h"

#include <QCryptographicHash>
#include <QFile>
#include <QRandomGenerator>
#include <QStorageInfo>
#include <QUuid>

#include "miniUtil.h"

QString Common::GetUUID()
{
    QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return uuid;
}

QString Common::GenSha256(const QString& str, bool isFile)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);

    if (isFile) {
        QFile file(str);
        if (!file.open(QIODevice::ReadOnly)) {
            return QString();
        }

        if (!hash.addData(&file)) {
            return QString();
        }
    } else {
        hash.addData(str.toUtf8());
    }
    return QString(hash.result().toHex());
}

QVector<QString> Common::GetLocalDrivers()
{
    QVector<QString> drivers;
    auto mountedVolumes = QStorageInfo::mountedVolumes();
    for (const auto& driver : mountedVolumes) {
        if (driver.isValid() && driver.isReady()) {
            drivers.push_back(driver.rootPath());
        }
    }
    return drivers;
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

void BaseConfig::genPath()
{
    configDir_ = miniPath::Join(miniPath::GetHome().second, ".config", "relayFile");
    if (!miniPath::IsExist(configDir_)) {
        miniPath::CreateDir(configDir_);
    }
    // GlobalData::getInstance()->setGlobalConfigDir(QString::fromStdString(configDir));
    baseConfigPath_ = QString::fromStdString(miniPath::Join(configDir_, "relayFile"));
}

QString BaseConfig::getCurrentName()
{
    QMutexLocker locker(&mutex_);

    nlohmann::json j = loadJson();

    if (j.contains("name") && !j["name"].is_null()) {
        return QString::fromStdString(j["name"].get<std::string>());
    }

    QString newName = generateRandomName();
    j["name"] = newName.toStdString();

    if (!saveJson(j)) {
        qWarning() << "Failed to save config with new name";
    }
    return newName;
}

bool BaseConfig::getIpHistory(IpHistory& out)
{
    QMutexLocker locker(&mutex_);

    nlohmann::json j = loadJson();
    if (!j.contains("ipHistory") || j["ipHistory"].is_null()) {
        return false;
    }

    try {
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

    constexpr size_t MaxHistory = 10;
    nlohmann::json j = loadJson();

    IpHistory history;
    if (j.contains("ipHistory") && !j["ipHistory"].is_null()) {
        history = j["ipHistory"].get<IpHistory>();
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

    if (history.history.size() > MaxHistory) {
        history.history.erase(history.history.begin(), history.history.end() - MaxHistory);
    }
    j["ipHistory"] = history;
    return saveJson(j);
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

    nlohmann::json j = loadJson();

    if (j.contains("Width") && j.contains("Height") && j["Width"].is_number() && j["Height"].is_number()) {
        width = j["Width"].get<int>();
        height = j["Height"].get<int>();
    }

    return {width, height};
}

bool BaseConfig::saveWidthHeight(int width, int height)
{
    QMutexLocker locker(&mutex_);
    nlohmann::json j = loadJson();
    j["Width"] = width;
    j["Height"] = height;
    return saveJson(j);
}

BaseConfig::BaseConfig()
{
    genPath();
}

bool BaseConfig::saveJson(const nlohmann::json& j)
{
    QFile file(baseConfigPath_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        qWarning() << "Cannot open config file for writing";
        return false;
    }

    QByteArray data(QString::fromStdString(j.dump(4)).toUtf8());
    if (file.write(data) == -1) {
        qWarning() << "Failed to write config file";
        return false;
    }

    return true;
}

nlohmann::json BaseConfig::loadJson()
{
    QFile file(baseConfigPath_);
    nlohmann::json j;
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        try {
            QByteArray data = file.readAll();
            if (!data.isEmpty()) {
                j = nlohmann::json::parse(data.toStdString());
            }
        } catch (const nlohmann::json::exception& e) {
            qWarning() << "Json parse error:" << e.what();
            j = nlohmann::json::object();
        }
    }
    return j;
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