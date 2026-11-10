/* Matching a disc against the database: disc index scoring, trackset (SHA-1 of every track),
 * track map recovery and Redump verification. */
#include "mh/disc.h"

#include <stdlib.h>
#include <string.h>

#include "disc_internal.h"

/* appends text exactly (these messages carry their own leading CRLF in the original) */
static void cat_note(MhDisc *d, const char *text) { mh_str_cat(d->notes, sizeof d->notes, text); }

/* orig: db_apply_disc @0x1400151b0 */
bool mh_db_apply_disc(const MhDb *db, MhDisc *d)
{
    char key[16], tmp[16];
    uint32_t best = 0xffffffffu, best_score = 0;
    bool ambiguous = false;
    if (!d || d->serial[0] == 0 || !db || !db->disc_index) return false;
    if (!mh_canonical_serial(d->serial, key) || db->disc_count == 0) return false;
    for (uint32_t i = 0; i < db->disc_count; i++) {
        const uint8_t *rec = mh_db_disc_rec(db, i);
        uint16_t dbt, mine, m;
        uint32_t base, score, dbs, ds, diff, first;
        uint32_t matches = 0;
        if (!rec || !mh_canonical_serial((const char *)rec, tmp) || mh_str_icmp(tmp, key) != 0) continue;
        dbt = mh_rd16(rec + 0x54);
        mine = d->ntracks;
        if (dbt == mine) { base = 100000; score = 200000; }
        else { base = 0; score = 100000; }
        dbs = mh_rd32(rec + 0x58);
        ds = d->total_sectors;
        diff = ds < dbs ? dbs - ds : ds - dbs;
        if (diff != 0) score = diff > 299 ? base : base + 300 - diff;
        m = dbt < mine ? dbt : mine;
        if (m != 0) {
            first = mh_rd32(rec + 0x50);
            for (uint32_t k = 0; k < m; k++) {
                const uint8_t *e = mh_db_entry_ptr(db, first + k);
                if (e && d->tracks[k].kind == e[1]) matches++;
            }
            score += matches * 100;
        }
        if (best == 0xffffffffu || best_score < score) {
            best = i;
            best_score = score;
            ambiguous = false;
        } else if (best_score == score) {
            const uint8_t *br = mh_db_disc_rec(db, best);
            if (!(br && mh_str_icmp((const char *)br + 0x10, (const char *)rec + 0x10) == 0)) ambiguous = true;
        }
    }
    if (ambiguous) {
        cat_note(d, "\r\nThe serial matches more than one edition in the disc database.");
        return false;
    }
    if (best != 0xffffffffu) {
        const uint8_t *rec = mh_db_disc_rec(db, best);
        d->db_index = best;
        d->flags |= MH_DF_DB_MATCH;
        if (rec && mh_rd16(rec + 0x54) == d->ntracks) {
            uint32_t first = mh_rd32(rec + 0x50);
            for (uint32_t k = 0; k < d->ntracks; k++) {
                const uint8_t *e = mh_db_entry_ptr(db, first + k);
                if (e && d->tracks[k].kind != e[1]) {
                    d->flags |= MH_DF_TRACKS_SUSPECT;
                    cat_note(d, "\r\nThe track types differ from the original database.");
                    break;
                }
            }
        } else {
            d->flags |= MH_DF_TRACKS_SUSPECT;
            cat_note(d, "\r\nThe original database lists a different number of tracks. Check or recover the original CUE.");
        }
        mh_disc_refresh_status(d);
        return true;
    }
    return false;
}

/* first trackset record whose 20 byte key is >= key (index may equal ts_count) */
static uint32_t ts_lower_bound(const MhDb *db, const uint8_t *key)
{
    uint32_t lo = 0, hi = db->ts_count;
    if (hi == 0) return 0;
    for (;;) {
        uint32_t mid = (hi - lo) / 2 + lo;
        const uint8_t *r = db->trackset + 0x40 + (uint64_t)mid * db->ts_rec_size;
        if (memcmp(r, key, 0x14) < 0) {
            lo = mid + 1;
            if (hi <= lo) break;
        } else {
            if (mid <= lo) break;
            hi = mid;
        }
    }
    return lo;
}

/* orig: db_apply_saved_single_track @0x1400155b0 */
bool mh_db_apply_saved_single_track(const MhDb *db, MhDisc *d)
{
    uint32_t i, found = 0, nfound = 0;
    if (!db || !d || !db->trackset) return false;
    if ((d->flags & (MH_DF_FROM_V2 | MH_DF_STRUCT_OK)) != (MH_DF_FROM_V2 | MH_DF_STRUCT_OK)) return false;
    if (d->kind != MH_DISC_KIND_V2_DEVICE && d->kind != MH_DISC_KIND_V2_IMAGE) return false;
    if (d->ntracks != 1 || d->tracks[0].kind != 2 || d->tracks[0].start != 0 || d->tracks[0].pregap != 0) return false;
    if (db->ts_count == 0) return false;
    i = ts_lower_bound(db, d->sha1);
    for (; i < db->ts_count; i++) {
        const uint8_t *r = db->trackset + 0x40 + (uint64_t)i * db->ts_rec_size;
        if (memcmp(r, d->sha1, 0x14) != 0) break;
        if (mh_rd16(r + 0x18) == 1) {
            found = mh_rd32(r + 0x1c);
            nfound++;
        }
    }
    if (nfound == 1) {
        const uint8_t *rec = mh_db_disc_rec(db, found);
        if (rec && mh_rd16(rec + 0x54) == 1 && mh_rd32(rec + 0x58) == d->total_sectors) {
            d->flags |= MH_DF_DB_MATCH;
            d->db_index = found;
            return true;
        }
    }
    return false;
}

/* orig: db_match_trackset @0x140016680 */
bool mh_db_match_trackset(const MhDb *db, MhDisc *d, const uint8_t *sha1s, unsigned n)
{
    uint32_t saved_status, flags, matches = 0, last = 0, i;
    if (!db || !d || n - 1 >= 99 || !sha1s || !db->trackset) return false;
    saved_status = d->ps.status_saved;
    if (d->ps.status == MH_PSS_TRACKSET) {
        if (saved_status == MH_PSS_STRONG) {
            mh_str_copy(d->serial, sizeof d->serial, d->ps.serial);
            flags = d->flags | MH_DF_SERIAL;
        } else {
            d->serial[0] = 0;
            flags = d->flags & ~(uint32_t)MH_DF_SERIAL;
        }
    } else {
        flags = d->flags;
    }
    if (d->sbi_data) flags &= ~(uint32_t)MH_DF_SBI;
    d->flags = flags & ~(uint32_t)(MH_DF_TRACKSET_OK | MH_DF_INTEGRITY_BAD | MH_DF_IDENTITY);
    d->sbi_len = 0;
    d->sbi_data = NULL;
    d->ps.needs_hash = 1;
    d->ps.trackset_id = 0;
    d->ps.trackset_serial[0] = 0;
    d->ps.status = saved_status;
    d->ps.source = d->ps.source_saved;
    mh_str_copy(d->ps.message, sizeof d->ps.message, d->ps.message_saved);

    if (db->ts_count != 0) {
        for (i = ts_lower_bound(db, sha1s); i < db->ts_count; i++) {
            const uint8_t *r = db->trackset + 0x40 + (uint64_t)i * db->ts_rec_size;
            if (memcmp(r, sha1s, 0x14) != 0) break;
            if (mh_rd16(r + 0x18) == n &&
                memcmp(db->trackset + db->ts_entries_off + (uint64_t)mh_rd32(r + 0x14) * 0x14, sha1s, (size_t)n * 0x14) == 0) {
                matches++;
                last = i;
            }
        }
        d->flags |= MH_DF_TRACKSET_CHECKED;
        if (matches == 1) {
            const uint8_t *r = db->trackset + 0x40 + (uint64_t)last * db->ts_rec_size;
            char canon[16];
            uint32_t idx;
            d->flags |= MH_DF_IDENTITY | MH_DF_TRACKSET_CHECKED | MH_DF_TRACKSET_OK;
            d->ps.status = MH_PSS_TRACKSET;
            d->ps.source = MH_PSSRC_TRACKSET;
            d->ps.needs_hash = 0;
            d->ps.trackset_id = mh_rd32(r + 0x20);
            mh_str_copy(d->ps.trackset_serial, sizeof d->ps.trackset_serial, (const char *)r + 0x30);
            mh_str_copy(d->ps.message, sizeof d->ps.message,
                        "Physical edition confirmed only by the complete, ordered sequence of SHA-1 hashes of all tracks.");
            if (d->serial[0] == 0 && mh_canonical_serial((const char *)r + 0x30, canon)) {
                mh_str_copy(d->serial, sizeof d->serial, canon);
                d->flags |= MH_DF_SERIAL;
            }
            idx = mh_rd32(r + 0x1c);
            if (idx < db->disc_count) {
                d->flags |= MH_DF_DB_MATCH;
                d->db_index = idx;
            }
            mh_apply_trackset_title(d, (const char *)r + 0x50);
            if (mh_rd16(r + 0x28) != 0) {
                d->sbi_len = mh_rd16(r + 0x28);
                d->flags |= MH_DF_SBI;
                d->sbi_data = db->trackset + mh_rd32(r + 0x24);
            }
            cat_note(d, "\r\nTRACKSET_HASH_CONFIRMED: physical identity resolved by all tracks, in CUE order.");
            mh_disc_refresh_status(d);
            return true;
        }
        if (matches > 1) {
            d->ps.status = MH_PSS_AMBIGUOUS;
            d->ps.source = MH_PSSRC_NONE;
            mh_str_copy(d->ps.message, sizeof d->ps.message,
                        "More than one record has the same complete, ordered SHA-1 sequence.");
            return false;
        }
    }
    d->flags |= MH_DF_TRACKSET_CHECKED | MH_DF_INTEGRITY_BAD;
    cat_note(d, "\r\nComplete trackset not found; no heuristic was promoted.");
    return false;
}

/* orig: db_recover_track_map_safe @0x140016b90 */
bool mh_db_recover_track_map(const MhDb *db, MhDisc *d)
{
    MhDisc *t = NULL;
    uint8_t *buf = NULL;
    MhIo io;
    bool io_open = false, ok = false;
    uint8_t sha[99 * 20];
    uint32_t idx, ntr, cum = 0;
    const uint8_t *rec;
    uint8_t mode;
    if (!d || d->nfiles != 1) return false;
    t = mh_alloc(sizeof *t);
    buf = mh_alloc(0x24c00);
    if (!t || !buf) goto done;
    memcpy(t, d, sizeof *t);
    t->notes[0] = 0;
    if (!db) goto done;
    idx = t->db_index;
    if (idx == 0xffffffffu || idx >= db->disc_count) goto done;
    rec = mh_db_disc_rec(db, idx);
    if (!rec) goto done;
    ntr = mh_rd16(rec + 0x54);
    if (mh_rd32(rec + 0x58) != t->total_sectors || (uint16_t)(ntr - 1) >= 99 ||
        t->total_sectors != t->files[0].sectors)
        goto done;
    mode = 2;
    if (t->ntracks != 0 && t->tracks[0].kind == 2 && t->tracks[0].mode != 0) mode = t->tracks[0].mode;
    memset(t->tracks, 0, sizeof t->tracks);
    for (uint32_t k = 0; k < ntr; k++) {
        const uint8_t *e = mh_db_entry_ptr(db, mh_rd32(rec + 0x50) + k);
        uint32_t len;
        uint16_t pregap;
        MhTrack *tr = &t->tracks[k];
        if (!e || e[0] != k + 1) goto done;
        len = mh_rd32(e + 4);
        pregap = mh_rd16(e + 2);
        if (len == 0 || t->total_sectors - len < cum || len <= pregap) goto done;
        tr->number = e[0];
        tr->kind = e[1];
        tr->mode = e[1] != 1 ? mode : 0;
        tr->flags = (uint8_t)((pregap != 0) << 2);
        tr->pregap = pregap;
        tr->index0 = cum;
        tr->index1 = cum + pregap;
        tr->start = cum + pregap;
        tr->length = len;
        cum += len;
    }
    if (cum != t->total_sectors || t->tracks[0].kind != 2 || t->tracks[0].pregap != 0) goto done;
    t->ntracks = (uint16_t)ntr;
    t->flags = (t->flags & ~(uint32_t)MH_DF_TRACKS_SUSPECT) | MH_DF_TRACKS_FIXED | MH_DF_STRUCT_OK;
    cat_note(t, "\r\nTrack map fixed automatically from the reference database; the original BINs were not changed.");
    mh_disc_refresh_status(t);
    if (!mh_io_open(&io, d->files[0].path, MH_IO_READ, false, NULL)) goto done;
    io_open = true;
    for (uint32_t k = 0; k < ntr; k++) {
        uint32_t first = t->tracks[k].start - t->tracks[k].pregap;
        uint32_t end = (k + 1 < ntr) ? t->tracks[k + 1].start - t->tracks[k + 1].pregap : t->total_sectors;
        MhSha1 c;
        if (end <= first) goto done;
        mh_sha1_init(&c);
        while (first < end) {
            uint32_t cnt = end - first > 0x40 ? 0x40 : end - first;
            if (!mh_io_read(&io, (uint64_t)first * 0x930, buf, (size_t)cnt * 0x930)) goto done;
            mh_sha1_update(&c, buf, (size_t)cnt * 0x930);
            first += cnt;
        }
        mh_sha1_final(&c, sha + k * 20);
    }
    if (!mh_db_match_trackset(db, t, sha, ntr)) goto done;
    memcpy(d->tracks, t->tracks, sizeof d->tracks);
    d->ntracks = t->ntracks;
    d->flags = t->flags;
    d->ps = t->ps;
    d->db_index = t->db_index;
    d->sbi_data = t->sbi_data;
    d->sbi_len = t->sbi_len;
    cat_note(d, "\r\nCUE repaired in memory: map confirmed by the SHA-1 hashes of all tracks in order. Original files preserved.");
    mh_disc_refresh_status(d);
    ok = true;
done:
    if (io_open) mh_io_close(&io);
    mh_free(buf);
    mh_free(t);
    return ok;
}

/* "Name (Track 02).bin" -> "Name" (orig: inline code of db_verify_redump) */
static void redump_game_name(const char *rec_name, char out[MH_DISC_TITLE_MAX])
{
    char name[100];
    size_t n = 0, len;
    int pos;
    while (n < 0x5f && rec_name[n]) { name[n] = rec_name[n]; n++; }
    name[n] = 0;
    len = n;
    if (len > 3 && name[len - 4] == '.' && (name[len - 3] & 0xdf) == 'B' && (name[len - 2] & 0xdf) == 'I' &&
        (name[len - 1] & 0xdf) == 'N')
        name[len - 4] = 0;
    pos = mh_str_find_i(name, " (TRACK ");
    if (pos >= 0) name[pos] = 0;
    while ((len = strlen(name)) != 0 && name[len - 1] == ' ') name[len - 1] = 0;
    mh_str_copy(out, MH_DISC_TITLE_MAX, name);
}

/* orig: db_verify_redump @0x1400171e0 */
bool mh_db_verify_redump(const MhDb *db, MhDisc *d, MhProgress *prog)
{
    uint8_t *buf;
    uint32_t total_crc;
    MhSha1 total_sha;
    uint8_t total_digest[20];
    uint64_t total_bytes = 0, done = 0;
    bool all_ok = true, any_found = false;
    char game[MH_DISC_TITLE_MAX];
    game[0] = 0;
    if (!d) return false;
    buf = mh_alloc(0x400000);
    if (!buf) return false;
    total_crc = mh_crc32_begin();
    mh_sha1_init(&total_sha);
    for (unsigned f = 0; f < d->nfiles; f++) total_bytes += d->files[f].size;
    for (unsigned f = 0; f < d->nfiles; f++) {
        MhIo io;
        uint32_t crc;
        MhSha1 sha;
        uint8_t digest[20];
        uint64_t pos = 0, size = d->files[f].size;
        bool ok = true;
        if (!mh_io_open(&io, d->files[f].path, MH_IO_READ, false, NULL)) { all_ok = false; continue; }
        crc = mh_crc32_begin();
        mh_sha1_init(&sha);
        while (pos < size) {
            size_t want = size - pos > 0x400000 ? 0x400000 : (size_t)(size - pos);
            if (!mh_io_read(&io, pos, buf, want)) { ok = false; break; }
            crc = mh_crc32_update(crc, buf, want);
            mh_sha1_update(&sha, buf, want);
            total_crc = mh_crc32_update(total_crc, buf, want);
            mh_sha1_update(&total_sha, buf, want);
            pos += want;
            done += want;
            if (prog && !mh_progress_report(prog, done, total_bytes, "Verifying BIN integrity")) {
                mh_io_close(&io);
                mh_free(buf);
                return false;
            }
        }
        mh_io_close(&io);
        if (!ok) { all_ok = false; continue; }
        crc = mh_crc32_end(crc);
        mh_sha1_final(&sha, digest);
        {
            bool match = false;
            if (db && db->redump_index && db->redump_count) {
                uint32_t lo = 0, hi = db->redump_count;
                const uint8_t *base = db->redump_index + 0x40;
                uint32_t rs = db->redump_rec_size;
                for (;;) { /* lower bound on the size field */
                    uint32_t mid = (hi - lo) / 2 + lo;
                    if (mh_rd64(base + (uint64_t)mid * rs) < size) {
                        lo = mid + 1;
                        if (hi <= lo) break;
                    } else {
                        if (mid <= lo) break;
                        hi = mid;
                    }
                }
                for (uint32_t j = lo; j < db->redump_count; j++) {
                    const uint8_t *r = base + (uint64_t)j * rs;
                    if (mh_rd64(r) != size) break;
                    if (mh_rd32(r + 8) == crc && memcmp(r + 0xc, digest, 20) == 0) {
                        if (game[0] == 0) redump_game_name((const char *)r + 0x20, game);
                        match = true;
                        break;
                    }
                }
            }
            if (match) any_found = true;
            else all_ok = false;
        }
    }
    d->crc32 = mh_crc32_end(total_crc);
    mh_sha1_final(&total_sha, total_digest);
    memcpy(d->sha1, total_digest, 20);
    mh_free(buf);
    if (all_ok && any_found) {
        d->flags = (d->flags & ~(uint32_t)MH_DF_INTEGRITY_BAD) | MH_DF_TRACKSET_CHECKED | MH_DF_TRACKSET_OK;
        if (d->db_index == 0xffffffffu && game[0]) mh_str_copy(d->title, sizeof d->title, game);
        cat_note(d, "\r\nIntegrity confirmed by the Redump DAT.");
        mh_disc_refresh_status(d);
        return true;
    }
    d->flags |= MH_DF_TRACKSET_CHECKED | MH_DF_INTEGRITY_BAD;
    cat_note(d, "\r\nThe hash was not found in the Redump DAT, or a BIN differs.");
    mh_disc_refresh_status(d);
    return false;
}
