#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "Protocol/Protocol.h"

// 全局加解密助手：管理 AES-256-GCM 密钥与加解密开关。
// 发送前调用 encryptFrame，接收后调用 decryptFrame；两者均为 no-op 当未启用或未配置密钥。
class CryptoHelper
{
public:
    static CryptoHelper& instance();

    bool enabled() const;
    void setEnabled(bool enabled);
    bool hasKey() const;

    // 以 SHA-256(passphrase) 派生 32 字节密钥
    void setKey(const std::string& passphrase);
    void clearKey();

    // 对 frame->data 进行 AES-256-GCM 加密并置 mark |= FRAME_MARK_ENCRYPTED。
    // 若未启用加密或没有密钥，返回 true（无操作放行）。
    bool encryptFrame(FramePtr frame);

    // 若 frame->mark 含 FRAME_MARK_ENCRYPTED，则提取 nonce/tag 解密并恢复原始 data。
    // 解密失败（无密钥、tag 校验失败、数据过短）返回 false，调用方应丢弃该帧。
    bool decryptFrame(FramePtr frame);

private:
    CryptoHelper() = default;
    ~CryptoHelper() = default;

    void genNonce(uint8_t nonce[12]);

private:
    std::atomic<bool> enabled_{false};
    std::atomic<bool> hasKey_{false};
    uint8_t key_[32]{};
    std::atomic<uint64_t> nonceCounter_{0};
    mutable std::mutex mutex_;   // 保护 key_ 的写操作
};
