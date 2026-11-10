/* mh/cdsector.h: raw CD sector (2352 bytes) checks, EDC/ECC and the V2 track map sector.
 *
 * Sector layout as implemented by the original (ECMA-130):
 *   0x000  sync: 00 FF FF FF FF FF FF FF FF FF FF 00           (12 bytes)
 *   0x00C  header: minute, second, frame (BCD), mode           (4 bytes, mode at 0x00F)
 * Mode 1 (mode byte 1):
 *   0x010  user data                                           (2048 bytes)
 *   0x810  EDC, little endian, over bytes 0x000..0x80F
 *   0x814  8 reserved bytes (zero, they take part in the ECC)
 *   0x81C  P parity (172 bytes), 0x8C8 Q parity (104 bytes), ECC runs over 0x00C..0x81B
 * Mode 2 (mode byte 2), 8 byte subheader twice: file, channel, submode, coding, x2
 *   (bytes 0x10..0x13 must equal 0x14..0x17; submode at 0x12, bit 0x20 selects Form 2)
 *   Form 1: 0x018 data (2048), EDC at 0x818 over 0x010..0x817, then P/Q parity at 0x81C/0x8C8
 *           computed with the 4 header bytes treated as zero
 *   Form 2: 0x018 data (2324), EDC at 0x92C over 0x010..0x92B; an EDC of 0 means "unused" and
 *           is accepted; there is no ECC
 * The original filled the EDC/ECC tables lazily at the first call; here they are constants
 * (the tests re-derive them algorithmically and compare). */
#ifndef MH_CDSECTOR_H
#define MH_CDSECTOR_H

#include "mh/common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MH_SECTOR_RAW 2352
/* A raw sector stored on the SD image occupies 5 blocks of 512 bytes: the 2352 bytes plus
 * 208 zero bytes (see mh_io_read_raw_payload). */
#define MH_SECTOR_SLOT 2560

/* Result bits of mh_cd_sector_check / mh_cd_sector_recode. 0 means good. */
enum {
    MH_SECTOR_OK = 0,
    MH_SECTOR_BAD_FORMAT = 1, /* not a Mode 1/2 sector: sync, header or subheader copy wrong */
    MH_SECTOR_BAD_EDC = 2,
    MH_SECTOR_BAD_ECC = 4
};

/* orig: cd_sector_check (+ .part.0). Returns 1 for a malformed sector, otherwise 0 or an OR of
 * 2 (EDC mismatch) and 4 (P/Q parity mismatch). Audio and Mode 0 sectors give 1. */
int mh_cd_sector_check(const uint8_t *sector);
/* orig: cd_sector_recode. Repairs the EDC (only if wrong) and recomputes the ECC in place.
 * Returns what mh_cd_sector_check returned BEFORE the repair (so 0 = untouched, 1 = malformed
 * and untouched). Form 2 sectors only get their EDC fixed. */
int mh_cd_sector_recode(uint8_t *sector);
/* 0 malformed, 1 Mode 1, 2 Mode 2 Form 1, 3 Mode 2 Form 2 (same validation as the check). */
int mh_cd_sector_kind(const uint8_t *sector);

/* orig: ecc. Recomputes the P/Q parity at 0x81C..0x92F in place. For Mode 2 (mode byte 2) the
 * four header bytes are zeroed while computing and restored afterwards. */
void mh_cd_ecc(uint8_t *sector);
/* orig: repair_sector. Mode 2 Form 1 only, no checks at all: stores EDC(0x010..0x817) at
 * 0x818 and recomputes P/Q with the header treated as zero. Used after patching a data
 * sector (menu defaults). */
void mh_cd_repair_sector(uint8_t *sector);
/* The sector EDC: CRC with polynomial 0xD8018001 (reflected), initial value 0, no final xor. */
uint32_t mh_cd_edc(const uint8_t *data, size_t len);

/* Table access for the tests: edc[256], ecc forward[256] (xtime) and backward[256]. */
const uint32_t *mh_cd_edc_table(void);
const uint8_t *mh_cd_ecc_forward_table(void);
const uint8_t *mh_cd_ecc_backward_table(void);

/* orig: validate_track_sector. Validates the V2 image "track map" raw sector (magic
 * "PSTTRK1", 2352 bytes) against the disc length in sectors:
 *   +0x00 "PSTTRK1"  +0x08 u8 track count 1..99  +0x09 u8 == 0x10  +0x0A u8 flags
 *   +0x0C u32 CRC-32 of all 2352 bytes with this field zeroed
 *   +0x10 entries of 16 bytes: u32 start, u32 index1 (start <= index1 < start+length), u32 length,
 *         u8 type 0..2 (entry 0: start 0, index1 0, type != 0; later entries start where the
 *         previous one ended; every entry ends within total_sectors)
 *   flags bit 0: extra block at +0x640: u16 length (4..0x2E8), u32 CRC-32 at +0x644 of
 *         the length bytes at +0x648 which begin with "SBI\0"
 * and the last entry must end exactly at total_sectors. Little endian throughout. */
bool mh_track_sector_valid(const uint8_t *sector, uint32_t total_sectors);

#ifdef __cplusplus
}
#endif
#endif
