/* CD sector EDC/ECC (orig functions in the 0x1400050xx..0x14001xxxx range). */
#include "internal.h"

#include <string.h>

#include "cd_tables.inc"

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

const uint32_t *mh_cd_edc_table(void) { return k_edc_table; }
const uint8_t *mh_cd_ecc_forward_table(void) { return k_ecc_forward; }
const uint8_t *mh_cd_ecc_backward_table(void) { return k_ecc_backward; }

uint32_t mh_cd_edc(const uint8_t *p, size_t len)
{
    uint32_t edc = 0;
    while (len--) edc = (edc >> 8) ^ k_edc_table[(edc ^ *p++) & 0xFF];
    return edc;
}

/* One parity family (P or Q): `major` columns of `minor` bytes walked with stride `inc`
 * (mod major*minor) starting at (m>>1)*mult + (m&1); two parity bytes per column. */
static void ecc_family(const uint8_t *base, unsigned major, unsigned minor, unsigned mult,
                       unsigned inc, uint8_t *dest)
{
    unsigned size = major * minor, m, k;
    for (m = 0; m < major; m++) {
        unsigned idx = (m >> 1) * mult + (m & 1);
        uint8_t a = 0, b = 0;
        for (k = 0; k < minor; k++) {
            uint8_t t = base[idx];
            idx += inc;
            if (idx >= size) idx -= size;
            b ^= t;
            a = k_ecc_forward[t ^ a];
        }
        a = k_ecc_backward[k_ecc_forward[a] ^ b];
        dest[m] = a;
        dest[m + major] = a ^ b;
    }
}

static void ecc_core(uint8_t *s, bool zero_header)
{
    uint8_t saved[4];
    if (zero_header) {
        memcpy(saved, s + 0xC, 4);
        memset(s + 0xC, 0, 4);
    }
    ecc_family(s + 0xC, 86, 24, 2, 86, s + 0x81C);
    ecc_family(s + 0xC, 52, 43, 86, 88, s + 0x8C8);
    if (zero_header) memcpy(s + 0xC, saved, 4);
}

/* orig: ecc @0x140018ab0 */
void mh_cd_ecc(uint8_t *s) { ecc_core(s, s[0xF] == 2); }

/* orig: repair_sector @0x14002d880 */
void mh_cd_repair_sector(uint8_t *s)
{
    wr32(s + 0x818, mh_cd_edc(s + 0x10, 0x808));
    ecc_core(s, true);
}

/* The validation shared by cd_sector_check and cd_sector_recode: sync, mode 1 or 2 and the
 * two copies of the subheader. */
static bool format_ok(const uint8_t *s)
{
    unsigned i;
    if (s[0] != 0 || s[0xB] != 0 || (s[0xF] != 1 && s[0xF] != 2)) return false;
    for (i = 1; i <= 10; i++)
        if (s[i] != 0xFF) return false;
    if (s[0xF] == 2 && memcmp(s + 0x10, s + 0x14, 4) != 0) return false;
    return true;
}

int mh_cd_sector_kind(const uint8_t *s)
{
    if (!format_ok(s)) return 0;
    if (s[0xF] == 1) return 1;
    return (s[0x12] & 0x20) ? 3 : 2;
}

/* orig: cd_sector_check.part.0 @0x1400050f0 (everything after the format test) */
static int check_body(const uint8_t *s)
{
    int kind = mh_cd_sector_kind(s), r;
    size_t start, len, edc_at;
    uint8_t tmp[MH_SECTOR_RAW];
    if (kind == 1) { start = 0; len = 0x810; edc_at = 0x810; }
    else if (kind == 2) { start = 0x10; len = 0x808; edc_at = 0x818; }
    else {
        start = 0x10; len = 0x91C; edc_at = 0x92C;
        if (rd32(s + edc_at) == 0) return 0; /* Form 2 EDC is optional */
    }
    r = mh_cd_edc(s + start, len) != rd32(s + edc_at) ? MH_SECTOR_BAD_EDC : 0;
    if (kind != 3) {
        memcpy(tmp, s, sizeof tmp);
        mh_cd_ecc(tmp);
        if (memcmp(tmp + 0x81C, s + 0x81C, MH_SECTOR_RAW - 0x81C) != 0) r |= MH_SECTOR_BAD_ECC;
    }
    return r;
}

/* orig: cd_sector_check @0x140005050 */
int mh_cd_sector_check(const uint8_t *s)
{
    return format_ok(s) ? check_body(s) : MH_SECTOR_BAD_FORMAT;
}

/* orig: cd_sector_recode @0x1400055d0 */
int mh_cd_sector_recode(uint8_t *s)
{
    int kind = mh_cd_sector_kind(s), r;
    if (kind == 0) return MH_SECTOR_BAD_FORMAT;
    r = check_body(s);
    if (r == 0) return 0;
    if (kind == 3) {
        if (r & MH_SECTOR_BAD_EDC) wr32(s + 0x92C, mh_cd_edc(s + 0x10, 0x91C));
        return r;
    }
    if (r & MH_SECTOR_BAD_EDC) {
        if (kind == 1) wr32(s + 0x810, mh_cd_edc(s, 0x810));
        else wr32(s + 0x818, mh_cd_edc(s + 0x10, 0x808));
    }
    mh_cd_ecc(s);
    return r;
}

/* orig: validate_track_sector @0x14003d7a0 */
bool mh_track_sector_valid(const uint8_t *s, uint32_t total)
{
    uint8_t tmp[MH_SECTOR_RAW];
    uint32_t crc, end = 0, prev_end = 0;
    unsigned i, count;
    if (memcmp(s, "PSTTRK1", 7) != 0 || s[8] < 1 || s[8] > 99 || s[9] != 0x10) return false;
    memcpy(tmp, s, sizeof tmp);
    memset(tmp + 0xC, 0, 4);
    crc = mh_crc32(tmp, sizeof tmp);
    if (rd32(s + 0xC) != crc) return false;
    count = s[8];
    for (i = 0; i < count; i++) {
        const uint8_t *e = s + 0x10 + 16 * i;
        uint32_t start = rd32(e), idx1 = rd32(e + 4), len = rd32(e + 8);
        end = start + len;
        if (len == 0 || end < start || end > total) return false;
        if (idx1 >= end || idx1 < start || e[12] > 2) return false;
        if (i == 0) {
            if (start != 0 || idx1 != 0 || e[12] == 0) return false;
        } else if (start != prev_end) {
            return false;
        }
        prev_end = end;
    }
    if (s[0xA] & 1) {
        unsigned n = s[0x640] | (unsigned)s[0x641] << 8;
        if (n < 4 || n > 0x2E8 || memcmp(s + 0x648, "SBI", 4) != 0) return false;
        if (rd32(s + 0x644) != mh_crc32(s + 0x648, n)) return false;
    }
    return end == total;
}
