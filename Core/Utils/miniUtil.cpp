#include "miniUtil.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

#ifdef OS_MINI_WINDOWS
#include <Windows.h>
static HANDLE hOut = nullptr;
#elif defined(OS_MINI_UNIXLIKE)
#include <limits.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

#ifdef OS_MINI_WINDOWS
class Utf8ConsoleBuf : public std::streambuf
{
protected:
    int overflow(int c = EOF) override
    {
        if (c != EOF) {
            char ch = static_cast<char>(c);
            xsputn(&ch, 1);
        }
        return c;
    }

    std::streamsize xsputn(const char* s, std::streamsize n) override
    {
        int wideLen = MultiByteToWideChar(CP_UTF8, 0, s, static_cast<int>(n), nullptr, 0);
        if (wideLen <= 0) {
            return 0;
        }

        std::vector<wchar_t> buffer(wideLen);
        MultiByteToWideChar(CP_UTF8, 0, s, static_cast<int>(n), buffer.data(), wideLen);

        if (hOut == nullptr) {
            hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        }

        DWORD written;
        WriteConsoleW(hOut, buffer.data(), wideLen, &written, nullptr);

        return n;
    }
};

std::string W2U8(const std::wstring& wstr)
{
    if (wstr.empty()) {
        return {};
    }

    int utf8Size = ::WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (utf8Size == 0) {
        return {};
    }

    std::string result(utf8Size, 0);

    int converted =
        ::WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), &result[0], utf8Size, nullptr, nullptr);

    if (converted == 0) {
        return {};
    }

    result.resize(converted);
    return result;
}

std::wstring U8ToW(const std::string& str)
{
    if (str.empty()) {
        return {};
    }
    int wideSize = ::MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    if (wideSize == 0) {
        return {};
    }
    std::wstring result(wideSize, 0);
    int converted = ::MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), &result[0], wideSize);

    if (converted == 0) {
        return {};
    }
    result.resize(converted);
    return result;
}

#endif

miniUtil::miniUtil()
{
}

miniUtil::~miniUtil()
{
}

std::string miniUtil::GetSizeInfo(std::uint64_t size)
{
    if (size < 1024) {
        return std::to_string(size) + " B";
    }
    if (size < 1024 * 1024) {
        return std::to_string(size / 1024) + " KB";
    }
    if (size < 1024 * 1024 * 1024) {
        return std::to_string(size / 1024 / 1024) + " MB";
    }
    return std::to_string(size / 1024 / 1024 / 1024) + " GB";
}

std::string miniUtil::GetTimeInfo(std::uint64_t milliseconds)
{
    constexpr std::uint64_t MS_PER_SEC = 1000;
    constexpr std::uint64_t MS_PER_MIN = 60 * MS_PER_SEC;
    constexpr std::uint64_t MS_PER_HOUR = 60 * MS_PER_MIN;

    std::uint64_t hours = milliseconds / MS_PER_HOUR;
    std::uint64_t remain = milliseconds % MS_PER_HOUR;

    std::uint64_t minutes = remain / MS_PER_MIN;
    remain %= MS_PER_MIN;

    std::uint64_t seconds = remain / MS_PER_SEC;
    std::uint64_t ms = remain % MS_PER_SEC;

    if (hours > 0) {
        return std::to_string(hours) + "时" + std::to_string(minutes) + "分";
    }

    if (minutes > 0) {
        return std::to_string(minutes) + "分" + std::to_string(seconds) + "秒";
    }

    if (seconds > 0) {
        return std::to_string(seconds) + "秒" + std::to_string(ms) + "毫秒";
    }

    return std::to_string(ms) + "毫秒";
}

std::string miniUtil::Upper(const std::string& str)
{
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::toupper(c); });
    return result;
}
std::string miniUtil::Lower(const std::string& str)
{
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
    return result;
}

void miniUtil::setU8Output()
{
#ifdef OS_MINI_WINDOWS
    static Utf8ConsoleBuf buf;
    std::cout.rdbuf(&buf);
#else
#endif
}

std::vector<std::string> miniUtil::split(const std::string& str, const std::string& delim)
{
    std::vector<std::string> result;

    if (delim.empty()) {
        result.push_back(str);
        return result;
    }

    std::string::size_type start = 0;
    std::string::size_type pos;

    while ((pos = str.find(delim, start)) != std::string::npos) {
        result.emplace_back(str, start, pos - start);
        start = pos + delim.size();
    }

    result.emplace_back(str, start, str.size() - start);
    return result;
}

std::pair<miniErr, std::string> miniUtil::U82Ansi(const std::string& str)
{
#ifdef OS_MINI_WINDOWS
    int wideCharLen = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (wideCharLen <= 0) {
        return {"Error1", ""};
    }
    std::wstring wideStr(wideCharLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wideStr[0], wideCharLen);
    int acpLen = WideCharToMultiByte(CP_ACP, 0, wideStr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (acpLen <= 0) {
        return {"Error2", ""};
    }
    std::string acpStr(acpLen, '\0');
    WideCharToMultiByte(CP_ACP, 0, wideStr.c_str(), -1, &acpStr[0], acpLen, nullptr, nullptr);

    acpStr.resize(acpLen - 1);
    return {"", acpStr};
#else
    return {"", str};
#endif
}

std::pair<miniErr, std::string> miniUtil::AutoU8(const std::string& str)
{
    if (!IsU8(str)) {
        return Ansi2U8(str);
    }
    return {"", str};
}

std::pair<miniErr, std::string> miniUtil::Ansi2U8(const std::string& str)
{
#ifdef OS_MINI_WINDOWS
    int wideCharLen = MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, nullptr, 0);
    if (wideCharLen <= 0) {
        return {"Error1", ""};
    }
    std::wstring wideStr(wideCharLen, L'\0');
    MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, &wideStr[0], wideCharLen);

    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0) {
        return {"Error2", ""};
    }
    std::string utf8Str(utf8Len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), -1, &utf8Str[0], utf8Len, nullptr, nullptr);

    utf8Str.resize(utf8Len - 1);
    return {"", utf8Str};
#else
    return {"", str};
#endif
}

bool miniUtil::IsU8(const std::string& str)
{
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(str.data());
    size_t len = str.size();

    for (size_t i = 0; i < len;) {
        uint8_t b = bytes[i];

        // 1-byte: 0xxxxxxx
        if ((b & 0x80) == 0x00) {
            ++i;
            continue;
        }
        // 2-byte: 110xxxxx 10xxxxxx
        else if ((b & 0xE0) == 0xC0) {
            if (i + 1 >= len) {
                return false;
            }
            if ((bytes[i + 1] & 0xC0) != 0x80) {
                return false;
            }
            i += 2;
        }
        // 3-byte: 1110xxxx 10xxxxxx 10xxxxxx
        else if ((b & 0xF0) == 0xE0) {
            if (i + 2 >= len) {
                return false;
            }
            if ((bytes[i + 1] & 0xC0) != 0x80) {
                return false;
            }
            if ((bytes[i + 2] & 0xC0) != 0x80) {
                return false;
            }
            if (b == 0xE0 && bytes[i + 1] < 0xA0) {
                return false;
            }
            i += 3;
        }
        // 4-byte: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
        else if ((b & 0xF8) == 0xF0) {
            if (i + 3 >= len) {
                return false;
            }
            if ((bytes[i + 1] & 0xC0) != 0x80) {
                return false;
            }
            if ((bytes[i + 2] & 0xC0) != 0x80) {
                return false;
            }
            if ((bytes[i + 3] & 0xC0) != 0x80) {
                return false;
            }
            if (b == 0xF0 && bytes[i + 1] < 0x90) {
                return false;
            }
            i += 4;
        } else {
            return false;
        }
    }

    return true;
}

void miniBuffer::Clear()
{
    std::unique_lock<std::mutex> lock(mutex_);
    buffer_.clear();
}

size_t miniBuffer::Size() const
{
    return buffer_.size();
}

void miniBuffer::RemoveOf(int start, int size)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (start >= buffer_.size()) {
        return;
    }

    size_t pend = start + size;
    if (pend > buffer_.size()) {
        pend = buffer_.size();
    }
    buffer_.erase(buffer_.begin() + start, buffer_.begin() + pend);
}

void miniBuffer::Append(const char* data, size_t size)
{
    std::unique_lock<std::mutex> lock(mutex_);
    std::copy_n(data, size, std::back_inserter(buffer_));
}

int miniBuffer::IndexOf(const char* data, size_t size, int start)
{
    std::unique_lock<std::mutex> lock(mutex_);
    auto it = std::search(buffer_.begin() + start, buffer_.end(), data, data + size);
    if (it == buffer_.end()) {
        return -1;
    }
    auto dis = std::distance(buffer_.begin(), it);
    return static_cast<int>(dis);
}

int miniBuffer::IndexOf(const std::string& str, int start)
{
    return IndexOf(str.c_str(), str.size(), start);
}

const std::vector<char>& miniBuffer::GetBuffer() const
{
    return buffer_;
}

std::pair<miniErr, std::string> miniPath::GetExePath()
{
#if defined(OS_MINI_WINDOWS)
    wchar_t exePath[MAX_PATH] = {0};
    DWORD length = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (length == 0) {
        return {"Error1", ""};
    }
    return {"", W2U8(exePath)};
#elif defined(OS_MINI_MACOS)
    uint32_t size{};
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    auto r = _NSGetExecutablePath(buffer.data(), &size);
    if (r != 0) {
        return {"Error1", ""};
    }
    return {"", buffer.data()};
#else
    char exePath[PATH_MAX] = {0};
    ssize_t length = readlink("/proc/self/exe", exePath, PATH_MAX - 1);
    if (length == -1) {
        return {"Error1", ""};
    }
    exePath[length] = '\0';
    return {"", exePath};
#endif
}

std::pair<miniErr, std::string> miniPath::GetExeDir()
{
    auto r = GetExePath();
    if (!r.first.empty()) {
        return r;
    }
    fs::path p(r.second);
    return {"", p.parent_path().string()};
}

std::pair<miniErr, std::string> miniPath::GetExeName()
{
    auto r = GetExePath();
    if (!r.first.empty()) {
        return r;
    }
    fs::path p(r.second);
    return {"", p.filename().string()};
}

std::pair<miniErr, std::string> miniPath::GetHome()
{
#if defined(OS_MINI_WINDOWS)
    std::wstring val;
    DWORD size = GetEnvironmentVariableW(L"USERPROFILE", nullptr, 0);
    if (size == 0) {
        return {"Error1", ""};
    }
    val.resize(size);
    if (GetEnvironmentVariableW(L"USERPROFILE", &val[0], size) == 0) {
        return {"Error2", ""};
    }
    val.resize(size - 1);
    return {"", W2U8(val)};
#else
    char* homedir = getenv("HOME");
    if (homedir == nullptr) {
        return {"Error1", ""};
    }
    return {"", homedir};
#endif
}

std::string miniPath::Join(const std::string& path, const std::string& f)
{
    fs::path p(path);
    p.append(f);
    p = p.lexically_normal();
    return p.string();
}

std::string miniPath::Join(const std::string& path, const std::string& f1, const std::string& f2)
{
    fs::path p(path);
    p.append(f1);
    p.append(f2);
    p = p.lexically_normal();
    return p.string();
}

std::pair<miniErr, std::string> miniPath::CreateDir(const std::string& path)
{
    try {
        fs::create_directories(path);
    } catch (const fs::filesystem_error& e) {
        return {"Error1", e.what()};
    }
    return {"", path};
}

bool miniPath::IsDir(const std::string& path)
{
    return fs::is_directory(path);
}

bool miniPath::IsFile(const std::string& path)
{
    return fs::is_regular_file(path);
}

bool miniPath::IsExist(const std::string& path)
{
    return fs::exists(path);
}

bool miniPath::GetList(const std::string& path, std::vector<miniFileMeta>& fileList)
{
    if (!IsDir(path) || !IsExist(path)) {
        return false;
    }
    fileList.clear();

    auto GetLastModified = [](const std::string& path) -> uint64_t {
        auto ftime = fs::last_write_time(path);
        // C++17: filesystem clock → system_clock
        auto sctime = std::chrono::system_clock::now() + (ftime - fs::file_time_type::clock::now());
        return std::chrono::duration_cast<std::chrono::milliseconds>(sctime.time_since_epoch()).count();
    };

    try {
        for (const auto& entry : fs::directory_iterator(path)) {

            auto filename = entry.path().filename().string();
            if (filename == "." || filename == "..") {
                continue;
            }

#if defined(OS_MINI_WINDOWS)
            DWORD attrs = GetFileAttributesW(U8ToW(entry.path().string()).c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))) {
                continue;
            }
#else
#endif

            if (filename.find(">") != std::string::npos || filename.find("$") != std::string::npos) {
                continue;
            }

            miniFileMeta meta;
            meta.dir = path;
            if (entry.is_regular_file()) {
                meta.fileType = miniFileType::mTypeFile;
                meta.fileName = entry.path().filename().string();
                meta.fileSize = fs::file_size(entry.path());
                meta.lastModified = GetLastModified(entry.path().string());
                fileList.push_back(meta);
            } else if (entry.is_directory()) {
                meta.fileType = miniFileType::mTypeDir;
                meta.fileName = entry.path().filename().string();
                meta.fileSize = 0;
                meta.lastModified = GetLastModified(entry.path().string());
                fileList.push_back(meta);
            }
        }
    } catch (const fs::filesystem_error& e) {
        return false;
    }
    return true;
}

std::string miniPath::cdUp(const std::string& path)
{
    fs::path p(path);
    p = p.parent_path();
    p = p.lexically_normal();
    return p.string();
}
