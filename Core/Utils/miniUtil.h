#ifndef MINI_UTIL_H
#define MINI_UTIL_H

#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

/*
 * 最基本的辅助工具，无需额外依赖。
 * 运行环境: UTF-8
 *
 * version: 0.11.0
 *
 * 所有 std::pair 返回值的，统一，第一个值是错误字符串，为空正常。
 */
#if defined(_WIN32) || defined(_WIN64)
#define OS_MINI_WINDOWS
#elif defined(__clang__) && defined(__APPLE__)
#define OS_MINI_MACOS
#else
#define OS_MINI_UNIXLIKE
#endif

// ========================== miniUtil ==========================
using miniErr = std::string;
class miniUtil
{
public:
    miniUtil();
    ~miniUtil();

public:
    static void setU8Output();
    static std::vector<std::string> split(const std::string& str, const std::string& delim);
    static std::pair<miniErr, std::string> U82Ansi(const std::string& str);
    static std::pair<miniErr, std::string> AutoU8(const std::string& str);
    static std::pair<miniErr, std::string> Ansi2U8(const std::string& str);
    static bool IsU8(const std::string& str);
    static std::string Upper(const std::string& str);
    static std::string Lower(const std::string& str);
    static std::string GetSizeInfo(std::uint64_t size);
    static std::string GetTimeInfo(std::uint64_t milliseconds);
};

template <typename T> class miniSingleton
{
public:
    miniSingleton(const miniSingleton&) = delete;
    miniSingleton& operator=(const miniSingleton&) = delete;

    static T& instance()
    {
        // C++11 起：局部静态变量初始化是线程安全的
        static T instance;
        return instance;
    }

private:
    miniSingleton() = default;
    ~miniSingleton() = default;
};

// ========================== miniBuffer ==========================
class miniBuffer
{
public:
    miniBuffer() = default;
    ~miniBuffer() = default;

public:
    void Clear();
    size_t Size() const;

    void RemoveOf(int start, int size);
    void Append(const char* data, size_t size);

    int IndexOf(const char* data, size_t size, int start = 0);
    int IndexOf(const std::string& str, int start = 0);

    const std::vector<char>& GetBuffer() const;

private:
    std::mutex mutex_;
    std::vector<char> buffer_;
};

// ========================== miniPath ==========================
class miniPath
{
public:
    miniPath() = default;
    ~miniPath() = default;

public:
    enum class miniFileType {
        mTypeDir,
        mTypeFile,
    };
    struct miniFileMeta {
        std::string dir;
        miniFileType fileType;
        std::string fileName;
        std::uint64_t fileSize;
        std::uint64_t lastModified;
    };

public:
    static std::pair<miniErr, std::string> GetExePath();
    static std::pair<miniErr, std::string> GetExeDir();
    static std::pair<miniErr, std::string> GetExeName();
    static std::pair<miniErr, std::string> GetHome();
    static std::pair<miniErr, std::string> CreateDir(const std::string& path);

    static bool IsDir(const std::string& path);
    static bool IsFile(const std::string& path);
    static bool IsExist(const std::string& path);
    static bool GetList(const std::string& path, std::vector<miniFileMeta>& fileList);

    static std::string Join(const std::string& path, const std::string& f);
    static std::string Join(const std::string& path, const std::string& f1, const std::string& f2);

    static std::string cdUp(const std::string& path);
    static bool isUnc(const std::string& p);
    static std::string Normalize(const std::string& path);
    static std::string toNative(const std::string& p);
};

#endif   // MINI_UTIL_H
