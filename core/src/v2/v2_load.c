/* v2_load.c: loading a V2 library from an image or a card.
 * orig: v2_load_source @0x140036c10, v2_load_image, v2_load_physical,
 * recover_v2_header_from_body @0x14002bf00, v2_probe_recoverable_physical, cache_name,
 * cache_region. */
#include <stdio.h>
#include <stdlib.h>

#include "v2_internal.h"

/* ---------------------------------------------------------------- recover header */

/* Reads the PSTCFG2 slot like the original: a second read is tried when the first one does
 * not give a valid, zero padded sector. */
static bool read_cfg_slot(MhIo *io, uint8_t cfg[MH_V2_CFG_SIZE])
{
    uint8_t slot[MH_V2_SLOT];
    int attempt;
    for (attempt = 0; attempt < 2; attempt++) {
        unsigned i;
        bool pad = true;
        if (!mh_io_read(io, MH_V2_CFG_OFF, slot, sizeof slot)) {
            if (attempt == 0) continue;
            return false;
        }
        memcpy(cfg, slot, MH_V2_CFG_SIZE);
        for (i = MH_V2_CFG_SIZE; i < MH_V2_SLOT; i++)
            if (slot[i]) pad = false;
        if (pad && mh_v2_cfg_valid(cfg)) return true;
    }
    return false;
}

/* orig: recover_v2_header_from_body */
bool mh_v2_recover_header_from_body(MhIo *io, uint64_t size, const MhV2Env *env,
                                    uint8_t hdr[MH_V2_HDR_SIZE])
{
    uint64_t menu_size;
    uint32_t menu_sectors;
    uint8_t first[512], cfg[MH_V2_CFG_SIZE];
    uint8_t *cases = NULL, *discs = NULL;
    uint8_t used[256], case_of[256], slot_of[256];
    unsigned i, j, ncases = 0, ndiscs = 0;
    bool ok = false;

    if (size < MH_V2_MIN_IMAGE || !env || !env->menu || env->menu_size == 0) return false;
    menu_size = env->menu_size;
    if (menu_size % MH_V2_SECTOR != 0) return false;
    menu_sectors = (uint32_t)(menu_size / MH_V2_SECTOR);
    if (!mh_io_read(io, 0, first, sizeof first)) return false;
    if (memcmp(first, MH_V2_WIP_MAGIC, 7) == 0) return false;
    if (menu_sectors - 1u > 0x5c) return false;

    cases = malloc(MH_V2_CASE_TABLE);
    discs = malloc(MH_V2_DISC_TABLE);
    if (!cases || !discs) goto done;
    if (!mh_io_read(io, MH_V2_CASE_OFF, cases, MH_V2_CASE_TABLE)) goto done;
    if (!mh_io_read(io, MH_V2_DISC_OFF, discs, MH_V2_DISC_TABLE)) goto done;
    if (!read_cfg_slot(io, cfg)) goto done;

    memset(used, 0, sizeof used);
    memset(case_of, 0xff, sizeof case_of);
    memset(slot_of, 0xff, sizeof slot_of);
    for (i = 0; i < 256; i++) {
        const uint8_t *r = cases + (size_t)i * MH_V2_CASE_SIZE;
        uint16_t art;
        if (r[4] == 0) continue;
        art = mh_v2_rd16(r + 0x38);
        if (memcmp(r, MH_V2_CASE_MAGIC, 4) != 0 || r[6] != i || (uint8_t)(r[5] - 1) > 4 ||
            (art != 0xffff && art != i) || mh_crc_record(r, MH_V2_CASE_SIZE) != mh_v2_rd32(r + 0x4c) ||
            !mh_v2_case_ext_valid(r))
            goto done;
        for (j = 0; j < 5; j++) {
            uint8_t id = r[0x3a + j];
            if (j < r[5]) {
                if (used[id]) goto done;
                used[id] = 1;
                case_of[id] = (uint8_t)i;
                slot_of[id] = (uint8_t)j;
            } else if (id != 0) {
                goto done;
            }
        }
        ncases++;
    }
    for (i = 0; i < 256; i++) {
        const uint8_t *r = discs + (size_t)i * MH_V2_DISC_SIZE;
        uint64_t base, raw_lba, raw_blocks, raw_end;
        uint32_t sectors;
        uint16_t art;
        if (r[4] == 0) {
            if (used[i]) goto done;
            continue;
        }
        sectors = mh_v2_rd32(r + 0x5c);
        base = mh_v2_rd64(r + 0x3c);
        raw_lba = mh_v2_rd64(r + 0x4c);
        raw_blocks = mh_v2_rd64(r + 0x54);
        raw_end = raw_blocks + raw_lba;
        if (!used[i] || memcmp(r, MH_V2_DISC_MAGIC, 4) != 0 || r[5] != i || r[6] != case_of[i] || r[7] != slot_of[i] ||
            (uint8_t)(r[0x60] - 1) > 0x62 || sectors == 0 || raw_blocks != (uint64_t)sectors * 5 ||
            base < MH_V2_FIXED_BLOCKS || (base & 0xff) != 0 || base != mh_v2_rd64(r + 0x44) || base + 0x100 != raw_lba ||
            raw_end < raw_lba)
            goto done;
        if (size / 512 < raw_end) goto done;
        art = mh_v2_rd16(r + 0x62);
        if (!mh_disc_layout_valid(r) || (art != 0xffff && art != i) ||
            mh_crc_record(r, MH_V2_DISC_SIZE) != mh_v2_rd32(r + 0xfc) ||
            !mh_v2_disc_ext_valid(r, cases + (size_t)case_of[i] * MH_V2_CASE_SIZE))
            goto done;
        ndiscs++;
    }
    if (ncases != 0 && ndiscs != 0) {
        uint8_t slot[MH_V2_HDR_SLOT];
        mh_v2_header_recovered((uint16_t)ncases, (uint16_t)ndiscs, menu_sectors, mh_crc32(cases, MH_V2_CASE_TABLE),
                               mh_crc32(discs, MH_V2_DISC_TABLE), slot);
        memcpy(hdr, slot, MH_V2_HDR_SIZE);
        ok = mh_v2_header_valid(hdr);
    }
done:
    free(cases);
    free(discs);
    return ok;
}

/* orig: v2_probe_recoverable_physical (the open is done by the disk layer) */
bool mh_v2_recover_probe(void *user, MhIo *io, uint64_t size)
{
    uint8_t hdr[MH_V2_HDR_SIZE];
    return mh_v2_recover_header_from_body(io, size, (const MhV2Env *)user, hdr);
}

/* ---------------------------------------------------------------- load */

/* orig: verification_issue_text @0x14003d930 (the disc module does not export it) */
static void issue_text(MhDisc *d)
{
    MhWbuf w;
    uint32_t v = d->verify_issues;
    mh_wbuf_init(&w);
    if (v & 0xc0) {
        if (v & 2)
            mh_wbuf_puts(&w, "Some sectors are inconsistent. Videos or audio may fail. You can create a copy with recalculated EDC/ECC.\r\n");
        else
            mh_wbuf_puts(&w, "Some sectors have EDC/ECC that does not match, but the disc is identical to the known original, "
                             "so the real disc has the same sectors. Create an EDC/ECC copy only if videos or audio fail.\r\n");
    }
    if (v & 1) mh_wbuf_puts(&w, "Could not confirm the game ID.\r\n");
    if (v & 2) mh_wbuf_puts(&w, "The Disc differs from the known version; it may be a modification.\r\n");
    if (v & 4) mh_wbuf_puts(&w, "The size reported by the game exceeds the file.\r\n");
    if (v & 8) mh_wbuf_puts(&w, "The format of some sectors does not match the CUE.\r\n");
    if (v & 0x10) mh_wbuf_puts(&w, "The data on the card does not match the Disc record.\r\n");
    if (v & 0x20) mh_wbuf_puts(&w, "The card has invalid extra data. Restore this Disc.\r\n");
    if (v == 0) mh_wbuf_puts(&w, "Disc verified: read and structure OK.");
    mh_str_copy(d->verify_text, sizeof d->verify_text, mh_wbuf_cstr(&w));
    mh_wbuf_free(&w);
}

/* orig: cache_name.constprop.0 */
static void cache_name(char *out, size_t n, const char *dir, const char *prefix, unsigned num, const char *ext)
{
    MhWbuf w;
    mh_wbuf_init(&w);
    mh_wbuf_puts(&w, prefix);
    mh_wbuf_u32(&w, num);
    mh_wbuf_puts(&w, ext);
    mh_path_join(dir, mh_wbuf_cstr(&w), out, n);
    mh_wbuf_free(&w);
}

/* orig: cache_region.constprop.0: copies one 0x2800 byte region of the source to a file */
static bool cache_region(MhIo *io, uint64_t off, const char *path)
{
    uint8_t *buf = malloc(MH_V2_ART_SIZE);
    bool ok = false;
    if (!buf) return false;
    if (mh_io_read(io, off, buf, MH_V2_ART_SIZE)) ok = mh_write_entire_file(path, buf, MH_V2_ART_SIZE);
    free(buf);
    return ok;
}

typedef struct LoadCtx {
    MhIo *io;
    MhV2SourceKind kind;
    uint32_t disk;
    uint64_t size;
    const char *image_path;
    const char *cache_dir;
    const MhV2Env *env;
    MhV2Result *res;
    MhProject *np;
    const uint8_t *hdr;       /* the chosen header (0x930 bytes) */
    uint8_t *cases;           /* Case table */
    uint8_t *discs;           /* Disc table */
    uint8_t used_disc[256];
    unsigned n_cases, n_discs;
    int hdr_repair;           /* orig 0x84 */
    int cheat_fail;           /* orig 0x88 */
    int tracks_damaged;       /* orig 0x8c */
    int cheat_removed;        /* orig 0x98 */
    int tracks_rebuilt;       /* orig 0x80 (count) */
    int rebuilt_flag;         /* orig 0x9c */
    int recovered;            /* orig 0x6c */
} LoadCtx;

static bool lfail(LoadCtx *c, const char *msg)
{
    mh_v2_result_fail(c->res, msg);
    return false;
}

/* Converts the track map sector into MhTrack entries. false when an entry is malformed. */
static bool tracks_from_sector(MhDisc *d, const uint8_t *t)
{
    unsigned n = t[8], k;
    d->ntracks = (uint16_t)n;
    for (k = 0; k < n; k++) {
        MhV2TrackEntry e;
        MhTrack *tr = &d->tracks[k];
        uint32_t len;
        mh_v2_trk_entry_decode(t + MH_V2_TRK_ENTRY_OFF + 16 * k, &e);
        memset(tr, 0, sizeof *tr);
        tr->number = (uint8_t)(k + 1);
        if (e.type == 0) {
            tr->kind = 1;
        } else {
            if (e.type != 1 && e.type != 2) return false;
            tr->kind = 2;
        }
        tr->mode = e.type;
        len = (e.start + e.length) - e.index1;
        if (e.length < e.index1 - e.start) len = 0;
        tr->length = len;
        tr->pregap = e.index1 - e.start;
        tr->index0 = e.start;
        tr->index1 = e.index1;
        tr->start = e.index1;
        tr->flags = (uint8_t)(((e.flag_bits & 1) << 2) | ((e.flag_bits >> 1) & 3));
    }
    return true;
}

/* Loads one disc of a Case into a new MhDisc. Returns the disc or NULL (c->res set). */
static MhDisc *load_disc(LoadCtx *c, const uint8_t *drec, const uint8_t *crec, unsigned ci, unsigned slot, unsigned id)
{
    MhV2DiscRec r;
    MhDisc *d;
    uint8_t trk[MH_V2_SECTOR], first[MH_V2_SECTOR];
    bool track_ok, rebuilt = false;
    uint32_t flags;
    uint64_t base;
    uint8_t marker[512];
    char path[MH_PATH_MAX];

    mh_v2_disc_decode(drec, &r, NULL);
    base = r.base_lba;
    if (c->used_disc[id] || !c->discs[(size_t)id * MH_V2_DISC_SIZE + 4]) {
        lfail(c, "The card has an invalid Disc record.");
        return NULL;
    }
    if (!mh_v2_disc_magic(drec) || r.id != id || r.case_index != ci || r.slot != slot ||
        (uint8_t)(r.track_count - 1) > 0x62 || r.sectors == 0 || r.raw_blocks != (uint64_t)r.sectors * 5 ||
        (base & 0xff) != 0 || base != r.track_lba || r.raw_lba != base + 0x100 ||
        c->size / 512 < r.raw_blocks + r.raw_lba || !mh_disc_layout_valid(drec) ||
        mh_crc_record(drec, MH_V2_DISC_SIZE) != r.crc || !mh_v2_disc_ext_valid(drec, crec)) {
        lfail(c, "The card has an invalid Disc record.");
        return NULL;
    }
    c->used_disc[id] = 1;

    /* track map: native, rebuilt from the first RAW sector (single track), or damaged */
    memset(trk, 0, sizeof trk);
    track_ok = mh_io_read_raw_payload(c->io, r.track_lba, trk) && mh_track_sector_valid(trk, r.sectors);
    if (!track_ok) {
        track_ok = false;
        if (c->io && r.track_count == 1 && r.sectors != 0 && mh_io_read_raw_payload(c->io, r.raw_lba, first) &&
            (uint8_t)(first[15] - 1) < 2) {
            uint8_t synth[MH_V2_SECTOR];
            uint8_t mode = first[15];
            uint16_t len = mh_v2_rd16(trk + MH_V2_TRK_SBI_OFF); /* SBI block of the damaged sector */
            memset(synth, 0, sizeof synth);
            memcpy(synth, MH_V2_TRK_MAGIC, 7);
            synth[8] = 1;
            synth[9] = 0x10;
            mh_v2_wr32(synth + 0x18, r.sectors);
            synth[0x1c] = mode;
            synth[0x1d] = 4;
            if ((uint16_t)(len - 4) < 0x2e5 && memcmp(trk + MH_V2_TRK_SBI_OFF + 8, MH_V2_SBI_MAGIC, 4) == 0 &&
                mh_v2_rd32(trk + MH_V2_TRK_SBI_OFF + 4) == mh_crc32(trk + MH_V2_TRK_SBI_OFF + 8, len)) {
                synth[10] |= 1;
                memcpy(synth + MH_V2_TRK_SBI_OFF, trk + MH_V2_TRK_SBI_OFF, (size_t)len + 8);
            }
            mh_v2_wr32(synth + 0xc, 0);
            mh_v2_wr32(synth + 0xc, mh_crc32(synth, sizeof synth));
            if (mh_track_sector_valid(synth, r.sectors)) {
                c->tracks_rebuilt++;
                memcpy(trk, synth, sizeof trk);
                c->rebuilt_flag = 1;
                rebuilt = true;
                track_ok = true;
            }
        }
    }

    d = mh_calloc(sizeof *d);
    if (!d) {
        lfail(c, "Not enough memory to load the card.");
        return NULL;
    }
    d->kind = (uint32_t)c->kind;
    if (track_ok) {
        d->status = MH_DISC_STATUS_OK;
        flags = MH_DF_STRUCT_OK | MH_DF_FROM_V2 | MH_DF_TRACKSET_CHECKED;
    } else {
        d->status = MH_DISC_STATUS_FIXABLE;
        flags = MH_DF_FROM_V2 | MH_DF_TRACKSET_CHECKED;
    }
    if (drec[0xc] != 0) flags |= MH_DF_SERIAL;
    if (r.flags & MH_V2_DF_TRACKS_FIXED) flags |= MH_DF_TRACKS_FIXED;
    d->flags = flags;
    mh_str_copy(d->serial, sizeof d->serial, (const char *)drec + 0xc);
    /* the original copied the name with a 0x21 byte cap and the title with 0x60 from the same
     * source; both stop at the NUL that ends the 0x20 byte field */
    mh_v2_disc_full_title(drec, d->name, MH_DISC_NAME_MAX);
    mh_v2_disc_full_title(drec, d->title, MH_DISC_TITLE_MAX);
    /* bin_count is a free byte on the card; MhDisc.files holds 99 entries and the rest of the code
     * indexes it by nfiles (the original had no such array walk for a loaded disc) */
    d->nfiles = r.bin_count > MH_DISC_MAX_FILES ? MH_DISC_MAX_FILES : r.bin_count;
    d->total_sectors = r.sectors;
    d->total_bytes = (uint64_t)r.sectors * MH_V2_SECTOR;
    d->crc32 = r.crc32;
    memcpy(d->sha1, r.sha1, 20);

    /* session marker of an earlier write in this very session (resume / verification) */
    if (!mh_io_read(c->io, (r.raw_lba - 1) * 512, marker, sizeof marker)) {
        mh_disc_free(d);
        lfail(c, "Could not read the Disc state. Check the card connection.");
        return NULL;
    }
    {
        MhV2Resume rs;
        if (mh_v2_resume_decode(marker, &rs)) {
            MhV2DiscRec m;
            mh_v2_disc_decode(rs.disc_rec, &m, NULL);
            if (m.base_lba == r.base_lba && m.raw_lba == r.raw_lba && m.raw_blocks == r.raw_blocks &&
                m.sectors == r.sectors && m.crc32 == r.crc32 && memcmp(m.sha1, r.sha1, 20) == 0 && rs.has_verify) {
                d->verified = 1;
                d->verify_crc32 = r.crc32;
                memcpy(d->verify_sha1, r.sha1, 20);
                d->verify_issues = (uint32_t)(rs.verify_stamp & 0xffffffffu);
                d->verify_accepted = (uint32_t)(rs.verify_stamp >> 32);
                issue_text(d);
            }
        }
    }
    d->v2_image_tag = 0xffffffffffffffffull;
    d->card_index = (uint16_t)id;
    d->v2_drive = c->disk;
    if (c->kind == MH_V2_SRC_IMAGE) mh_str_copy(d->source_path, sizeof d->source_path, c->image_path);
    d->v2_pkg_a = base;
    d->v2_raw_lba512 = r.raw_lba;
    d->v2_raw_blocks = r.raw_blocks;

    if (track_ok) {
        if (!mh_track_sector_valid(trk, d->total_sectors) || !tracks_from_sector(d, trk)) {
            mh_disc_free(d);
            lfail(c, "Could not convert a track table.");
            return NULL;
        }
        if (trk[10] & MH_V2_TRK_F_SBI) {
            uint16_t len = mh_v2_rd16(trk + MH_V2_TRK_SBI_OFF);
            cache_name(path, sizeof path, c->cache_dir, "DISC-", id, ".sbi");
            if ((uint16_t)(len - 4) < 0x2e5 && mh_write_entire_file(path, trk + MH_V2_TRK_SBI_OFF + 8, len)) {
                mh_str_copy(d->sbi_path, sizeof d->sbi_path, path);
                d->flags |= MH_DF_SBI;
            } else {
                mh_disc_free(d);
                lfail(c, "Could not extract an SBI file from the card.");
                return NULL;
            }
        }
    } else {
        c->tracks_damaged++;
        d->ntracks = r.track_count;
    }

    /* disc art */
    if (r.art_id != MH_V2_ART_NONE) {
        uint64_t art_lba;
        if (r.art_id != id) {
            mh_disc_free(d);
            lfail(c, "A Disc has an invalid art ID.");
            return NULL;
        }
        cache_name(path, sizeof path, c->cache_dir, "DISC-", id, ".art");
        art_lba = base + ((r.flags & MH_V2_DF_EXT) ? 0x1e : 0x14);
        if (!cache_region(c->io, art_lba * 512, path)) {
            mh_disc_free(d);
            lfail(c, "Could not load a Disc art from the card.");
            return NULL;
        }
        mh_str_copy(d->cd_art_path, sizeof d->cd_art_path, path);
        d->flags |= MH_DF_CD_ART;
    }

    /* cheats */
    if (r.cheat_size != 0) {
        size_t rounded = ((size_t)r.cheat_size + 0x1ff) & ~(size_t)0x1ff;
        uint8_t *blob = mh_alloc(rounded);
        char e[256];
        if (!blob) {
            mh_disc_free(d);
            lfail(c, "Not enough memory to load cheats.");
            return NULL;
        }
        e[0] = 0;
        if (!mh_io_read(c->io, r.cheat_lba * 512, blob, rounded)) {
            int code = c->io->err->code;
            mh_free(blob);
            mh_disc_free(d);
            mh_v2_result_fail_code(c->res, "Could not read the cheat catalog; the previous library was preserved", code);
            return NULL;
        }
        if (!mh_cheat_profile_deserialize(blob, r.cheat_size, &d->cheats, e, sizeof e)) {
            d->cheat_regenerated = 1;
            mh_str_copy(d->cheat_note, sizeof d->cheat_note, e);
            if (r.cheat_size > 0x3f && memcmp(blob, "PSTCHT1", 7) == 0 && blob[8] == 1 && blob[9] == 0)
                mh_str_copy(d->cheat_note, sizeof d->cheat_note,
                            "Old profile: the codes will not be reinterpreted. The Manager will prepare a catalog "
                            "validated by serial, with all choices turned off.");
            c->cheat_fail = 1;
        }
        if (d->cheats) {
            mh_cheat_profile_prepare_hardware(d->cheats, d->serial);
            if (d->cheats->removed != 0) {
                MhWbuf w;
                mh_wbuf_init(&w);
                mh_wbuf_u32(&w, d->cheats->removed);
                mh_wbuf_puts(&w, " cheat(s) removed because of a documented incompatibility. The others were preserved. "
                                 "Apply Changes to update the card.");
                mh_str_copy(d->cheat_note, sizeof d->cheat_note, mh_wbuf_cstr(&w));
                mh_wbuf_free(&w);
                c->cheat_removed = 1;
            }
        }
        mh_free(blob);
    }

    /* the status note */
    if (track_ok) {
        if (!rebuilt)
            mh_str_copy(d->notes, sizeof d->notes,
                        c->kind == MH_V2_SRC_IMAGE
                            ? "Loaded from an existing PSXRAW V2 image. The RAW data will be preserved."
                            : "Loaded from an existing PSXRAW V2 card. The RAW data will be preserved.");
        else
            mh_str_copy(d->notes, sizeof d->notes,
                        "The single-track Track Table was damaged and was rebuilt exactly as "
                        "MODE1/2352 or MODE2/2352 from the protected DiscRecord and the first RAW sector. The game data "
                        "stayed at the same LBA.");
    } else {
        mh_str_copy(d->notes, sizeof d->notes,
                    "This Disc's Track Table is damaged and has several tracks. The RAW data was preserved, "
                    "but you must provide the original CUE before applying changes.");
    }
    return d;
}

/* orig: v2_load_source */
bool mh_v2_load_io(MhIo *io, MhV2SourceKind kind, uint32_t disk_number, uint64_t size,
                   const char *image_path, const char *cache_dir, const MhV2Env *env,
                   MhProject *project, MhV2Result *res)
{
    LoadCtx c;
    uint8_t *scratch = NULL, *ha = NULL, *hb = NULL, *ctab = NULL, *dtab = NULL;
    MhProject *np = NULL;
    uint8_t chosen[MH_V2_HDR_SIZE];
    bool A_valid, B_valid, ok = false;
    uint32_t crc_c, crc_d;
    MhV2Header hd;
    unsigned i, k;

    if (res) memset(res, 0, sizeof *res);
    if (!cache_dir || !project || !io || (kind == MH_V2_SRC_IMAGE && !image_path)) {
        mh_v2_result_fail(res, "Invalid V2 load target.");
        return false;
    }
    if (!mh_ensure_dir(cache_dir)) {
        mh_v2_result_fail(res, "Could not create the temporary art cache.");
        return false;
    }
    memset(&c, 0, sizeof c);
    c.io = io;
    c.kind = kind;
    c.disk = disk_number;
    c.size = size;
    c.image_path = image_path;
    c.cache_dir = cache_dir;
    c.env = env;
    c.res = res;

    np = mh_calloc(sizeof *np);
    scratch = mh_alloc(MH_V2_DISC_TABLE);
    ha = mh_alloc(MH_V2_HDR_SIZE);
    hb = mh_alloc(MH_V2_HDR_SIZE);
    ctab = mh_alloc(MH_V2_CASE_TABLE);
    if (!np || !scratch || !ha || !hb || !ctab) {
        lfail(&c, "Not enough memory to load the card.");
        goto out;
    }
    dtab = scratch;
    mh_project_init(np);
    c.np = np;

    /* header slots */
    {
        unsigned b;
        uint8_t *slot = scratch; /* 0xA00 bytes */
        bool bad = false;
        if (!mh_io_read(io, MH_V2_HDR_A_OFF, slot, MH_V2_HDR_SLOT)) bad = true;
        if (!bad) {
            memcpy(ha, slot, MH_V2_HDR_SIZE);
            for (b = MH_V2_HDR_SIZE; b < MH_V2_HDR_SLOT; b++)
                if (slot[b]) bad = true;
        }
        if (!bad && !mh_io_read(io, MH_V2_HDR_B_OFF, slot, MH_V2_HDR_SLOT)) bad = true;
        if (!bad) {
            memcpy(hb, slot, MH_V2_HDR_SIZE);
            for (b = MH_V2_HDR_SIZE; b < MH_V2_HDR_SLOT; b++)
                if (slot[b]) bad = true;
        }
        if (bad) {
            lfail(&c, "Could not read the V2 headers.");
            goto out;
        }
    }
    A_valid = mh_v2_header_valid(ha);
    B_valid = mh_v2_header_valid(hb);
    if (!A_valid && !B_valid) {
        if (mh_v2_recover_header_from_body(io, size, env, ha)) {
            memset(hb, 0, MH_V2_HDR_SIZE);
            A_valid = true;
            B_valid = false;
            c.recovered = 1;
        } else {
            lfail(&c, "Library has no valid commit. If a write was interrupted, resume it in the same session of the "
                      "Manager before using the card.");
            goto out;
        }
    }

    /* the tables */
    if (!mh_io_read(io, MH_V2_CASE_OFF, ctab, MH_V2_CASE_TABLE) ||
        !mh_io_read(io, MH_V2_DISC_OFF, dtab, MH_V2_DISC_TABLE)) {
        lfail(&c, "Could not read the V2 tables.");
        goto out;
    }
    crc_c = mh_crc32(ctab, MH_V2_CASE_TABLE);
    crc_d = mh_crc32(dtab, MH_V2_DISC_TABLE);

    /* choose the header whose table CRCs match the tables on the card */
    {
        MhV2Header a, b;
        mh_v2_header_decode(ha, &a);
        mh_v2_header_decode(hb, &b);
        if (!A_valid) {
            /* only B is valid */
            if (crc_c == b.crc_cases && crc_d == b.crc_discs) {
                memcpy(chosen, hb, sizeof chosen);
                c.hdr_repair = 1;
            } else {
                lfail(&c, "No valid V2 header matches the tables on the card.");
                goto out;
            }
        } else if (a.crc_cases != crc_c) {
            if (B_valid && crc_c == b.crc_cases && crc_d == b.crc_discs) {
                memcpy(chosen, hb, sizeof chosen);
                c.hdr_repair = 1;
            } else {
                lfail(&c, "No valid V2 header matches the tables on the card.");
                goto out;
            }
        } else if (!B_valid) {
            if (a.crc_discs != crc_d) {
                lfail(&c, "No valid V2 header matches the tables on the card.");
                goto out;
            }
            memcpy(chosen, ha, sizeof chosen);
            c.hdr_repair = 1;
        } else if (b.crc_cases == crc_c && b.crc_discs == crc_d) {
            /* B matches the tables too */
            if (a.crc_discs == crc_d) {
                if (memcmp(ha, hb, MH_V2_HDR_SIZE) == 0) {
                    memcpy(chosen, ha, sizeof chosen);
                    c.hdr_repair = c.recovered;
                } else if (a.generation == b.generation) {
                    lfail(&c, "The V2 headers differ but have the same generation; automatic selection refused.");
                    goto out;
                } else if ((int32_t)(b.generation - a.generation) < 1) {
                    memcpy(chosen, ha, sizeof chosen);
                    c.hdr_repair = 1;
                } else {
                    memcpy(chosen, hb, sizeof chosen);
                    c.hdr_repair = 1;
                }
            } else {
                memcpy(chosen, hb, sizeof chosen);
                c.hdr_repair = 1;
            }
        } else {
            /* B does not match the tables, A does (checked below) */
            if (a.crc_discs != crc_d) {
                lfail(&c, "No valid V2 header matches the tables on the card.");
                goto out;
            }
            memcpy(chosen, ha, sizeof chosen);
            if (memcmp(ha, hb, MH_V2_HDR_SIZE) != 0)
                c.hdr_repair = 1;
            else
                c.hdr_repair = c.recovered;
        }
    }
    c.hdr = chosen;
    mh_v2_header_decode(chosen, &hd);
    c.cases = ctab;
    c.discs = dtab;

    /* the menu */
    if (hd.menu_sectors - 1u >= 0x5d) {
        lfail(&c, "Invalid V2 menu sector count.");
        goto out;
    }
    {
        size_t msize = (size_t)hd.menu_sectors * MH_V2_SECTOR;
        uint8_t *menu = mh_alloc(msize);
        uint8_t slot[MH_V2_SLOT];
        char mpath[MH_PATH_MAX];
        uint32_t s;
        if (!menu) {
            lfail(&c, "Not enough memory to extract the V2 menu.");
            goto out;
        }
        if (!c.recovered) {
            for (s = 0; s < hd.menu_sectors; s++) {
                unsigned b;
                if (!mh_io_read(io, (hd.menu_lba + (uint64_t)s * 5) * 512, slot, sizeof slot)) {
                    mh_free(menu);
                    lfail(&c, "Could not extract the V2 menu after several retries.");
                    goto out;
                }
                for (b = MH_V2_SECTOR; b < MH_V2_SLOT; b++)
                    if (slot[b]) {
                        mh_free(menu);
                        lfail(&c, "Invalid V2 menu padding.");
                        goto out;
                    }
                memcpy(menu + (size_t)s * MH_V2_SECTOR, slot, MH_V2_SECTOR);
            }
        } else {
            if (!env || !env->menu || env->menu_size < msize) {
                mh_free(menu);
                lfail(&c, "Not enough memory to extract the V2 menu.");
                goto out;
            }
            memcpy(menu, env->menu, msize);
        }
        mh_path_join(cache_dir, "menu.bin", mpath, sizeof mpath);
        if (!mh_write_entire_file(mpath, menu, msize)) {
            mh_free(menu);
            lfail(&c, "Could not save the V2 menu to the editing cache.");
            goto out;
        }
        np->has_menu_defaults = mh_menu_defaults_read(menu, msize, np->menu_defaults, NULL) ? 1u : 0u;
        mh_free(menu);
    }

    /* cases and their discs */
    for (i = 0; i < 256; i++) {
        const uint8_t *cr = ctab + (size_t)i * MH_V2_CASE_SIZE;
        MhCase *cs = &np->cases[i];
        MhV2CaseRec r;
        if (cr[4] == 0) continue;
        mh_v2_case_decode(cr, &r, NULL);
        if (!mh_v2_case_magic(cr) || r.index != i || (uint8_t)(r.disc_count - 1) > 4 ||
            mh_crc_record(cr, MH_V2_CASE_SIZE) != r.crc || !mh_v2_case_ext_valid(cr)) {
            lfail(&c, "The card has an invalid Case record.");
            goto out;
        }
        cs->in_use = 1;
        cs->slot = (uint8_t)i;
        cs->pad5 = r.variant;
        mh_v2_case_full_name(cr, dtab + (size_t)r.disc_ids[0] * MH_V2_DISC_SIZE, cs->title, MH_CASE_TITLE_MAX);
        mh_str_copy(cs->serial, sizeof cs->serial, (const char *)cr + 0x28);
        if (r.art_id != MH_V2_ART_NONE) {
            char path[MH_PATH_MAX];
            if (r.art_id != r.index) {
                lfail(&c, "A Case has an invalid art ID.");
                goto out;
            }
            cache_name(path, sizeof path, cache_dir, "CASE-", r.art_id, ".art");
            if (!cache_region(io, MH_V2_ART_OFF + (uint64_t)i * MH_V2_ART_SIZE, path)) {
                lfail(&c, "Could not load a Case art from the card.");
                goto out;
            }
            mh_str_copy(cs->art_path, sizeof cs->art_path, path);
        }
        for (k = 0; k < r.disc_count; k++) {
            unsigned id = r.disc_ids[k];
            MhDisc *d = load_disc(&c, dtab + (size_t)id * MH_V2_DISC_SIZE, cr, i, k, id);
            if (!d) goto out;
            cs->discs[cs->disc_count++] = d;
        }
        c.n_discs += r.disc_count;
        c.n_cases++;
    }
    if (hd.case_count != c.n_cases || hd.disc_count != c.n_discs) {
        lfail(&c, "The V2 header counts do not match the tables.");
        goto out;
    }

    /* the project block */
    np->device = (int32_t)disk_number;
    np->reserved_c = hd.generation;
    memcpy(np->reserved_18, &size, 8);
    np->card_loaded = (kind == MH_V2_SRC_CARD) ? 1u : 0u;
    np->image_loaded = (kind == MH_V2_SRC_IMAGE) ? 1u : 0u;
    if (kind == MH_V2_SRC_IMAGE) mh_str_copy(np->image_path, sizeof np->image_path, image_path);

    {
        bool clean = !c.cheat_removed && !c.cheat_fail && !c.hdr_repair && !c.rebuilt_flag && !c.tracks_damaged;
        const char *msg;
        MhWbuf w;
        mh_wbuf_init(&w);
        np->dirty = clean ? (((chosen[7] >> 1) ^ 1) & 1u) : 1u;
        mh_project_recount(np);
        mh_project_clear(project);
        *project = *np;
        free(np);
        np = NULL;
        if (res) {
            res->ok = 1;
            res->size = size;
            res->cases = (uint16_t)c.n_cases;
            res->discs = (uint16_t)c.n_discs;
            if (c.tracks_damaged == 0 && c.tracks_rebuilt != 0) {
                mh_wbuf_puts(&w, "PSXRAW V2 recovered without changing the layout: ");
                mh_wbuf_u32(&w, (uint32_t)c.tracks_rebuilt);
                mh_wbuf_puts(&w, " single-track Track Table(s) were rebuilt exactly. Click Apply Changes "
                                 "to publish the metadata with safe 512-byte writes.");
                msg = mh_wbuf_cstr(&w);
            } else if (c.tracks_damaged != 0) {
                mh_wbuf_puts(&w, "PSXRAW V2 recognized from the fixed tables. ");
                mh_wbuf_u32(&w, (uint32_t)c.tracks_damaged);
                mh_wbuf_puts(&w, " Disc(s) have a damaged multi-track Track Table; the games were listed and "
                                 "preserved, but Apply Changes stays blocked until you provide the original CUEs.");
                msg = mh_wbuf_cstr(&w);
            } else {
                int flag = clean ? c.recovered : c.cheat_fail;
                msg = "V2 library recovered from the fixed tables: both headers were missing. Click Apply "
                      "Changes to publish backup header B first, then main header A.";
                if (!clean && !c.recovered) {
                    msg = "V2 loaded with an old or invalid cheat catalog. The update will be reported per "
                          "Disc.";
                }
                if (clean || !c.recovered) {
                    if (flag == 0) {
                        msg = "V2 recovered from one of the headers. Save or apply to repair the main/backup pair.";
                        if (c.hdr_repair == 0) {
                            msg = "Older V2 loaded. Apply Changes to update how the card appears "
                                  "in Finder.";
                            if (chosen[7] & 2) {
                                msg = "PSXRAW V2 card loaded for editing.";
                                if (kind == MH_V2_SRC_IMAGE) msg = "PSXRAW V2 image loaded for editing.";
                            }
                        }
                    }
                }
            }
            mh_str_copy(res->msg, sizeof res->msg, msg);
        }
        mh_wbuf_free(&w);
        ok = true;
    }

out:
    if (np) {
        mh_project_clear(np);
        free(np);
    }
    free(scratch);
    free(ha);
    free(hb);
    free(ctab);
    return ok;
}

/* orig: v2_load_image */
bool mh_v2_load_image(const char *path, const char *cache_dir, const MhV2Env *env,
                      MhProject *project, MhV2Result *res)
{
    MhIo io;
    uint64_t size;
    bool ok;
    if (!path || !mh_file_size64(path, &size)) {
        if (res) {
            res->ok = 0;
            mh_str_copy(res->msg, sizeof res->msg, "V2 image not found.");
        }
        return false;
    }
    if (!mh_io_open(&io, path, MH_IO_READ, false, NULL)) {
        if (res) {
            memset(res, 0, sizeof *res);
            mh_str_copy(res->msg, sizeof res->msg, "Could not open the V2 card for reading.");
        }
        return false;
    }
    ok = mh_v2_load_io(&io, MH_V2_SRC_IMAGE, 0xffffffffu, size, path, cache_dir, env, project, res);
    mh_io_close(&io);
    return ok;
}

/* orig: v2_load_physical */
bool mh_v2_load_physical(const MhDiskInfo *disk, const MhDiskOpts *opts, const char *cache_dir,
                         const MhV2Env *env, MhProject *project, MhV2Result *res)
{
    char err[MH_ERR_MSG_MAX];
    MhBlockDev *dev;
    bool ok;
    if (res) memset(res, 0, sizeof *res);
    if (!disk) {
        mh_v2_result_fail(res, "Invalid V2 load target.");
        return false;
    }
    err[0] = 0;
    dev = mh_blockdev_open_read(disk, opts, err, sizeof err);
    if (!dev) {
        mh_v2_result_fail(res, err[0] ? err : "Could not open the V2 card for reading.");
        return false;
    }
    ok = mh_v2_load_io(&dev->io, MH_V2_SRC_CARD, disk->number, dev->size, NULL, cache_dir, env, project, res);
    mh_blockdev_close(dev);
    return ok;
}
