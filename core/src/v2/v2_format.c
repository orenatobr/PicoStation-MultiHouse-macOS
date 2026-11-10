/* v2_format.c: encode/decode of the V2 on-card structures (see mh/v2format.h). */
#include <string.h>

#include "mh/base.h"
#include "mh/v2format.h"

/* ---- header ------------------------------------------------------------------------------ */

/* orig: v2_header_valid @0x140036930 */
bool mh_v2_header_valid(const uint8_t *h)
{
    if (memcmp(h, MH_V2_MAGIC, 6) != 0) return false;
    if (h[6] != MH_V2_VERSION) return false;
    if (!(h[7] & MH_V2_HF_BASE)) return false;
    if (mh_v2_rd16(h + 8) > 0x100 || mh_v2_rd16(h + 10) > 0x100) return false;
    if (mh_v2_rd32(h + 0x10) != MH_V2_HDR_UNIT) return false;
    if (mh_v2_rd64(h + 0x14) != MH_V2_HDR_B_LBA) return false;
    if (mh_v2_rd64(h + 0x1c) != MH_V2_HDR_CASE_LBA) return false;
    if (mh_v2_rd64(h + 0x24) != MH_V2_HDR_DISC_LBA) return false;
    if (mh_v2_rd64(h + 0x2c) != MH_V2_HDR_ART_LBA) return false;
    if (mh_v2_rd64(h + 0x34) != MH_V2_MENU_LBA) return false;
    if (mh_v2_rd32(h + 0x3c) - 1u >= 0x5d) return false;
    if (mh_v2_rd64(h + 0x40) != MH_V2_CFG_LBA) return false;
    if (mh_v2_rd64(h + 0x48) != MH_V2_ART_SIZE) return false;
    if (h[7] & MH_V2_HF_MBR) {
        if (h[0x1fe] != 0x55 || h[0x1ff] != 0xaa) return false;
        if (h[0x1be] != 0 || h[0x1c2] != MH_V2_MBR_TYPE) return false;
        if (mh_v2_rd32(h + 0x1c6) != 1 || mh_v2_rd32(h + 0x1ca) == 0) return false;
    }
    return mh_v2_rd32(h + MH_V2_HDR_CRC_OFF) == mh_crc_record(h, MH_V2_HDR_SIZE);
}

/* orig: v2_header_raw_valid @0x1400368e0 */
bool mh_v2_header_raw_valid(const uint8_t *h)
{
    unsigned i;
    if (!mh_v2_header_valid(h)) return false;
    for (i = MH_V2_HDR_SIZE; i < MH_V2_HDR_SLOT; i++)
        if (h[i]) return false;
    return true;
}

void mh_v2_header_decode(const uint8_t *h, MhV2Header *o)
{
    memset(o, 0, sizeof *o);
    o->version = h[6];
    o->flags = h[7];
    o->case_count = mh_v2_rd16(h + 8);
    o->disc_count = mh_v2_rd16(h + 10);
    o->generation = mh_v2_rd32(h + 12);
    o->unit = mh_v2_rd32(h + 0x10);
    o->b_lba = mh_v2_rd64(h + 0x14);
    o->case_lba = mh_v2_rd64(h + 0x1c);
    o->disc_lba = mh_v2_rd64(h + 0x24);
    o->art_lba = mh_v2_rd64(h + 0x2c);
    o->menu_lba = mh_v2_rd64(h + 0x34);
    o->menu_sectors = mh_v2_rd32(h + 0x3c);
    o->cfg_lba = mh_v2_rd64(h + 0x40);
    o->art_size = mh_v2_rd64(h + 0x48);
    o->crc_cases = mh_v2_rd32(h + 0x50);
    o->crc_discs = mh_v2_rd32(h + 0x54);
    o->crc_art = mh_v2_rd32(h + 0x58);
    o->crc_menu = mh_v2_rd32(h + 0x5c);
    memcpy(o->menu_sha1, h + 0x60, 20);
    o->mbr_signature = mh_v2_rd32(h + 0x1b8);
    o->mbr_sectors = mh_v2_rd32(h + 0x1ca);
}

uint32_t mh_v2_mbr_sectors(uint64_t card_blocks)
{
    return (card_blocks - 1 < 0x100000000ull) ? (uint32_t)(card_blocks - 1) : 0xFFFFFFFFu;
}

/* orig: layout written by v2_write_handle_direct (header at stack +0x790) */
void mh_v2_header_encode(const MhV2Header *in, uint8_t out[MH_V2_HDR_SLOT])
{
    memset(out, 0, MH_V2_HDR_SLOT);
    memcpy(out, MH_V2_MAGIC, 6);
    out[6] = MH_V2_VERSION;
    out[7] = in->flags;
    mh_v2_wr16(out + 8, in->case_count);
    mh_v2_wr16(out + 10, in->disc_count);
    mh_v2_wr32(out + 12, in->generation);
    mh_v2_wr32(out + 0x10, MH_V2_HDR_UNIT);
    mh_v2_wr64(out + 0x14, MH_V2_HDR_B_LBA);
    mh_v2_wr64(out + 0x1c, MH_V2_HDR_CASE_LBA);
    mh_v2_wr64(out + 0x24, MH_V2_HDR_DISC_LBA);
    mh_v2_wr64(out + 0x2c, MH_V2_HDR_ART_LBA);
    mh_v2_wr64(out + 0x34, MH_V2_MENU_LBA);
    mh_v2_wr32(out + 0x3c, in->menu_sectors);
    mh_v2_wr64(out + 0x40, MH_V2_CFG_LBA);
    mh_v2_wr64(out + 0x48, MH_V2_ART_SIZE);
    mh_v2_wr32(out + 0x50, in->crc_cases);
    mh_v2_wr32(out + 0x54, in->crc_discs);
    mh_v2_wr32(out + 0x58, in->crc_art);
    mh_v2_wr32(out + 0x5c, in->crc_menu);
    memcpy(out + 0x60, in->menu_sha1, 20);
    if (in->flags & MH_V2_HF_MBR) {
        static const uint8_t ent[8] = {0x00, 0x00, 0x02, 0x00, MH_V2_MBR_TYPE, 0xfe, 0xff, 0xff};
        mh_v2_wr32(out + 0x1b8, in->generation ^ MH_V2_MBR_SIG_XOR);
        memcpy(out + 0x1be, ent, 8);
        mh_v2_wr32(out + 0x1c6, 1);
        mh_v2_wr32(out + 0x1ca, in->mbr_sectors);
        out[0x1fe] = 0x55;
        out[0x1ff] = 0xaa;
    }
    mh_v2_wr32(out + MH_V2_HDR_CRC_OFF, mh_crc_record(out, MH_V2_HDR_SIZE));
}

/* orig: header rebuilt by recover_v2_header_from_body @0x14002bf00 */
void mh_v2_header_recovered(uint16_t cases, uint16_t discs, uint32_t menu_sectors,
                            uint32_t crc_cases, uint32_t crc_discs, uint8_t out[MH_V2_HDR_SLOT])
{
    MhV2Header h;
    memset(&h, 0, sizeof h);
    h.flags = MH_V2_HF_BASE;
    h.case_count = cases;
    h.disc_count = discs;
    h.generation = 1;
    h.menu_sectors = menu_sectors;
    h.crc_cases = crc_cases;
    h.crc_discs = crc_discs;
    mh_v2_header_encode(&h, out);
}

/* ---- Case record ------------------------------------------------------------------------- */

bool mh_v2_case_magic(const uint8_t r[MH_V2_CASE_SIZE]) { return memcmp(r, MH_V2_CASE_MAGIC, 4) == 0; }

void mh_v2_case_decode(const uint8_t r[MH_V2_CASE_SIZE], MhV2CaseRec *o, bool *crc_ok)
{
    memset(o, 0, sizeof *o);
    o->used = r[4];
    o->disc_count = r[5];
    o->index = r[6];
    o->variant = r[7];
    memcpy(o->name, r + 8, MH_V2_CASE_NAME_MAX);
    memcpy(o->code, r + 0x28, MH_V2_CASE_CODE_MAX);
    o->art_id = mh_v2_rd16(r + 0x38);
    memcpy(o->disc_ids, r + 0x3a, 5);
    o->crc = mh_v2_rd32(r + 0x4c);
    if (crc_ok) *crc_ok = (o->crc == mh_crc_record(r, MH_V2_CASE_SIZE));
}

void mh_v2_case_encode(const MhV2CaseRec *in, uint8_t r[MH_V2_CASE_SIZE])
{
    memset(r, 0, MH_V2_CASE_SIZE);
    memcpy(r, MH_V2_CASE_MAGIC, 4);
    r[4] = in->used;
    r[5] = in->disc_count;
    r[6] = in->index;
    r[7] = in->variant;
    mh_str_copy((char *)r + 8, MH_V2_CASE_NAME_MAX, in->name);
    mh_str_copy((char *)r + 0x28, MH_V2_CASE_CODE_MAX, in->code);
    mh_v2_wr16(r + 0x38, in->art_id);
    memcpy(r + 0x3a, in->disc_ids, 5);
    mh_v2_wr32(r + 0x4c, mh_crc_record(r, MH_V2_CASE_SIZE));
}

/* ---- Disc record ------------------------------------------------------------------------- */

bool mh_v2_disc_magic(const uint8_t r[MH_V2_DISC_SIZE]) { return memcmp(r, MH_V2_DISC_MAGIC, 4) == 0; }

void mh_v2_disc_decode(const uint8_t r[MH_V2_DISC_SIZE], MhV2DiscRec *o, bool *crc_ok)
{
    memset(o, 0, sizeof *o);
    o->used = r[4];
    o->id = r[5];
    o->case_index = r[6];
    o->slot = r[7];
    o->flags = mh_v2_rd32(r + 8);
    memcpy(o->serial, r + 0xc, MH_V2_SERIAL_MAX);
    memcpy(o->title, r + 0x1c, MH_V2_TITLE_MAX);
    o->base_lba = mh_v2_rd64(r + 0x3c);
    o->track_lba = mh_v2_rd64(r + 0x44);
    o->raw_lba = mh_v2_rd64(r + 0x4c);
    o->raw_blocks = mh_v2_rd64(r + 0x54);
    o->sectors = mh_v2_rd32(r + 0x5c);
    o->track_count = r[0x60];
    o->bin_count = r[0x61];
    o->art_id = mh_v2_rd16(r + 0x62);
    o->cheat_lba = mh_v2_rd64(r + 0x64);
    o->cheat_size = mh_v2_rd32(r + 0x6c);
    o->extra_lba = mh_v2_rd64(r + 0x70);
    o->extra_size = mh_v2_rd32(r + 0x78);
    o->crc32 = mh_v2_rd32(r + 0x7c);
    memcpy(o->sha1, r + 0x80, 20);
    o->crc = mh_v2_rd32(r + 0xfc);
    if (crc_ok) *crc_ok = (o->crc == mh_crc_record(r, MH_V2_DISC_SIZE));
}

void mh_v2_disc_encode(const MhV2DiscRec *in, uint8_t r[MH_V2_DISC_SIZE])
{
    memset(r, 0, MH_V2_DISC_SIZE);
    memcpy(r, MH_V2_DISC_MAGIC, 4);
    r[4] = in->used;
    r[5] = in->id;
    r[6] = in->case_index;
    r[7] = in->slot;
    mh_v2_wr32(r + 8, in->flags);
    mh_str_copy((char *)r + 0xc, MH_V2_SERIAL_MAX, in->serial);
    mh_str_copy((char *)r + 0x1c, MH_V2_TITLE_MAX, in->title);
    mh_v2_wr64(r + 0x3c, in->base_lba);
    mh_v2_wr64(r + 0x44, in->track_lba);
    mh_v2_wr64(r + 0x4c, in->raw_lba);
    mh_v2_wr64(r + 0x54, in->raw_blocks);
    mh_v2_wr32(r + 0x5c, in->sectors);
    r[0x60] = in->track_count;
    r[0x61] = in->bin_count;
    mh_v2_wr16(r + 0x62, in->art_id);
    mh_v2_wr64(r + 0x64, in->cheat_lba);
    mh_v2_wr32(r + 0x6c, in->cheat_size);
    mh_v2_wr64(r + 0x70, in->extra_lba);
    mh_v2_wr32(r + 0x78, in->extra_size);
    mh_v2_wr32(r + 0x7c, in->crc32);
    memcpy(r + 0x80, in->sha1, 20);
    mh_v2_wr32(r + 0xfc, mh_crc_record(r, MH_V2_DISC_SIZE));
}

/* ---- track map entries ------------------------------------------------------------------- */

void mh_v2_trk_entry_decode(const uint8_t e[16], MhV2TrackEntry *o)
{
    o->start = mh_v2_rd32(e);
    o->index1 = mh_v2_rd32(e + 4);
    o->length = mh_v2_rd32(e + 8);
    o->type = e[0xc];
    o->kind_bits = e[0xd];
    o->flag_bits = e[0xe];
}

void mh_v2_trk_entry_encode(const MhV2TrackEntry *in, uint8_t e[16])
{
    memset(e, 0, 16);
    mh_v2_wr32(e, in->start);
    mh_v2_wr32(e + 4, in->index1);
    mh_v2_wr32(e + 8, in->length);
    e[0xc] = in->type;
    e[0xd] = in->kind_bits;
    e[0xe] = in->flag_bits;
}

/* ---- menu config ------------------------------------------------------------------------- */

bool mh_v2_cfg_valid(const uint8_t *c)
{
    if (memcmp(c, MH_V2_CFG_MAGIC, 7) != 0 || mh_v2_rd16(c + 8) != 1) return false;
    return mh_v2_rd32(c + MH_V2_CFG_CRC_OFF) == mh_crc32(c, MH_V2_CFG_CRC_OFF);
}

void mh_v2_cfg_decode(const uint8_t *c, MhV2Config *o)
{
    o->version = mh_v2_rd16(c + 8);
    o->version2 = mh_v2_rd16(c + 10);
    o->music_lba = mh_v2_rd64(c + 0x1c);
    o->music_size = mh_v2_rd32(c + 0x24);
    o->music_crc = mh_v2_rd32(c + 0x28);
}

/* orig: PSTCFG2 built in v2_write_handle_direct */
void mh_v2_cfg_encode(uint32_t music_size, uint32_t music_crc, uint8_t out[MH_V2_SLOT])
{
    static const char text[] = MH_V2_CFG_TEXT;
    memset(out, 0, MH_V2_SLOT);
    memcpy(out, MH_V2_CFG_MAGIC, 7);
    mh_v2_wr16(out + 8, 1);
    mh_v2_wr16(out + 10, 1);
    mh_v2_wr64(out + 0x1c, MH_V2_MUSIC_LBA);
    mh_v2_wr32(out + 0x24, music_size);
    mh_v2_wr32(out + 0x28, music_crc);
    memcpy(out + MH_V2_CFG_TEXT_OFF, text, MH_V2_CFG_TEXT_LEN); /* 0x4c: includes the NUL */
    mh_v2_wr32(out + MH_V2_CFG_CRC_OFF, mh_crc32(out, MH_V2_CFG_CRC_OFF));
}

/* ---- resume block ------------------------------------------------------------------------ */

void mh_v2_resume_encode(const MhV2Resume *in, uint8_t b[512])
{
    memset(b, 0, 512);
    memcpy(b, MH_V2_RSM_MAGIC, 8);
    mh_v2_wr64(b + MH_V2_RSM_TIME_OFF, in->stamp);
    memcpy(b + MH_V2_RSM_REC_OFF, in->disc_rec, MH_V2_DISC_SIZE);
    if (in->has_verify) {
        memcpy(b + MH_V2_RSM_VFY_OFF, MH_V2_VFY_MAGIC, 8);
        mh_v2_wr64(b + MH_V2_RSM_VFY_VAL_OFF, in->verify_stamp);
    }
    mh_v2_wr32(b + MH_V2_RSM_CRC_OFF, mh_crc32(b, MH_V2_RSM_CRC_OFF));
}

bool mh_v2_resume_decode(const uint8_t b[512], MhV2Resume *o)
{
    memset(o, 0, sizeof *o);
    if (memcmp(b, MH_V2_RSM_MAGIC, 8) != 0) return false;
    if (mh_v2_rd32(b + MH_V2_RSM_CRC_OFF) != mh_crc32(b, MH_V2_RSM_CRC_OFF)) return false;
    o->stamp = mh_v2_rd64(b + MH_V2_RSM_TIME_OFF);
    memcpy(o->disc_rec, b + MH_V2_RSM_REC_OFF, MH_V2_DISC_SIZE);
    if (memcmp(b + MH_V2_RSM_VFY_OFF, MH_V2_VFY_MAGIC, 8) == 0) {
        o->has_verify = true;
        o->verify_stamp = mh_v2_rd64(b + MH_V2_RSM_VFY_VAL_OFF);
    }
    return true;
}
