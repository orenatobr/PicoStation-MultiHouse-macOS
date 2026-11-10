/* mh/hash.h: CRC-32, SHA-1 and SHA-256, all standard algorithms (verified against the
 * original: the SHA-256 of the embedded cheat database equals the original's kExpectedSha256). */
#ifndef MH_HASH_H
#define MH_HASH_H

#include "mh/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- CRC-32 (IEEE, reflected 0xEDB88320). Orig: crc32_begin/update/end ---------------------
 *   crc = mh_crc32_begin(); crc = mh_crc32_update(crc, p, n); ... mh_crc32_end(crc) */
static inline uint32_t mh_crc32_begin(void) { return 0xFFFFFFFFu; }
uint32_t mh_crc32_update(uint32_t crc, const void *buf, size_t len);
/* The table driven reference of mh_crc32_update, same result; kept so a test can compare the two. */
uint32_t mh_crc32_update_portable(uint32_t crc, const void *buf, size_t len);
static inline uint32_t mh_crc32_end(uint32_t crc) { return ~crc; }
uint32_t mh_crc32(const void *buf, size_t len); /* one-shot */
/* orig: crc_record. CRC of a record whose last 4 bytes hold its own CRC: covers
 * rec[0 .. len-4). len < 4 hashes nothing. The caller compares it with the stored u32. */
uint32_t mh_crc_record(const void *rec, size_t len);

/* ---- SHA-1. Orig: sha1_init/update/final/block (context was 0x68 bytes) -------------------- */
typedef struct MhSha1 {
    uint32_t h[5];
    uint64_t total; /* bytes hashed so far */
    uint8_t buf[64];
    uint32_t buflen;
} MhSha1;
void mh_sha1_init(MhSha1 *c);
void mh_sha1_update(MhSha1 *c, const void *data, size_t len);
void mh_sha1_final(MhSha1 *c, uint8_t out[20]);
void mh_sha1_block(uint32_t h[5], const uint8_t block[64]); /* orig: sha1_block */
/* n whole 64 byte blocks on the hardware SHA-1 instructions where there are any; same result as n calls
 * of mh_sha1_block. */
void mh_sha1_blocks(uint32_t h[5], const uint8_t *data, size_t n);
void mh_sha1(const void *data, size_t len, uint8_t out[20]); /* one-shot */

/* ---- SHA-256. Orig: only sha256_block existed as a function; cheat_db_load inlined the
 * init (standard IV), the buffering and the padding. This is the complete standard API. ------ */
typedef struct MhSha256 {
    uint32_t h[8];
    uint64_t total;
    uint8_t buf[64];
    uint32_t buflen;
} MhSha256;
void mh_sha256_init(MhSha256 *c);
void mh_sha256_update(MhSha256 *c, const void *data, size_t len);
void mh_sha256_final(MhSha256 *c, uint8_t out[32]);
void mh_sha256_block(uint32_t h[8], const uint8_t block[64]); /* orig: sha256_block */
void mh_sha256(const void *data, size_t len, uint8_t out[32]); /* one-shot */

#ifdef __cplusplus
}
#endif
#endif
