#include "CryptoHelper.h"

#include <QRandomGenerator>
#include <cstring>

#include "AesGcm.h"

CryptoHelper& CryptoHelper::instance()
{
    static CryptoHelper inst;
    return inst;
}

bool CryptoHelper::enabled() const
{
    return enabled_.load();
}

void CryptoHelper::setEnabled(bool enabled)
{
    enabled_.store(enabled);
}

bool CryptoHelper::hasKey() const
{
    return hasKey_.load();
}

void CryptoHelper::setKey(const std::string& passphrase)
{
    std::lock_guard<std::mutex> lock(mutex_);
    crypto::Sha256(passphrase.data(), passphrase.size(), key_);
    hasKey_.store(true);
}

void CryptoHelper::clearKey()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::memset(key_, 0, sizeof(key_));
    hasKey_.store(false);
}

void CryptoHelper::genNonce(uint8_t nonce[12])
{
    // 96-bit nonce：8 字节会话随机盐 + 4 字节自增计数器，同密钥下不重复。
    static const uint64_t sessionSalt = QRandomGenerator::system()->generate64();
    std::memcpy(nonce, &sessionSalt, 8);
    uint32_t counter = static_cast<uint32_t>(nonceCounter_.fetch_add(1));
    std::memcpy(nonce + 8, &counter, 4);
}

bool CryptoHelper::encryptFrame(FramePtr frame)
{
    if (!enabled_.load()) {
        return true;
    }
    if (!hasKey_.load()) {
        return true;   // 未配置密钥：放行（由调用方决定是否警告）
    }

    uint8_t nonce[12];
    genNonce(nonce);

    const size_t plainLen = frame->data.size();
    std::vector<char> out(12 + plainLen + 16);
    std::memcpy(out.data(), nonce, 12);

    crypto::Aes256GcmEncrypt(key_, nonce, reinterpret_cast<const uint8_t*>(frame->data.data()), plainLen,
                             reinterpret_cast<uint8_t*>(out.data() + 12),
                             reinterpret_cast<uint8_t*>(out.data() + 12 + plainLen));

    frame->data = std::move(out);
    frame->mark |= FRAME_MARK_ENCRYPTED;
    return true;
}

bool CryptoHelper::decryptFrame(FramePtr frame)
{
    if ((frame->mark & FRAME_MARK_ENCRYPTED) == 0) {
        return true;   // 未加密帧，直接放行
    }
    if (!hasKey_.load()) {
        return false;   // 收到加密帧但本端无密钥
    }

    const size_t totalLen = frame->data.size();
    if (totalLen < 12 + 16) {
        return false;   // 数据太短，不可能是合法加密帧
    }

    const uint8_t* nonce = reinterpret_cast<const uint8_t*>(frame->data.data());
    const size_t cipherLen = totalLen - 12 - 16;
    const uint8_t* cipher = nonce + 12;
    const uint8_t* tag = cipher + cipherLen;

    std::vector<char> plain(cipherLen);
    bool ok = crypto::Aes256GcmDecrypt(key_, nonce, cipher, cipherLen, tag, reinterpret_cast<uint8_t*>(plain.data()));
    if (!ok) {
        return false;
    }

    frame->data = std::move(plain);
    frame->mark &= ~FRAME_MARK_ENCRYPTED;
    return true;
}
