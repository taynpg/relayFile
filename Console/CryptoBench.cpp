#include <Crypto/AesGcm.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int hexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::vector<uint8_t> fromHex(const char* s)
{
    std::vector<uint8_t> out;
    while (s[0] && s[1]) {
        out.push_back(static_cast<uint8_t>((hexVal(s[0]) << 4) | hexVal(s[1])));
        s += 2;
    }
    return out;
}

void printHex(const char* label, const uint8_t* p, size_t n)
{
    printf("%s", label);
    for (size_t i = 0; i < n; ++i) printf("%02x", p[i]);
    printf("\n");
}

// NIST GCMVS / McGrew-Viega AES-256-GCM 向量：防止加解密双侧一致但都算错
//（roundtrip 校验无法发现 tag 计算系统性错误，必须用外部已知答案锚定）
bool runVector(const char* name, const char* keyHex, const char* nonceHex, const char* ptHex, const char* ctHex,
               const char* tagHex)
{
    auto key = fromHex(keyHex);
    auto nonce = fromHex(nonceHex);
    auto pt = fromHex(ptHex);
    auto expCt = fromHex(ctHex);
    auto expTag = fromHex(tagHex);

    std::vector<uint8_t> ct(pt.size());
    uint8_t tag[16];
    crypto::Aes256GcmEncrypt(key.data(), nonce.data(), pt.data(), pt.size(), ct.data(), tag);
    if (ct != expCt || std::memcmp(tag, expTag.data(), 16) != 0) {
        printf("FAIL: %s encrypt mismatch\n", name);
        printHex("  got ct : ", ct.data(), ct.size());
        printHex("  exp ct : ", expCt.data(), expCt.size());
        printHex("  got tag: ", tag, 16);
        printHex("  exp tag: ", expTag.data(), 16);
        return false;
    }
    std::vector<uint8_t> dec(pt.size());
    if (!crypto::Aes256GcmDecrypt(key.data(), nonce.data(), ct.data(), ct.size(), tag, dec.data()) || dec != pt) {
        printf("FAIL: %s decrypt mismatch\n", name);
        return false;
    }
    // 篡改 1 bit 必须验签失败
    if (!ct.empty()) {
        ct[0] ^= 1;
        if (crypto::Aes256GcmDecrypt(key.data(), nonce.data(), ct.data(), ct.size(), tag, dec.data())) {
            printf("FAIL: %s tamper accepted\n", name);
            return false;
        }
    }
    printf("PASS: %s\n", name);
    return true;
}

bool runVectors()
{
    bool ok = true;
    // Case 13: 零密钥零 nonce 空明文
    ok &= runVector("GCM empty (MV13)", "0000000000000000000000000000000000000000000000000000000000000000",
                    "000000000000000000000000", "", "", "530f8afbc74536b9a963b4f1c4cb738b");
    // Case 14: 零密钥零 nonce 16B 零明文
    ok &= runVector("GCM 16B zero (MV14)", "0000000000000000000000000000000000000000000000000000000000000000",
                    "000000000000000000000000", "00000000000000000000000000000000",
                    "cea7403d4d606b6e074ec5d3baf39d18", "d0d1c8a799996bf0265b98b5d48ab919");
    // Case 15: 非零密钥，60B 多块 GHASH + 尾块（tag 经 OpenSSL EVP 独立确认）
    ok &= runVector("GCM 60B (MV15)", "feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308",
                    "cafebabefacedbaddecaf888",
                    "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
                    "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39",
                    "522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa"
                    "8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662",
                    "eb9f796c8d356fc31a8433884b696f4f");
    return ok;
}

}   // namespace

int main()
{
    if (!runVectors()) {
        printf("NIST vectors FAILED, abort benchmark\n");
        return 1;
    }

    uint8_t key[32];
    uint8_t nonce[12];
    for (int i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(i);
    for (int i = 0; i < 12; ++i) nonce[i] = static_cast<uint8_t>(i);

    // 模拟文件传输块大小 256KB，累计处理 256MB
    const size_t blockSize = 256 * 1024;
    const int blocks = 1024;   // 256 MB

    std::vector<uint8_t> plain(blockSize);
    for (size_t i = 0; i < blockSize; ++i) plain[i] = static_cast<uint8_t>(i * 7 + 3);
    std::vector<uint8_t> cipher(blockSize);
    std::vector<uint8_t> decrypted(blockSize);
    uint8_t tag[16];

    // ---- 加密 ----
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < blocks; ++i) {
        crypto::Aes256GcmEncrypt(key, nonce, plain.data(), blockSize, cipher.data(), tag);
    }
    auto t1 = std::chrono::steady_clock::now();
    double encSec = std::chrono::duration<double>(t1 - t0).count();
    double totalMB = static_cast<double>(blockSize) * blocks / (1024.0 * 1024.0);
    printf("Encrypt: %.1f MB in %.3f s => %.1f MB/s\n", totalMB, encSec, totalMB / encSec);

    // ---- 解密 ----
    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < blocks; ++i) {
        bool ok = crypto::Aes256GcmDecrypt(key, nonce, cipher.data(), blockSize, tag, decrypted.data());
        if (!ok) {
            printf("Decrypt FAILED at block %d\n", i);
            return 1;
        }
    }
    t1 = std::chrono::steady_clock::now();
    double decSec = std::chrono::duration<double>(t1 - t0).count();
    printf("Decrypt: %.1f MB in %.3f s => %.1f MB/s\n", totalMB, decSec, totalMB / decSec);

    // ---- 正确性抽查 ----
    if (std::memcmp(plain.data(), decrypted.data(), blockSize) != 0) {
        printf("FAIL: roundtrip mismatch\n");
        return 1;
    }
    printf("Roundtrip OK\n");

    // ---- 大块篡改检测 ----
    cipher[12345] ^= 0x40;
    if (crypto::Aes256GcmDecrypt(key, nonce, cipher.data(), blockSize, tag, decrypted.data())) {
        printf("FAIL: tampered block accepted\n");
        return 1;
    }
    printf("Tamper reject OK\n");
    return 0;
}
