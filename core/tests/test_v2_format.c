/* Tests of the V2 on-card format helpers. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "mh/base.h"
#include "mh/v2format.h"
#include "v2_stubs.inc"

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
    puts("v2 format ok");
    return 0;
}
