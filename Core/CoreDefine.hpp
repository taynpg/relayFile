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

// 文件传输：块大小与窗口。
// 第二批起窗口按「字节」计（与块大小解耦），块大小由接收方在握手时决定：
// 小文件小块（少浪费）、大文件大块（少事件循环往返）。
constexpr std::uint64_t defBlockSize = 256 * 1024;
constexpr std::uint64_t defMinBlockSize = 64 * 1024;
constexpr std::uint64_t defMaxBlockSize = 4 * 1024 * 1024;
constexpr std::uint64_t defWindowBytes = 4 * 1024 * 1024;
// 累积 ACK：收到这么多字节，或者距上次 ACK 超过 defAckDelayMs，就回一次 ACK
constexpr std::uint64_t defAckBytes = 1024 * 1024;
constexpr int defAckDelayMs = 20;
// 单块重传上限 / 连续无进展的 Complete 重发上限
constexpr int defRetransLimit = 5;
// 单个任务的总兜底超时（传输中途卡死由 OneFileTrans 自己的收发超时负责，
// 这里只防止"整条任务永远不结束"）
constexpr int defTaskTotalTimeout = 6 * 60 * 60 * 1000;

// 覆盖前内容粗判：均匀抽取的采样点数与每点读取大小
constexpr int defSampleCount = 10;
constexpr std::uint64_t defSampleBlockSize = 4 * 1024;
