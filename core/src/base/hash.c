/* CRC-32, SHA-1, SHA-256. */
#include "internal.h"

#include <string.h>

#include "crc_table.inc"

#if defined(__aarch64__) && defined(__APPLE__)
#include <arm_acle.h>
#include <arm_neon.h>
#define MH_HW_HASH 1 /* every Apple Silicon core has the CRC32 and SHA-1 instructions */
#endif

/* orig: crc32_update @0x140012e40 (bitwise there, table driven here, same result) */
uint32_t mh_crc32_update_portable(uint32_t crc, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    while (len--) crc = (crc >> 8) ^ k_crc32_table[(crc ^ *p++) & 0xFF];
    return crc;
}

#ifdef MH_HW_HASH
/* The ARM CRC32 instruction is the same reflected 0xEDB88320 polynomial, eight bytes at a time. */
__attribute__((target("crc"))) static uint32_t crc32_update_hw(uint32_t crc, const uint8_t *p, size_t len)
{
    while (len >= 8) {
        uint64_t v;
        memcpy(&v, p, 8);
        crc = __crc32d(crc, v);
        p += 8;
        len -= 8;
    }
    while (len--) crc = __crc32b(crc, *p++);
    return crc;
}
#endif

uint32_t mh_crc32_update(uint32_t crc, const void *buf, size_t len)
{
#ifdef MH_HW_HASH
    return crc32_update_hw(crc, buf, len);
#else
    return mh_crc32_update_portable(crc, buf, len);
#endif
}

uint32_t mh_crc32(const void *buf, size_t len)
{
    return mh_crc32_end(mh_crc32_update(mh_crc32_begin(), buf, len));
}

/* orig: crc_record @0x140012fb0 */
uint32_t mh_crc_record(const void *rec, size_t len)
{
    return mh_crc32(rec, len >= 4 ? len - 4 : 0);
}

#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

/* orig: sha1_block @0x140030330 */
void mh_sha1_block(uint32_t h[5], const uint8_t blk[64])
{
    uint32_t w[80], a, b, c, d, e, t;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = (uint32_t)blk[4 * i] << 24 | (uint32_t)blk[4 * i + 1] << 16 |
               (uint32_t)blk[4 * i + 2] << 8 | blk[4 * i + 3];
    for (; i < 80; i++) w[i] = ROL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4];
    for (i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5a827999u; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1u; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdcu; }
        else { f = b ^ c ^ d; k = 0xca62c1d6u; }
        t = ROL(a, 5) + f + e + k + w[i];
        e = d; d = c; c = ROL(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

#ifdef MH_HW_HASH
/* SHA-1 on the ARMv8 crypto instructions: 20 groups of four rounds, message schedule with SHA1SU0/SU1. */
__attribute__((target("sha2"))) static void sha1_blocks_hw(uint32_t h[5], const uint8_t *data, size_t n)
{
    const uint32x4_t k[4] = {vdupq_n_u32(0x5A827999u), vdupq_n_u32(0x6ED9EBA1u), vdupq_n_u32(0x8F1BBCDCu),
                             vdupq_n_u32(0xCA62C1D6u)};
    uint32x4_t abcd = vld1q_u32(h);
    uint32_t e = h[4];
    while (n--) {
        uint32x4_t abcd_save = abcd, w[20];
        uint32_t e_save = e;
        int i;
        for (i = 0; i < 4; i++) w[i] = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(data + 16 * i)));
        for (i = 4; i < 20; i++)
            w[i] = vsha1su1q_u32(vsha1su0q_u32(w[i - 4], w[i - 3], w[i - 2]), w[i - 1]);
        for (i = 0; i < 20; i++) {
            uint32x4_t t = vaddq_u32(w[i], k[i / 5]);
            uint32_t next = vsha1h_u32(vgetq_lane_u32(abcd, 0));
            if (i < 5) abcd = vsha1cq_u32(abcd, e, t);
            else if (i < 10 || i >= 15) abcd = vsha1pq_u32(abcd, e, t);
            else abcd = vsha1mq_u32(abcd, e, t);
            e = next;
        }
        abcd = vaddq_u32(abcd, abcd_save);
        e += e_save;
        data += 64;
    }
    vst1q_u32(h, abcd);
    h[4] = e;
}
#endif

/* Whole 64 byte blocks, as fast as the machine allows (same result as calling mh_sha1_block n times). */
void mh_sha1_blocks(uint32_t h[5], const uint8_t *data, size_t n)
{
#ifdef MH_HW_HASH
    sha1_blocks_hw(h, data, n);
#else
    while (n--) {
        mh_sha1_block(h, data);
        data += 64;
    }
#endif
}

/* orig: sha1_init @0x1400309f0 */
void mh_sha1_init(MhSha1 *c)
{
    memset(c, 0, sizeof *c);
    c->h[0] = 0x67452301u; c->h[1] = 0xefcdab89u; c->h[2] = 0x98badcfeu;
    c->h[3] = 0x10325476u; c->h[4] = 0xc3d2e1f0u;
}

/* orig: sha1_update @0x140030a20 */
void mh_sha1_update(MhSha1 *c, const void *data, size_t len)
{
    const uint8_t *p = data;
    c->total += len;
    if (c->buflen) { /* top up the partial block first */
        size_t k = 64 - c->buflen;
        if (k > len) k = len;
        memcpy(c->buf + c->buflen, p, k);
        c->buflen += (uint32_t)k;
        p += k;
        len -= k;
        if (c->buflen < 64) return;
        mh_sha1_block(c->h, c->buf);
        c->buflen = 0;
    }
    if (len >= 64) { /* the whole blocks straight from the caller's buffer */
        size_t n = len / 64;
        mh_sha1_blocks(c->h, p, n);
        p += n * 64;
        len -= n * 64;
    }
    if (len) {
        memcpy(c->buf, p, len);
        c->buflen = (uint32_t)len;
    }
}

/* orig: sha1_final @0x140030770 */
void mh_sha1_final(MhSha1 *c, uint8_t out[20])
{
    uint64_t bits = c->total * 8;
    int i;
    c->buf[c->buflen++] = 0x80;
    if (c->buflen > 56) {
        memset(c->buf + c->buflen, 0, 64 - c->buflen);
        mh_sha1_block(c->h, c->buf);
        c->buflen = 0;
    }
    memset(c->buf + c->buflen, 0, 56 - c->buflen);
    for (i = 0; i < 8; i++) c->buf[56 + i] = (uint8_t)(bits >> (56 - 8 * i));
    mh_sha1_block(c->h, c->buf);
    for (i = 0; i < 20; i++) out[i] = (uint8_t)(c->h[i / 4] >> (24 - 8 * (i % 4)));
}

void mh_sha1(const void *data, size_t len, uint8_t out[20])
{
    MhSha1 c;
    mh_sha1_init(&c);
    mh_sha1_update(&c, data, len);
    mh_sha1_final(&c, out);
}

static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

/* orig: sha256_block @0x140030ac0 */
void mh_sha256_block(uint32_t h[8], const uint8_t blk[64])
{
    uint32_t w[64], v[8], t1, t2;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = (uint32_t)blk[4 * i] << 24 | (uint32_t)blk[4 * i + 1] << 16 |
               (uint32_t)blk[4 * i + 2] << 8 | blk[4 * i + 3];
    for (; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    memcpy(v, h, sizeof v);
    for (i = 0; i < 64; i++) {
        uint32_t S1 = ROR(v[4], 6) ^ ROR(v[4], 11) ^ ROR(v[4], 25);
        uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
        uint32_t S0 = ROR(v[0], 2) ^ ROR(v[0], 13) ^ ROR(v[0], 22);
        uint32_t mj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        t1 = v[7] + S1 + ch + K256[i] + w[i];
        t2 = S0 + mj;
        v[7] = v[6]; v[6] = v[5]; v[5] = v[4]; v[4] = v[3] + t1;
        v[3] = v[2]; v[2] = v[1]; v[1] = v[0]; v[0] = t1 + t2;
    }
    for (i = 0; i < 8; i++) h[i] += v[i];
}

/* orig: inlined in cheat_db_load @0x1400070f0 (IV at 0x140f0dd80) */
void mh_sha256_init(MhSha256 *c)
{
    static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memset(c, 0, sizeof *c);
    memcpy(c->h, iv, sizeof iv);
}

void mh_sha256_update(MhSha256 *c, const void *data, size_t len)
{
    const uint8_t *p = data;
    c->total += len;
    while (len) {
        size_t k = 64 - c->buflen;
        if (k > len) k = len;
        memcpy(c->buf + c->buflen, p, k);
        c->buflen += (uint32_t)k;
        p += k;
        len -= k;
        if (c->buflen == 64) {
            mh_sha256_block(c->h, c->buf);
            c->buflen = 0;
        }
    }
}

void mh_sha256_final(MhSha256 *c, uint8_t out[32])
{
    uint64_t bits = c->total * 8;
    int i;
    c->buf[c->buflen++] = 0x80;
    if (c->buflen > 56) {
        memset(c->buf + c->buflen, 0, 64 - c->buflen);
        mh_sha256_block(c->h, c->buf);
        c->buflen = 0;
    }
    memset(c->buf + c->buflen, 0, 56 - c->buflen);
    for (i = 0; i < 8; i++) c->buf[56 + i] = (uint8_t)(bits >> (56 - 8 * i));
    mh_sha256_block(c->h, c->buf);
    for (i = 0; i < 32; i++) out[i] = (uint8_t)(c->h[i / 4] >> (24 - 8 * (i % 4)));
}

void mh_sha256(const void *data, size_t len, uint8_t out[32])
{
    MhSha256 c;
    mh_sha256_init(&c);
    mh_sha256_update(&c, data, len);
    mh_sha256_final(&c, out);
}
