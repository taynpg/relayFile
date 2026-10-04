#include "AsioServer.h"

#include <Utils/miniUtil.h>
#include <asio.hpp>
#include <algorithm>
#include <csignal>
#include <cstring>
#include <iostream>
#include <relayFileVersion.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#include <DbgHelp.h>
#include <filesystem>

// 对应 DumpHelper::registerDumpSave 的 Qt-free 版本
static LONG WINAPI AloneExceptionFilter(EXCEPTION_POINTERS* pExceptionPointers)
{
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    const std::filesystem::path exe(exePath);
    const auto dumpDir = exe.parent_path() / "dump";
    std::error_code ec;
    std::filesystem::create_directories(dumpDir, ec);

    SYSTEMTIME st{};
    GetLocalTime(&st);

    wchar_t fullPath[MAX_PATH]{};
    swprintf_s(fullPath, _countof(fullPath), L"%s\\%s-%04d%02d%02d-%02d%02d%02d.dmp", dumpDir.wstring().c_str(),
               exe.stem().wstring().c_str(), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    HANDLE hFile = CreateFileW(fullPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return EXCEPTION_EXECUTE_HANDLER;
    }
    MINIDUMP_EXCEPTION_INFORMATION mdei{};
    mdei.ThreadId = GetCurrentThreadId();
    mdei.ExceptionPointers = pExceptionPointers;
    mdei.ClientPointers = FALSE;
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, MiniDumpNormal, &mdei, nullptr, nullptr);
    CloseHandle(hFile);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

namespace {

// 对应 Logger::initSimpleLogger(true)（去掉给 Gui 用的 QtSink）
bool initLogger()
{
    try {
        auto fileSink = std::make_shared<spdlog::sinks::daily_file_sink_mt>("log/relayFileServer.log", 0, 0, 60);
        fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e][%l]: %v");

        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleSink->set_pattern("%Y-%m-%d %H:%M:%S.%e %^[%l]: %v%$");

        auto logger = std::make_shared<spdlog::logger>("relayFileServer", spdlog::sinks_init_list{fileSink, consoleSink});
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::trace);
        spdlog::flush_on(spdlog::level::trace);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "initLogger: " << e.what() << std::endl;
        return false;
    }
}

}   // namespace

int main(int argc, char* argv[])
{
    const std::string versionMsg = std::string(VERSION_GIT_COMMIT) + "-v" + VERSION_NUM + "-" + VERSION_DEV;
    if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
        std::cout << versionMsg << std::endl;
        return 0;
    }

    // 注意：会替换 std::cout 的 rdbuf（走 WriteConsoleW），管道捕获不到输出，故放在 --version 之后
    miniUtil::setU8Output();

    if (!initLogger()) {
        return 1;
    }

    SPDLOG_INFO("============================================");
    SPDLOG_INFO("Version: {}", versionMsg);
    SPDLOG_INFO("============================================");

#ifdef _WIN32
    SetUnhandledExceptionFilter(AloneExceptionFilter);
#endif

    asio::io_context io;
    AsioServer server(io);

    constexpr std::uint16_t kPort = 9008;
    if (!server.startListen(kPort)) {
        SPDLOG_CRITICAL("relayFileServer启动失败，端口: {}", kPort);
        return 1;
    }

    // 多线程驱动 io_context：不同连接的收发在各自 strand 上并行
    const unsigned int threadCount = std::max(2u, std::thread::hardware_concurrency());
    auto workGuard = asio::make_work_guard(io);
    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    for (unsigned int i = 0; i < threadCount; ++i) {
        workers.emplace_back([&io] { io.run(); });
    }

    // Ctrl+C 优雅退出
    asio::signal_set signals(io, SIGINT, SIGTERM);
    signals.async_wait([&io](const std::error_code&, int) { io.stop(); });

    SPDLOG_INFO("relayFileServer已启动在端口: {}，IO线程数: {}，按Ctrl+C退出。", kPort, threadCount);
    for (auto& t : workers) {
        t.join();
    }
    return 0;
}
