/* Tests of the V2 on-card format helpers. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "mh/base.h"
#include "mh/v2format.h"
#include "v2_stubs.inc"


/* ---- long names extension (docs/LONG_NAMES.md) ------------------------------------------- */

static void fill_name(char *dst, size_t n, char first)
{
    size_t i;
    for (i = 0; i < n; i++) dst[i] = (char)(i % 10 ? 'a' + (int)(i % 26) : first);
    dst[n] = 0;
}

static void test_long_names(void)
{
    uint8_t d[MH_V2_DISC_SIZE], c[MH_V2_CASE_SIZE], x[MH_V2_DISC_SIZE];
    MhV2DiscRec r, o;
    MhV2CaseRec cr, co;
    char name[100], base[MH_V2_TITLE_MAX], ext[MH_V2_LNX_EXT_MAX + 1], out[80];
    unsigned i, n;

    /* split: 31 and shorter have no extension, 32..71 do, 72+ is cut at 71 */
    static const unsigned lens[] = {0, 1, 30, 31, 32, 40, 71, 72, 90};
    for (i = 0; i < sizeof lens / sizeof *lens; i++) {
        unsigned want = lens[i] > 71 ? 71 : lens[i];
        fill_name(name, lens[i], 'X');
        n = mh_v2_name_split(name, base, ext);
        assert(n == (want > 31 ? want - 31 : 0));
        assert(strlen(base) == (want > 31 ? 31 : want) && strlen(ext) == n);
        assert(strncmp(base, name, strlen(base)) == 0 && strncmp(ext, name + 31, n) == 0);
    }
    n = mh_v2_name_split("A(B)\x01 C", base, ext);
    assert(n == 0 && strcmp(base, "A(B)\x01 C") == 0); /* short names are left alone */

    /* a Disc record with title and case extension, no extension in the legacy form */
    memset(&r, 0, sizeof r);
    r.used = 1; r.id = 3; r.base_lba = 0x2800; r.sectors = 10; r.flags = 2;
    fill_name(name, 71, 'T');
    mh_v2_name_split(name, r.title, r.title_ext);
    r.has_ext = true; r.title_ext_len = 40; r.case_ext_len = 12;
    memcpy(r.case_ext, "0123456789AB", 12);
    mh_v2_disc_encode(&r, d);
    assert(memcmp(d + 0x94, "LNX1", 4) == 0 && d[0x98] == 40 && d[0x99] == 12);
    assert(memcmp(d + 0x9a, name + 31, 40) == 0 && memcmp(d + 0xc2, "0123456789AB", 12) == 0);
    assert(d[0xc2 + 12] == 0 && d[0xea] == 0 && d[0xfb] == 0);
    mh_v2_disc_decode(d, &o, NULL);
    assert(o.has_ext && o.title_ext_len == 40 && o.case_ext_len == 12 && strlen(o.title_ext) == 40);
    assert(strcmp(o.case_ext, "0123456789AB") == 0);
    memset(c, 0, sizeof c);
    memset(&cr, 0, sizeof cr);
    cr.used = 1; cr.disc_count = 1; cr.disc_ids[0] = 3; cr.long_name = 1;
    memset(cr.name, 'N', 31);
    mh_v2_case_encode(&cr, c);
    assert(c[0x3f] == 1 && mh_v2_case_ext_valid(c));
    mh_v2_case_decode(c, &co, NULL);
    assert(co.long_name == 1);
    assert(mh_v2_disc_ext_valid(d, c));
    mh_v2_disc_full_title(d, out, sizeof out);
    assert(strcmp(out, name) == 0);
    mh_v2_case_full_name(c, d, out, sizeof out);
    assert(strlen(out) == 43 && strncmp(out + 31, "0123456789AB", 12) == 0);
    /* the reader falls back to the base name when the Disc cannot supply the extension */
    mh_v2_case_full_name(c, NULL, out, sizeof out);
    assert(strlen(out) == 31);
    memcpy(x, d, sizeof x); x[7] = 1;
    mh_v2_case_full_name(c, x, out, sizeof out);
    assert(strlen(out) == 31);
    memcpy(x, d, sizeof x); x[0x99] = 0;
    mh_v2_case_full_name(c, x, out, sizeof out);
    assert(strlen(out) == 31);

    /* every rule of the spec makes the Disc record invalid */
#define BAD(stmt) do { memcpy(x, d, sizeof x); stmt; assert(!mh_v2_disc_ext_valid(x, c)); } while (0)
#define GOOD(stmt) do { memcpy(x, d, sizeof x); stmt; assert(mh_v2_disc_ext_valid(x, c)); } while (0)
    /* without the magic every byte in +0x94..+0xFB must be zero */
    for (i = 0x94; i < 0xfc; i++) {
        memset(x, 0, sizeof x);
        assert(mh_v2_disc_ext_valid(x, NULL));
        x[i] = 1;
        assert(!mh_v2_disc_ext_valid(x, NULL));
    }
    memset(x, 0, sizeof x);
    memcpy(x + 0x94, "LNX2", 4);
    assert(!mh_v2_disc_ext_valid(x, NULL));
    BAD(x[0x98] = 41);                         /* title_ext_len > 40 */
    BAD(x[0x99] = 41);                         /* case_ext_len > 40 */
    BAD((x[0x9a + 40 - 1] = 'z', x[0x98] = 39)); /* byte after title_ext_len not zero */
    BAD(x[0xc2 + 12] = 'z');                   /* byte after case_ext_len not zero */
    BAD(x[0x9a] = 0x1f);                       /* control character in title_ext */
    BAD(x[0x9a] = 0x7f);                       /* 0x7F is not printable */
    BAD(x[0xc2] = 0x80);                       /* high bit in case_ext */
    BAD(x[0xea] = 1);                          /* trailing reserved bytes */
    BAD(x[0xfb] = 1);
    BAD(x[7] = 1);                             /* case_ext_len only in slot 0 */
    BAD(x[0x1c + 5] = 0);                      /* title field shorter than 31 characters */
    BAD(x[0x1c + 31] = 'A');                   /* title field not NUL terminated */
    BAD(x[0x1c + 3] = 0x19);                   /* non printable in the base */
    GOOD(x[7] = 0);
    GOOD((x[0x99] = 0, memset(x + 0xc2, 0, 40)));
    GOOD((x[0x98] = 0, memset(x + 0x9a, 0, 40))); /* length 0 does not constrain the title field */
    GOOD((x[0x98] = 0, memset(x + 0x9a, 0, 40), x[0x1c + 5] = 0));
    {
        uint8_t cc[MH_V2_CASE_SIZE];                /* case_ext_len needs a 31 character Case name field */
        memcpy(cc, c, sizeof cc);
        cc[8 + 4] = 0;
        assert(!mh_v2_disc_ext_valid(d, cc));
        cc[8 + 4] = 'N';
        cc[8 + 31] = 'N';
        assert(!mh_v2_disc_ext_valid(d, cc));
        assert(mh_v2_disc_ext_valid(d, NULL));
    }
    /* Case record: flag 0 or 1 only, +0x40..+0x4B zero, 31 character name when flagged */
    assert(mh_v2_case_ext_valid(c));
    memcpy(x, c, MH_V2_CASE_SIZE); x[0x3f] = 2; assert(!mh_v2_case_ext_valid(x));
    memcpy(x, c, MH_V2_CASE_SIZE); x[0x3f] = 0xff; assert(!mh_v2_case_ext_valid(x));
    for (i = 0x40; i < 0x4c; i++) { memcpy(x, c, MH_V2_CASE_SIZE); x[i] = 1; assert(!mh_v2_case_ext_valid(x)); }
    memcpy(x, c, MH_V2_CASE_SIZE); x[8 + 10] = 0; assert(!mh_v2_case_ext_valid(x));
    memcpy(x, c, MH_V2_CASE_SIZE); x[8 + 31] = 'A'; assert(!mh_v2_case_ext_valid(x));
    memcpy(x, c, MH_V2_CASE_SIZE); x[8] = 0x1f; assert(!mh_v2_case_ext_valid(x));
    memcpy(x, c, MH_V2_CASE_SIZE); x[0x3f] = 0; x[8 + 10] = 0; assert(mh_v2_case_ext_valid(x)); /* a short name, no flag */

    /* corrupt cards still decode to terminated strings */
    memset(d + 0x9a, 'Q', 80);
    mh_v2_disc_decode(d, &o, NULL);
    assert(strlen(o.title_ext) == 40 && strlen(o.case_ext) == 40);
    /* a legacy record (no extension) encodes the reserved bytes as zero */
    memset(&r, 0, sizeof r);
    r.used = 1; strcpy(r.title, "Short");
    mh_v2_disc_encode(&r, d);
    for (i = 0x94; i < 0xfc; i++) assert(d[i] == 0);
    mh_v2_case_encode(&(MhV2CaseRec){.used = 1, .disc_count = 1}, c);
    for (i = 0x3f; i < 0x4c; i++) assert(c[i] == 0);
}

int main(void)
{
    uint8_t slot[MH_V2_HDR_SLOT], rec[MH_V2_DISC_SIZE], crec[MH_V2_CASE_SIZE];
    MhV2Header h, d;
    MhV2DiscRec dr, dd;
    MhV2CaseRec cr, cd;
    bool crc_ok;

    memset(&h, 0, sizeof h);
    h.flags = MH_V2_HF_BASE | MH_V2_HF_MBR;
    h.case_count = 3; h.disc_count = 4; h.generation = 7; h.menu_sectors = 93;
    h.crc_cases = 0x11111111; h.mbr_sectors = mh_v2_mbr_sectors(1000);
    mh_v2_header_encode(&h, slot);
    assert(mh_v2_header_valid(slot) && mh_v2_header_raw_valid(slot));
    assert(slot[0x1fe] == 0x55 && slot[0x1ff] == 0xaa && mh_v2_rd32(slot + 0x1b8) == (7 ^ 0x32545350u));
    mh_v2_header_decode(slot, &d);
    assert(d.case_count == 3 && d.disc_count == 4 && d.generation == 7 && d.menu_sectors == 93 && d.mbr_sectors == 999);
    assert(mh_v2_mbr_sectors(1ull << 40) == 0xFFFFFFFFu);
    /* every single byte flip inside the CRC range invalidates the header */
    for (unsigned i = 0; i < MH_V2_HDR_SIZE; i++) {
        slot[i] ^= 0x40;
        assert(!mh_v2_header_valid(slot));
        slot[i] ^= 0x40;
    }
    slot[0x940] = 1;
    assert(mh_v2_header_valid(slot) && !mh_v2_header_raw_valid(slot));
    slot[0x940] = 0;
    mh_v2_header_recovered(2, 2, 93, 1, 2, slot);
    assert(mh_v2_header_valid(slot) && slot[7] == 1 && mh_v2_rd32(slot + 12) == 1);

    /* records with unterminated strings must still decode to terminated strings */
    memset(&cr, 0, sizeof cr);
    cr.used = 1; cr.disc_count = 2; cr.index = 5; cr.art_id = 5; cr.disc_ids[0] = 1; cr.disc_ids[1] = 2;
    strcpy(cr.name, "GAME"); strcpy(cr.code, "SLUS-00001");
    mh_v2_case_encode(&cr, crec);
    memset(crec + 8, 'A', MH_V2_CASE_NAME_MAX);
    memset(crec + 0x28, 'B', MH_V2_CASE_CODE_MAX);
    mh_v2_case_decode(crec, &cd, &crc_ok);
    assert(!crc_ok && strlen(cd.name) == MH_V2_CASE_NAME_MAX && strlen(cd.code) == MH_V2_CASE_CODE_MAX);
    mh_v2_case_encode(&cd, crec); /* bounded: 31 / 15 characters */
    mh_v2_case_decode(crec, &cd, &crc_ok);
    assert(crc_ok && strlen(cd.name) == 31 && strlen(cd.code) == 15 && cd.index == 5);

    memset(&dr, 0, sizeof dr);
    dr.used = 1; dr.id = 9; dr.base_lba = 0x2800; dr.sectors = 10; dr.flags = 2;
    strcpy(dr.serial, "SLUS-00001"); strcpy(dr.title, "T");
    mh_v2_disc_encode(&dr, rec);
    memset(rec + 0xc, 'S', MH_V2_SERIAL_MAX);
    memset(rec + 0x1c, 'T', MH_V2_TITLE_MAX);
    mh_v2_disc_decode(rec, &dd, &crc_ok);
    assert(!crc_ok && strlen(dd.serial) == MH_V2_SERIAL_MAX && strlen(dd.title) == MH_V2_TITLE_MAX);
    mh_v2_disc_encode(&dd, rec);
    mh_v2_disc_decode(rec, &dd, &crc_ok);
    assert(crc_ok && strlen(dd.serial) == 15 && strlen(dd.title) == 31 && dd.base_lba == 0x2800);

    {
        uint8_t cfg[MH_V2_SLOT], blk[512];
        MhV2Resume a, b;
        mh_v2_cfg_encode(1234, 0xabcd, cfg);
        assert(mh_v2_cfg_valid(cfg));
        memset(&a, 0, sizeof a);
        a.stamp = 5; a.has_verify = true; a.verify_stamp = 0x100000003ull; memcpy(a.disc_rec, rec, sizeof rec);
        mh_v2_resume_encode(&a, blk);
        assert(mh_v2_resume_decode(blk, &b) && b.has_verify && b.verify_stamp == a.verify_stamp);
        blk[100] ^= 1;
        assert(!mh_v2_resume_decode(blk, &b));
    }
    test_long_names();
    puts("v2 format ok");
    return 0;
}
