#include "AesGcm.h"

#include <algorithm>
#include <cstring>

namespace crypto {
namespace {

// ============== SHA-256 ==============

static const uint32_t kSha256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32 - n));
}

inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) ^ (~x & z);
}
inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) ^ (x & z) ^ (y & z);
}
inline uint32_t ep0(uint32_t x)
{
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}
inline uint32_t ep1(uint32_t x)
{
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}
inline uint32_t sig0(uint32_t x)
{
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}
inline uint32_t sig1(uint32_t x)
{
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

struct Sha256Ctx {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t data[64];
    uint32_t datalen;
};

void sha256Init(Sha256Ctx& ctx)
{
    ctx.datalen = 0;
    ctx.bitlen = 0;
    ctx.state[0] = 0x6a09e667;
    ctx.state[1] = 0xbb67ae85;
    ctx.state[2] = 0x3c6ef372;
    ctx.state[3] = 0xa54ff53a;
    ctx.state[4] = 0x510e527f;
    ctx.state[5] = 0x9b05688c;
    ctx.state[6] = 0x1f83d9ab;
    ctx.state[7] = 0x5be0cd19;
}

void sha256Transform(Sha256Ctx& ctx, const uint8_t data[])
{
    uint32_t a, b, c, d, e, f, g, h, i, j, t1, t2, m[64];

    for (i = 0, j = 0; i < 16; ++i, j += 4) {
        m[i] = (static_cast<uint32_t>(data[j]) << 24) | (static_cast<uint32_t>(data[j + 1]) << 16)
               | (static_cast<uint32_t>(data[j + 2]) << 8) | (static_cast<uint32_t>(data[j + 3]));
    }
    for (; i < 64; ++i) {
        m[i] = sig1(m[i - 2]) + m[i - 7] + sig0(m[i - 15]) + m[i - 16];
    }

    a = ctx.state[0];
    b = ctx.state[1];
    c = ctx.state[2];
    d = ctx.state[3];
    e = ctx.state[4];
    f = ctx.state[5];
    g = ctx.state[6];
    h = ctx.state[7];

    for (i = 0; i < 64; ++i) {
        t1 = h + ep1(e) + ch(e, f, g) + kSha256[i] + m[i];
        t2 = ep0(a) + maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx.state[0] += a;
    ctx.state[1] += b;
    ctx.state[2] += c;
    ctx.state[3] += d;
    ctx.state[4] += e;
    ctx.state[5] += f;
    ctx.state[6] += g;
    ctx.state[7] += h;
}

void sha256Update(Sha256Ctx& ctx, const uint8_t* data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        ctx.data[ctx.datalen] = data[i];
        ctx.datalen++;
        if (ctx.datalen == 64) {
            sha256Transform(ctx, ctx.data);
            ctx.bitlen += 512;
            ctx.datalen = 0;
        }
    }
}

void sha256Final(Sha256Ctx& ctx, uint8_t hash[32])
{
    uint32_t i = ctx.datalen;

    if (ctx.datalen < 56) {
        ctx.data[i++] = 0x80;
        while (i < 56) ctx.data[i++] = 0x00;
    } else {
        ctx.data[i++] = 0x80;
        while (i < 64) ctx.data[i++] = 0x00;
        sha256Transform(ctx, ctx.data);
        std::memset(ctx.data, 0, 56);
    }

    ctx.bitlen += ctx.datalen * 8;
    ctx.data[63] = static_cast<uint8_t>(ctx.bitlen);
    ctx.data[62] = static_cast<uint8_t>(ctx.bitlen >> 8);
    ctx.data[61] = static_cast<uint8_t>(ctx.bitlen >> 16);
    ctx.data[60] = static_cast<uint8_t>(ctx.bitlen >> 24);
    ctx.data[59] = static_cast<uint8_t>(ctx.bitlen >> 32);
    ctx.data[58] = static_cast<uint8_t>(ctx.bitlen >> 40);
    ctx.data[57] = static_cast<uint8_t>(ctx.bitlen >> 48);
    ctx.data[56] = static_cast<uint8_t>(ctx.bitlen >> 56);
    sha256Transform(ctx, ctx.data);

    for (i = 0; i < 4; ++i) {
        hash[i] = static_cast<uint8_t>((ctx.state[0] >> (24 - i * 8)) & 0x000000ff);
        hash[i + 4] = static_cast<uint8_t>((ctx.state[1] >> (24 - i * 8)) & 0x000000ff);
        hash[i + 8] = static_cast<uint8_t>((ctx.state[2] >> (24 - i * 8)) & 0x000000ff);
        hash[i + 12] = static_cast<uint8_t>((ctx.state[3] >> (24 - i * 8)) & 0x000000ff);
        hash[i + 16] = static_cast<uint8_t>((ctx.state[4] >> (24 - i * 8)) & 0x000000ff);
        hash[i + 20] = static_cast<uint8_t>((ctx.state[5] >> (24 - i * 8)) & 0x000000ff);
        hash[i + 24] = static_cast<uint8_t>((ctx.state[6] >> (24 - i * 8)) & 0x000000ff);
        hash[i + 28] = static_cast<uint8_t>((ctx.state[7] >> (24 - i * 8)) & 0x000000ff);
    }
}

// ============== AES-256 ==============

static const uint8_t sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16};

static const uint8_t rcon[11] = {0x8d, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36};

inline uint32_t rotWord(uint32_t x)
{
    return (x << 8) | (x >> 24);
}
inline uint32_t subWord(uint32_t x)
{
    return (static_cast<uint32_t>(sbox[x >> 24]) << 24) | (static_cast<uint32_t>(sbox[(x >> 16) & 0xff]) << 16)
           | (static_cast<uint32_t>(sbox[(x >> 8) & 0xff]) << 8) | static_cast<uint32_t>(sbox[x & 0xff]);
}

inline uint8_t xtime(uint8_t x)
{
    return static_cast<uint8_t>((x << 1) ^ ((x >> 7) * 0x1b));
}
inline uint8_t mul2(uint8_t x)
{
    return xtime(x);
}
inline uint8_t mul3(uint8_t x)
{
    return xtime(x) ^ x;
}

// T-table：把 SubBytes+ShiftRows+MixColumns 合并为 4 张各 256 项的 uint32 表，
// 每列一轮只需 4 次查表 + 4 次异或。启动时惰性初始化一次。
struct TTables {
    uint32_t te0[256];
    uint32_t te1[256];
    uint32_t te2[256];
    uint32_t te3[256];
    TTables()
    {
        for (int i = 0; i < 256; ++i) {
            uint32_t s = sbox[i];
            uint32_t s2 = mul2(static_cast<uint8_t>(s));
            uint32_t s3 = mul3(static_cast<uint8_t>(s));
            te0[i] = (s2 << 24) | (s << 16) | (s << 8) | s3;
            te1[i] = (s3 << 24) | (s2 << 16) | (s << 8) | s;
            te2[i] = (s << 24) | (s3 << 16) | (s2 << 8) | s;
            te3[i] = (s << 24) | (s << 16) | (s3 << 8) | s2;
        }
    }
};

const TTables& tt()
{
    static const TTables t;
    return t;
}

void aes256KeyExpansion(const uint8_t key[32], uint32_t rk[60])
{
    for (int i = 0; i < 8; ++i) {
        rk[i] = (static_cast<uint32_t>(key[4 * i]) << 24) | (static_cast<uint32_t>(key[4 * i + 1]) << 16)
                | (static_cast<uint32_t>(key[4 * i + 2]) << 8) | static_cast<uint32_t>(key[4 * i + 3]);
    }
    for (int i = 8; i < 60; ++i) {
        uint32_t temp = rk[i - 1];
        if (i % 8 == 0) {
            // Rcon 作用于字的最高字节
            temp = subWord(rotWord(temp)) ^ (static_cast<uint32_t>(rcon[i / 8]) << 24);
        } else if (i % 8 == 4) {
            temp = subWord(temp);
        }
        rk[i] = rk[i - 8] ^ temp;
    }
}

inline uint32_t loadBE(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16)
           | (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

inline void storeBE(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

void aes256EncryptBlock(const uint8_t in[16], uint8_t out[16], const uint32_t rk[60])
{
    const auto& t = tt();
    uint32_t s0 = loadBE(in) ^ rk[0];
    uint32_t s1 = loadBE(in + 4) ^ rk[1];
    uint32_t s2 = loadBE(in + 8) ^ rk[2];
    uint32_t s3 = loadBE(in + 12) ^ rk[3];

    for (int r = 1; r < 14; ++r) {
        const uint32_t* k = rk + 4 * r;
        uint32_t t0 = t.te0[s0 >> 24] ^ t.te1[(s1 >> 16) & 0xff] ^ t.te2[(s2 >> 8) & 0xff] ^ t.te3[s3 & 0xff] ^ k[0];
        uint32_t t1 = t.te0[s1 >> 24] ^ t.te1[(s2 >> 16) & 0xff] ^ t.te2[(s3 >> 8) & 0xff] ^ t.te3[s0 & 0xff] ^ k[1];
        uint32_t t2 = t.te0[s2 >> 24] ^ t.te1[(s3 >> 16) & 0xff] ^ t.te2[(s0 >> 8) & 0xff] ^ t.te3[s1 & 0xff] ^ k[2];
        uint32_t t3 = t.te0[s3 >> 24] ^ t.te1[(s0 >> 16) & 0xff] ^ t.te2[(s1 >> 8) & 0xff] ^ t.te3[s2 & 0xff] ^ k[3];
        s0 = t0;
        s1 = t1;
        s2 = t2;
        s3 = t3;
    }

    // 最后一轮：无 MixColumns，用 sbox 直接构造
    // 注意：^ 优先级高于 |，轮密钥异或必须加括号作用于整个字
    const uint32_t* k = rk + 56;
    uint32_t o0 = ((static_cast<uint32_t>(sbox[s0 >> 24]) << 24) | (static_cast<uint32_t>(sbox[(s1 >> 16) & 0xff]) << 16)
                   | (static_cast<uint32_t>(sbox[(s2 >> 8) & 0xff]) << 8) | static_cast<uint32_t>(sbox[s3 & 0xff]))
                  ^ k[0];
    uint32_t o1 = ((static_cast<uint32_t>(sbox[s1 >> 24]) << 24) | (static_cast<uint32_t>(sbox[(s2 >> 16) & 0xff]) << 16)
                   | (static_cast<uint32_t>(sbox[(s3 >> 8) & 0xff]) << 8) | static_cast<uint32_t>(sbox[s0 & 0xff]))
                  ^ k[1];
    uint32_t o2 = ((static_cast<uint32_t>(sbox[s2 >> 24]) << 24) | (static_cast<uint32_t>(sbox[(s3 >> 16) & 0xff]) << 16)
                   | (static_cast<uint32_t>(sbox[(s0 >> 8) & 0xff]) << 8) | static_cast<uint32_t>(sbox[s1 & 0xff]))
                  ^ k[2];
    uint32_t o3 = ((static_cast<uint32_t>(sbox[s3 >> 24]) << 24) | (static_cast<uint32_t>(sbox[(s0 >> 16) & 0xff]) << 16)
                   | (static_cast<uint32_t>(sbox[(s1 >> 8) & 0xff]) << 8) | static_cast<uint32_t>(sbox[s2 & 0xff]))
                  ^ k[3];

    storeBE(out, o0);
    storeBE(out + 4, o1);
    storeBE(out + 8, o2);
    storeBE(out + 12, o3);
}

// ============== GCM ==============

// GF(2^128) 乘法（NIST SP 800-38D Algorithm 1），用 hi/lo 两个 uint64 表示 128 位块，
// 字节序为大端（块字节 0..7 -> hi，8..15 -> lo）。比逐字节实现快约一个数量级。
struct GfBlock {
    uint64_t hi;
    uint64_t lo;
};

static inline GfBlock gfFromBytes(const uint8_t b[16])
{
    GfBlock r{};
    for (int i = 0; i < 8; ++i) r.hi = (r.hi << 8) | b[i];
    for (int i = 8; i < 16; ++i) r.lo = (r.lo << 8) | b[i];
    return r;
}

static inline void gfToBytes(GfBlock v, uint8_t out[16])
{
    for (int i = 7; i >= 0; --i) {
        out[i] = static_cast<uint8_t>(v.hi & 0xff);
        v.hi >>= 8;
    }
    for (int i = 15; i >= 8; --i) {
        out[i] = static_cast<uint8_t>(v.lo & 0xff);
        v.lo >>= 8;
    }
}

static inline GfBlock gfMul(GfBlock x, GfBlock y)
{
    // Z = 0, V = Y
    GfBlock z{0, 0};
    GfBlock v = y;
    for (int i = 0; i < 128; ++i) {
        // 取 x 的第 i 位（从最高位开始）
        uint64_t xi = (i < 64) ? ((x.hi >> (63 - i)) & 1) : ((x.lo >> (127 - i)) & 1);
        if (xi) {
            z.hi ^= v.hi;
            z.lo ^= v.lo;
        }
        // V = V >> 1；若移出位为 1，则 V ^= R（R = 0xE1 << 120）
        uint64_t carry = v.lo & 1;
        v.lo = (v.lo >> 1) | (v.hi << 63);
        v.hi >>= 1;
        if (carry) v.hi ^= 0xE100000000000000ULL;
    }
    return z;
}

// 4-bit 查表乘法（Shoup 变体）：m[n][v] = H·Q(v)·x^(4n)，n 为从左起 nibble 序号，
// Q(v) = Σ bit_j(v)·x^(3-j)（即 byte0 = v<<4 的块）。每块仅需 32 次查表+异或。
// 建表用 gfMul + x4mul 递推，保证与逐位实现一致。
struct GhashTable {
    GfBlock m[32][16]{};

    // x4mul：Z • x^4 = (Z >> 4) ^ rtab[Z & 0xF]，仅建表时使用
    static GfBlock x4mul(GfBlock z)
    {
        // rtab[c]：被移出低 nibble c 的第 j 位是 x^(127-j) 的系数，乘 x^4 得
        // x^(131-j) ≡ x^(3-j)·x^128，即 R >> (3-j)（R = 0xE1<<120 为 x^128 约减式）
        static const GfBlock rtab[16] = {
            {0x0000000000000000ULL, 0}, {0x1C20000000000000ULL, 0}, {0x3840000000000000ULL, 0},
            {0x2460000000000000ULL, 0}, {0x7080000000000000ULL, 0}, {0x6CA0000000000000ULL, 0},
            {0x48C0000000000000ULL, 0}, {0x54E0000000000000ULL, 0}, {0xE100000000000000ULL, 0},
            {0xFD20000000000000ULL, 0}, {0xD940000000000000ULL, 0}, {0xC560000000000000ULL, 0},
            {0x9180000000000000ULL, 0}, {0x8DA0000000000000ULL, 0}, {0xA9C0000000000000ULL, 0},
            {0xB5E0000000000000ULL, 0},
        };
        GfBlock r = rtab[z.lo & 0xF];
        z.lo = (z.lo >> 4) | (z.hi << 60);
        z.hi >>= 4;
        z.hi ^= r.hi;
        z.lo ^= r.lo;
        return z;
    }

    void init(GfBlock H)
    {
        for (int v = 0; v < 16; ++v) {
            uint8_t b[16] = {0};
            b[0] = static_cast<uint8_t>(v << 4);
            m[0][v] = gfMul(gfFromBytes(b), H);
        }
        for (int n = 1; n < 32; ++n) {
            for (int v = 0; v < 16; ++v) {
                m[n][v] = x4mul(m[n - 1][v]);
            }
        }
    }

    // X • H：Z = Σ_n m[n][nib_n]，nib 从 hi/lo 寄存器直接提取，无字节转换
    GfBlock mul(GfBlock x) const
    {
        GfBlock z{0, 0};
        for (int n = 0; n < 16; ++n) {
            GfBlock t = m[n][(x.hi >> (60 - 4 * n)) & 0xF];
            z.hi ^= t.hi;
            z.lo ^= t.lo;
        }
        for (int n = 0; n < 16; ++n) {
            GfBlock t = m[16 + n][(x.lo >> (60 - 4 * n)) & 0xF];
            z.hi ^= t.hi;
            z.lo ^= t.lo;
        }
        return z;
    }
};

void ghash(const uint8_t h[16], const uint8_t* aad, size_t aadLen, const uint8_t* cipher, size_t cipherLen,
           uint8_t out[16])
{
    GhashTable gt;
    gt.init(gfFromBytes(h));
    GfBlock Y{0, 0};
    uint8_t block[16];

    auto absorb = [&](const uint8_t* data, size_t len) {
        size_t i = 0;
        while (i < len) {
            size_t n = std::min(len - i, static_cast<size_t>(16));
            std::memset(block, 0, 16);
            std::memcpy(block, data + i, n);
            GfBlock b = gfFromBytes(block);
            Y.hi ^= b.hi;
            Y.lo ^= b.lo;
            Y = gt.mul(Y);
            i += 16;
        }
    };

    absorb(aad, aadLen);
    absorb(cipher, cipherLen);

    std::memset(block, 0, 16);
    uint64_t aadBits = static_cast<uint64_t>(aadLen) * 8;
    uint64_t cipherBits = static_cast<uint64_t>(cipherLen) * 8;
    for (int i = 7; i >= 0; --i) {
        block[i] = static_cast<uint8_t>(aadBits & 0xff);
        aadBits >>= 8;
    }
    for (int i = 15; i >= 8; --i) {
        block[i] = static_cast<uint8_t>(cipherBits & 0xff);
        cipherBits >>= 8;
    }
    GfBlock lb = gfFromBytes(block);
    Y.hi ^= lb.hi;
    Y.lo ^= lb.lo;
    Y = gt.mul(Y);

    gfToBytes(Y, out);
}

}   // namespace

// ============== 外部接口 ==============

void Sha256(const void* data, size_t len, uint8_t out[32])
{
    Sha256Ctx ctx;
    sha256Init(ctx);
    sha256Update(ctx, static_cast<const uint8_t*>(data), len);
    sha256Final(ctx, out);
}

void Aes256GcmEncrypt(const uint8_t key[32], const uint8_t nonce[12], const uint8_t* plain, size_t len, uint8_t* cipher,
                      uint8_t tag[16])
{
    uint32_t rk[60];
    aes256KeyExpansion(key, rk);

    uint8_t h[16] = {0};
    aes256EncryptBlock(h, h, rk);

    uint8_t y0[16];
    std::memcpy(y0, nonce, 12);
    y0[12] = 0;
    y0[13] = 0;
    y0[14] = 0;
    y0[15] = 1;

    uint8_t counter[16];
    std::memcpy(counter, y0, 16);

    for (size_t i = 0; i < len; i += 16) {
        for (int j = 15; j >= 12; --j) {
            if (++counter[j] != 0) break;
        }
        uint8_t ks[16];
        aes256EncryptBlock(counter, ks, rk);
        size_t n = std::min(len - i, static_cast<size_t>(16));
        for (size_t j = 0; j < n; ++j) cipher[i + j] = plain[i + j] ^ ks[j];
    }

    uint8_t ghashOut[16];
    ghash(h, nullptr, 0, cipher, len, ghashOut);

    uint8_t ekY0[16];
    aes256EncryptBlock(y0, ekY0, rk);
    for (int i = 0; i < 16; ++i) tag[i] = ghashOut[i] ^ ekY0[i];
}

bool Aes256GcmDecrypt(const uint8_t key[32], const uint8_t nonce[12], const uint8_t* cipher, size_t len,
                      const uint8_t tag[16], uint8_t* out)
{
    uint32_t rk[60];
    aes256KeyExpansion(key, rk);

    uint8_t h[16] = {0};
    aes256EncryptBlock(h, h, rk);

    uint8_t y0[16];
    std::memcpy(y0, nonce, 12);
    y0[12] = 0;
    y0[13] = 0;
    y0[14] = 0;
    y0[15] = 1;

    // 验证 tag
    uint8_t ghashOut[16];
    ghash(h, nullptr, 0, cipher, len, ghashOut);
    uint8_t ekY0[16];
    aes256EncryptBlock(y0, ekY0, rk);
    uint8_t computedTag[16];
    for (int i = 0; i < 16; ++i) computedTag[i] = ghashOut[i] ^ ekY0[i];
    bool ok = true;
    for (int i = 0; i < 16; ++i) {
        if (computedTag[i] != tag[i]) ok = false;
    }
    if (!ok) return false;

    // CTR 解密（与加密相同）
    uint8_t counter[16];
    std::memcpy(counter, y0, 16);
    for (size_t i = 0; i < len; i += 16) {
        for (int j = 15; j >= 12; --j) {
            if (++counter[j] != 0) break;
        }
        uint8_t ks[16];
        aes256EncryptBlock(counter, ks, rk);
        size_t n = std::min(len - i, static_cast<size_t>(16));
        for (size_t j = 0; j < n; ++j) out[i + j] = cipher[i + j] ^ ks[j];
    }
    return true;
}

}   // namespace crypto
