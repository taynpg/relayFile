#pragma once

#include <cstdint>

constexpr int defWaitCmdTimeout = 30000;
constexpr int defClearWorkerTimeout = 5000;
constexpr int defSendTimeout = 5000;
constexpr int defRecvTimeout = 15000;
constexpr int defConsoleMessageStart = 0;
constexpr int defFileMessageStart = 500;
constexpr int defServerDirectFileStart = 550;
constexpr int defDirectChuckAck = 600;

// 文件传输优化：块大小与滑动窗口（在途块数）
constexpr std::uint64_t defBlockSize = 256 * 1024;
constexpr std::uint64_t defWindowSize = 32;
constexpr int defRetransLimit = 3;

// 覆盖前内容粗判：均匀抽取的采样点数与每点读取大小
constexpr int defSampleCount = 10;
constexpr std::uint64_t defSampleBlockSize = 4 * 1024;
