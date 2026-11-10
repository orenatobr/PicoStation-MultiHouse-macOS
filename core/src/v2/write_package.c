/* Package metadata of one disc (orig: build_package_metadata.constprop.0). */
#include "write_internal.h"

#include <stdlib.h>

/* The 0x6400 byte head of a package: track map "PSTTRK1" (+0), cheat blob (+0xA00, up to
 * 0x2800 bytes, blocks 5..0x18), the reserved 0xA00 "extra" slot (blocks 0x19..0x1D, zero,
 * record field extra_lba) and the Disc art (+0x3C00 = block 0x1E, exactly 0x2800 bytes). */
#define TRK_FAIL "Could not build this Disc's Track Table."

static bool sbi_ok(const uint8_t *p, uint64_t len)
{
    return len >= 4 && len - 4 < 0x2e5 && memcmp(p, MH_V2_SBI_MAGIC, 4) == 0;
}

/* orig: build_package_metadata.constprop.0 @0x140002a70 */
bool mh_v2_build_package_metadata(const MhDisc *d, uint8_t rec[MH_V2_DISC_SIZE],
                                  uint8_t out[MH_V2_PKG_META_BYTES], char *err, size_t errn)
{
    uint8_t trk[MH_V2_SECTOR];
    uint8_t *file_sbi = NULL;
    const uint8_t *sbi = NULL;
    uint64_t sbi_len = 0;
    unsigned n, i;
    uint16_t trk_flags = 0;

    if (err && errn) err[0] = 0;
    if (!out || !rec || !d) {
        mh_str_copy(err, errn, "Invalid package metadata.");
        return false;
    }
    memset(out, 0, MH_V2_PKG_META_BYTES);
    memset(trk, 0, sizeof trk);
    memcpy(trk, MH_V2_TRK_MAGIC, 7);
    trk[9] = MH_V2_TRK_ENTRY_SIZE;
    trk[8] = (uint8_t)d->ntracks;
    n = d->ntracks;
    for (i = 0; i < n; i++) {
        const MhTrack *t = &d->tracks[i];
        uint32_t begin, next_begin;
        uint8_t type, kind_bits, flag_bits;
        MhV2TrackEntry e;
        begin = t->start >= t->pregap ? t->start - t->pregap : 0;
        if (i + 1 < n) {
            const MhTrack *nt = &d->tracks[i + 1];
            if (nt->start < nt->pregap) goto bad;
            next_begin = nt->start - nt->pregap;
            if (t->start < begin || next_begin <= begin || t->number != i + 1 ||
                d->total_sectors < next_begin)
                goto bad;
        } else {
            next_begin = d->total_sectors;
            if (t->start < begin || next_begin <= begin || t->number != i + 1) goto bad;
        }
        if (t->kind == 1) {
            type = 0;
            kind_bits = 0;
        } else {
            type = t->mode;
            if (type != 1 && type != 2) goto bad;
            kind_bits = t->kind == 2 ? 4 : 0;
        }
        kind_bits |= (uint8_t)((t->flags << 1) & 2);
        flag_bits = (uint8_t)(((t->flags << 1) & 6) | ((t->flags >> 2) & 1));
        e.start = begin;
        e.index1 = t->start;
        e.length = next_begin - begin;
        e.type = type;
        e.kind_bits = kind_bits;
        e.flag_bits = flag_bits;
        mh_v2_trk_entry_encode(&e, trk + MH_V2_TRK_ENTRY_OFF + 16u * i);
    }
    /* the first track starts the disc and is a data track */
    if (mh_v2_rd32(trk + 0x10) != 0 || mh_v2_rd32(trk + 0x14) != 0 || trk[0x1c] == 0) goto bad;

    if (d->sbi_data && d->sbi_len != 0) {
        sbi = d->sbi_data;
        sbi_len = d->sbi_len;
    } else if (d->sbi_path[0]) {
        if (!mh_read_entire_file(d->sbi_path, &file_sbi, &sbi_len)) {
            free(file_sbi);
            goto bad;
        }
        sbi = file_sbi;
    }
    if (sbi) {
        if (!sbi_ok(sbi, sbi_len)) {
            free(file_sbi);
            goto bad;
        }
        trk_flags |= MH_V2_TRK_F_SBI;
        mh_v2_wr16(trk + MH_V2_TRK_SBI_OFF, (uint16_t)sbi_len);
        mh_v2_wr32(trk + MH_V2_TRK_SBI_OFF + 4, mh_crc32(sbi, (size_t)sbi_len));
        memcpy(trk + MH_V2_TRK_SBI_OFF + 8, sbi, (size_t)sbi_len);
    }
    free(file_sbi);
    mh_v2_wr16(trk + 0xa, trk_flags);
    mh_v2_wr32(trk + 0xc, 0);
    mh_v2_wr32(trk + 0xc, mh_crc32(trk, sizeof trk));
    /* the firmware (FUN_10022084) and the loader refuse a map whose INDEX 01 is not inside its
     * own track (a pregap of the next track reaching back past it): never write one */
    if (!mh_track_sector_valid(trk, d->total_sectors)) goto bad;
    memcpy(out, trk, sizeof trk);

    mh_v2_wr32(rec + 0x6c, 0);
    if (d->cheats && d->cheats->count != 0) {
        uint8_t *blob = NULL;
        uint32_t len = 0;
        if (!mh_cheat_profile_serialize(d->cheats, 2, &blob, &len, err, errn) ||
            len > MH_V2_PKG_CHEAT_BYTES) {
            mh_free(blob);
            if (!err || err[0] == 0) mh_str_copy(err, errn, "This Disc's cheats exceed the fixed area.");
            return false;
        }
        if (blob && len) memcpy(out + 0xa00, blob, len);
        mh_v2_wr32(rec + 0x6c, len);
        mh_free(blob);
    }
    if (d->cd_art_path[0]) {
        uint8_t *art = NULL;
        uint64_t asz = 0;
        if (!mh_read_entire_file(d->cd_art_path, &art, &asz) || asz != MH_V2_ART_SIZE) {
            free(art);
            mh_str_copy(err, errn, "The Disc art is not exactly 10,240 bytes.");
            return false;
        }
        memcpy(out + 0x3c00, art, MH_V2_ART_SIZE);
        free(art);
    }
    return true;
bad:
    mh_str_copy(err, errn, TRK_FAIL);
    return false;
}
