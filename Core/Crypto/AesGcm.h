#pragma once

// 自包含加密原语：SHA-256 与 AES-256-GCM（仅依赖标准库，无 OpenSSL）。
// 仅供本工具内部传输加密使用。

#include <cstddef>
#include <cstdint>

namespace crypto {

// 单次 SHA-256，out 输出 32 字节摘要（用于口令派生密钥）
void Sha256(const void* data, size_t len, uint8_t out[32]);

// AES-256-GCM 加密。key 32 字节，nonce 12 字节。
// cipher 输出与 plain 等长的密文，tag 输出 16 字节认证标签。
void Aes256GcmEncrypt(const uint8_t key[32], const uint8_t nonce[12], const uint8_t* plain, size_t len, uint8_t* cipher,
                      uint8_t tag[16]);

// AES-256-GCM 解密并校验 tag。tag 不匹配（密钥错/数据被篡改）返回 false，out 内容无效。
bool Aes256GcmDecrypt(const uint8_t key[32], const uint8_t nonce[12], const uint8_t* cipher, size_t len,
                      const uint8_t tag[16], uint8_t* out);

}   // namespace crypto
