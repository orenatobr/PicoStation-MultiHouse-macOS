/* Embedded database: loader (validation of the four blobs) and plain lookups. Formats are
 * documented in docs/EMBEDDED_DATA.md. */
#include "mh/disc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disc_internal.h"

uint32_t mh_rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
uint16_t mh_rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
uint64_t mh_rd64(const uint8_t *p) { return mh_rd32(p) | (uint64_t)mh_rd32(p + 4) << 32; }

static uint8_t *dup_blob(const uint8_t *src, size_t n)
{
    uint8_t *p;
    if (!src || n == 0 || n >= 0x80000000u) return NULL;
    p = mh_alloc(n);
    if (p) memcpy(p, src, n);
    return p;
}

/* orig: db_load @0x140015d40, disc index part (magic "PSTDB2") */
static bool load_disc_index(MhDb *db, const MhDbBlobs *b)
{
    uint8_t *d = dup_blob(b->disc_index, b->disc_index_size);
    size_t size = b->disc_index_size;
    uint32_t count, rec, ent;
    uint64_t end, entries;
    if (!d) return false;
    if (size < 0x40 || memcmp(d, "PSTDB2", 6) != 0 || mh_rd32(d + 8) != 2 || mh_rd32(d + 0x10) != 0x80 ||
        mh_rd32(d + 0x14) != 0x10)
        goto bad;
    count = mh_rd32(d + 0xc);
    rec = mh_rd32(d + 0x10);
    ent = mh_rd32(d + 0x14);
    if (count > (size - 0x40) / 0x80) goto bad;
    end = (uint64_t)count * rec + 0x40;
    if (end != mh_rd64(d + 0x18) || end > size) goto bad;
    if ((size - end) % ent != 0) goto bad;
    entries = (size - end) / ent;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *r = d + (uint64_t)i * rec;
        if (r[0x4f] != 0 || r[0x8f] != 0) goto bad;
        if (entries < (uint64_t)mh_rd32(r + 0x90) + mh_rd16(r + 0x94)) goto bad;
        if (i != 0 && mh_str_icmp((const char *)(d + (uint64_t)(i - 1) * rec + 0x40), (const char *)(r + 0x40)) > 0)
            goto bad;
    }
    db->disc_index = d;
    db->disc_index_size = size;
    db->disc_count = count;
    db->disc_rec_size = rec;
    db->disc_entry_size = ent;
    db->disc_entries_off = mh_rd64(d + 0x18);
    return true;
bad:
    mh_free(d);
    return false;
}

/* redump index ("PSTRDX2") */
static bool load_redump(MhDb *db, const MhDbBlobs *b)
{
    uint8_t *d = dup_blob(b->redump_index, b->redump_index_size);
    size_t size = b->redump_index_size;
    uint32_t count, rec;
    if (!d) return false;
    if (size < 0x40 || memcmp(d, "PSTRDX2", 7) != 0 || mh_rd32(d + 8) != 2 || mh_rd32(d + 0x10) != 0x80) goto bad;
    count = mh_rd32(d + 0xc);
    rec = mh_rd32(d + 0x10);
    if (count > (size - 0x40) / 0x80) goto bad;
    if ((uint64_t)count * rec + 0x40 != size) goto bad;
    for (uint32_t i = 1; i < count; i++)
        if (mh_rd64(d + 0x40 + (uint64_t)i * rec) < mh_rd64(d + 0x40 + (uint64_t)(i - 1) * rec)) goto bad;
    db->redump_index = d;
    db->redump_index_size = size;
    db->redump_count = count;
    db->redump_rec_size = rec;
    return true;
bad:
    mh_free(d);
    return false;
}

/* aliases ("PSTALS2") */
static bool load_aliases(MhDb *db, const MhDbBlobs *b)
{
    uint8_t *d = dup_blob(b->aliases, b->aliases_size);
    size_t size = b->aliases_size;
    uint32_t count;
    if (!d) return false;
    if (size < 0x20 || memcmp(d, "PSTALS2", 7) != 0 || mh_rd32(d + 8) != 2) goto bad;
    count = mh_rd32(d + 0xc);
    if (count > (size - 0x20) / 0x20 || ((uint64_t)count + 1) * 0x20 != size) goto bad;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *r = d + ((uint64_t)i + 1) * 0x20;
        if (r[0xf] != 0 || r[0x1f] != 0) goto bad;
        if (i != 0 && mh_str_icmp((const char *)(r - 0x20), (const char *)r) > 0) goto bad;
    }
    db->aliases = d;
    db->aliases_size = size;
    db->alias_count = count;
    return true;
bad:
    mh_free(d);
    return false;
}

/* trackset index ("PSTTS1"), including the embedded SBI blobs */
static bool load_trackset(MhDb *db, const MhDbBlobs *b)
{
    uint8_t *d = dup_blob(b->trackset, b->trackset_size);
    size_t size = b->trackset_size;
    uint32_t count, nent, sbi_seen = 0;
    uint64_t ent_off, data_off;
    if (!d) return false;
    if (size < 0x40 || memcmp(d, "PSTTS1", 6) != 0 || mh_rd32(d + 8) != 1 || mh_rd32(d + 0x10) != 0xc0 ||
        mh_rd32(d + 0x14) != 0x14)
        goto bad;
    count = mh_rd32(d + 0xc);
    nent = mh_rd32(d + 0x28);
    ent_off = mh_rd64(d + 0x18);
    data_off = mh_rd64(d + 0x20);
    if (ent_off != (uint64_t)count * 0xc0 + 0x40 || data_off != ent_off + (uint64_t)nent * 0x14 || data_off > size)
        goto bad;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *r = d + 0x40 + (uint64_t)i * 0xc0;
        uint16_t ntr = mh_rd16(r + 0x18), flags = mh_rd16(r + 0x1a), sbi_len = mh_rd16(r + 0x28);
        if ((uint16_t)(ntr - 1) > 0x62 || (uint64_t)nent < (uint64_t)ntr + mh_rd32(r + 0x14)) goto bad;
        if (r[0x4f] != 0 || r[0xaf] != 0 || mh_rd16(r + 0x2a) != 0) goto bad;
        if (i != 0 && memcmp(r - 0xc0, r, 0x14) > 0) goto bad;
        if (sbi_len == 0) {
            if (flags & 1) goto bad;
        } else {
            uint64_t off = mh_rd32(r + 0x24);
            const uint8_t *s;
            uint32_t prev = 0;
            if (!(flags & 1) || off < data_off || off > size || size - off < sbi_len) goto bad;
            if ((uint16_t)(sbi_len - 0x12) > 0x2d6 || (sbi_len - 4) % 14 != 0) goto bad;
            s = d + off;
            if (memcmp(s, "SBI\0", 4) != 0) goto bad;
            for (uint32_t k = 4; k < sbi_len; k += 14) {
                uint8_t m = s[k], se = s[k + 1], f = s[k + 2];
                uint32_t sec = (se & 0xf) + (se >> 4) * 10, fr = (f & 0xf) + (f >> 4) * 10;
                uint32_t lba;
                if ((se >> 4) > 5 || (m >> 4) > 9 || (m & 0xf) > 9 || (se & 0xf) > 9 || (f >> 4) > 7 ||
                    (f & 0xf) > 9 || sec > 0x3b || fr > 0x4a || s[k + 3] != 1)
                    goto bad;
                lba = ((uint32_t)((m & 0xf) + (m >> 4) * 10) * 60 + sec) * 75 + fr;
                if (lba < 150 || (k > 4 && lba <= prev)) goto bad;
                prev = lba;
            }
            sbi_seen++;
        }
    }
    if (sbi_seen != mh_rd32(d + 0x2c)) goto bad;
    db->trackset = d;
    db->trackset_size = size;
    db->ts_count = count;
    db->ts_rec_size = mh_rd32(d + 0x10);
    db->ts_entries_off = ent_off;
    db->ts_data_off = data_off;
    db->ts_entry_count = nent;
    db->ts_sbi_count = sbi_seen;
    return true;
bad:
    mh_free(d);
    return false;
}

void mh_db_free(MhDb *db)
{
    if (!db) return;
    mh_free(db->disc_index);
    mh_free(db->redump_index);
    mh_free(db->aliases);
    mh_free(db->trackset);
    memset(db, 0, sizeof *db);
}

bool mh_db_load(MhDb *db, const MhDbBlobs *blobs, const char *dir)
{
    memset(db, 0, sizeof *db);
    mh_str_copy(db->dir, sizeof db->dir, dir ? dir : "");
    if (blobs) {
        load_disc_index(db, blobs);
        load_redump(db, blobs);
        load_aliases(db, blobs);
        load_trackset(db, blobs);
    }
    return db->disc_index && db->redump_index && db->aliases && db->trackset;
}

bool mh_db_load_dir(MhDb *db, const char *embedded_dir, const char *dir)
{
    static const char *const names[4] = {"disc_index.bin", "redump_index.bin", "aliases.bin",
                                         "trackset_index.bin"};
    uint8_t *data[4] = {0};
    uint64_t sz[4] = {0};
    MhDbBlobs b;
    bool ok;
    for (int i = 0; i < 4; i++) {
        char p[MH_PATH_MAX];
        mh_path_join(embedded_dir, names[i], p, sizeof p);
        if (!mh_read_entire_file(p, &data[i], &sz[i])) data[i] = NULL;
    }
    memset(&b, 0, sizeof b);
    b.disc_index = data[0]; b.disc_index_size = (size_t)sz[0];
    b.redump_index = data[1]; b.redump_index_size = (size_t)sz[1];
    b.aliases = data[2]; b.aliases_size = (size_t)sz[2];
    b.trackset = data[3]; b.trackset_size = (size_t)sz[3];
    ok = mh_db_load(db, &b, dir);
    for (int i = 0; i < 4; i++) free(data[i]);
    return ok;
}

/* ---- records -------------------------------------------------------------------------- */

const uint8_t *mh_db_disc_rec(const MhDb *db, uint32_t idx)
{
    uint64_t off;
    if (!db->disc_index || idx >= db->disc_count) return NULL;
    off = (uint64_t)db->disc_rec_size * idx;
    if (off + 0xc0 > db->disc_index_size) return NULL;
    return db->disc_index + 0x40 + off;
}

/* orig: db_record_serial @0x140016ad0 */
const char *mh_db_record_serial(const MhDb *db, uint32_t idx)
{
    const uint8_t *r = mh_db_disc_rec(db, idx);
    return r ? (const char *)r : "";
}

/* orig: db_record_title @0x140016b10 */
const char *mh_db_record_title(const MhDb *db, uint32_t idx)
{
    const uint8_t *r = mh_db_disc_rec(db, idx);
    return r ? (const char *)r + 0x10 : "";
}

bool mh_db_record_info(const MhDb *db, uint32_t idx, uint32_t *first_track, uint16_t *ntracks, uint32_t *total_sectors)
{
    const uint8_t *r = mh_db_disc_rec(db, idx);
    if (!r) return false;
    if (first_track) *first_track = mh_rd32(r + 0x50);
    if (ntracks) *ntracks = mh_rd16(r + 0x54);
    if (total_sectors) *total_sectors = mh_rd32(r + 0x58);
    return true;
}

/* 16 byte entry: +0 number, +1 kind, +2 u16 pregap, +4 u32 length, +8 u32 crc32 */
const uint8_t *mh_db_entry_ptr(const MhDb *db, uint32_t entry)
{
    uint64_t off;
    if (!db->disc_index) return NULL;
    off = (uint64_t)entry * db->disc_entry_size + db->disc_entries_off;
    if (off + 0x10 > db->disc_index_size) return NULL;
    return db->disc_index + off;
}

bool mh_db_track_entry(const MhDb *db, uint32_t entry_index, MhDbTrack *t)
{
    const uint8_t *e = mh_db_entry_ptr(db, entry_index);
    if (!e) return false;
    t->number = e[0];
    t->kind = e[1];
    t->pregap = mh_rd16(e + 2);
    t->length = mh_rd32(e + 4);
    t->crc32 = mh_rd32(e + 8);
    return true;
}

/* ---- aliases -------------------------------------------------------------------------- */

/* binary search exactly as the original (db_resolve_alias, db_find_original_cue) */
const uint8_t *mh_db_alias_find(const MhDb *db, const char *key)
{
    uint32_t lo = 0, hi = db->alias_count;
    if (!db->aliases || hi == 0) return NULL;
    for (;;) {
        uint32_t mid = (hi - lo) / 2 + lo;
        const uint8_t *rec = db->aliases + ((uint64_t)mid + 1) * 0x20;
        int c = mh_str_icmp(key, (const char *)rec);
        if (c == 0) return rec;
        if (c < 0) {
            if (mid <= lo) return NULL;
            hi = mid;
        } else {
            lo = mid + 1;
            if (hi <= lo) return NULL;
        }
    }
}

/* orig: db_resolve_alias @0x140017120 */
bool mh_db_resolve_alias(const MhDb *db, const char *serial, char *out, size_t n)
{
    const uint8_t *rec;
    mh_str_copy(out, n, serial);
    rec = mh_db_alias_find(db, serial);
    if (!rec) return false;
    mh_str_copy(out, n, (const char *)rec + 0x10);
    return true;
}

static void art_path(const MhDb *db, const char *serial, const char *suffix, char *out, size_t n)
{
    char dir[MH_PATH_MAX], name[0x40], w[0x20];
    mh_path_join(db->dir, "database/art/default", dir, sizeof dir);
    mh_utf8_from_latin1(serial, w, sizeof w);
    mh_str_copy(name, sizeof name, w);
    mh_str_cat(name, sizeof name, suffix);
    mh_path_join(dir, name, out, n);
}

/* orig: db_find_art @0x1400157a0 */
bool mh_db_find_art(const MhDb *db, MhDisc *d, bool case_art)
{
    const char *suffix = case_art ? "-CASE.art" : "-CD.art";
    char serial[16], path[MH_PATH_MAX];
    if (!d || d->serial[0] == 0) return false;
    mh_str_copy(serial, sizeof serial, d->serial);
    art_path(db, serial, suffix, path, sizeof path);
    if (!mh_file_exists(path)) {
        uint32_t lo = 0, hi = db->alias_count, j;
        if (!db->aliases || hi == 0) return false;
        /* lower bound of the key */
        for (;;) {
            uint32_t mid = (hi - lo) / 2 + lo;
            const uint8_t *rec = db->aliases + ((uint64_t)mid + 1) * 0x20;
            if (mh_str_icmp(serial, (const char *)rec) < 1) {
                if (mid <= lo) break;
                hi = mid;
            } else {
                lo = mid + 1;
                if (hi <= lo) break;
            }
        }
        j = lo;
        if (j >= db->alias_count) return false;
        for (;;) {
            const uint8_t *rec = db->aliases + ((uint64_t)j + 1) * 0x20;
            if (mh_str_icmp(serial, (const char *)rec) != 0) return false;
            art_path(db, (const char *)rec + 0x10, suffix, path, sizeof path);
            if (mh_file_exists(path)) break;
            if (++j >= db->alias_count) return false;
        }
    }
    if (case_art) {
        mh_str_copy(d->case_art_path, sizeof d->case_art_path, path);
        d->flags |= MH_DF_CASE_ART;
    } else {
        mh_str_copy(d->cd_art_path, sizeof d->cd_art_path, path);
        d->flags |= MH_DF_CD_ART;
    }
    return true;
}

/* orig: db_find_original_cue @0x140015a90 */
bool mh_db_find_original_cue(const MhDb *db, const MhDisc *d, char *out, size_t n)
{
    char dir[MH_PATH_MAX], name[0x40], w[0x20];
    const uint8_t *rec;
    if (!d || d->serial[0] == 0) return false;
    mh_path_join(db->dir, "database/cues", dir, sizeof dir);
    mh_utf8_from_latin1(d->serial, w, sizeof w);
    mh_str_copy(name, sizeof name, w);
    mh_str_cat(name, sizeof name, ".cue");
    mh_path_join(dir, name, out, n);
    if (mh_file_exists(out)) return true;
    rec = mh_db_alias_find(db, d->serial);
    if (rec) {
        mh_utf8_from_latin1((const char *)rec + 0x10, w, sizeof w);
        mh_str_copy(name, sizeof name, w);
        mh_str_cat(name, sizeof name, ".cue");
        mh_path_join(dir, name, out, n);
        if (mh_file_exists(out)) return true;
    }
    if (n) out[0] = 0;
    return false;
}

/* orig: db_find_sbi @0x140015ca0 */
bool mh_db_find_sbi(const MhDb *db, MhDisc *d)
{
    (void)db;
    if (!d || !d->sbi_data || d->sbi_len == 0) return false;
    d->flags |= MH_DF_SBI;
    return true;
}
