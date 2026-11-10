/* Streams one disc into a V2 package, or analyses it (orig: stream_disc @0x1400312f0). */
#include "write_internal.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef __APPLE__
#include <dispatch/dispatch.h>
#endif

#define CHUNK_SECTORS 0x800u /* sectors per read: 0x800 * 0x930 = 0x498000, * 0xa00 = 0x500000 */

/* First sector of a track including its pregap. */
static uint32_t track_begin(const MhDisc *d, unsigned i)
{
    return d->tracks[i].start - d->tracks[i].pregap;
}

/* Track that holds `sector`, scanning forward from *cur (sectors arrive in order). */
static unsigned track_of(const MhDisc *d, unsigned *cur, uint32_t sector)
{
    while (*cur + 1u < d->ntracks && track_begin(d, *cur + 1u) <= sector) (*cur)++;
    return *cur;
}

/* The per sector check of the analysis pass: sectors from INDEX 01 of a data track must look
 * like the mode the CUE announces. Returns the verify_issues bits found, so slices of a chunk can be
 * checked on several threads. */
static uint32_t sector_issues(const MhDisc *d, unsigned *cur, uint32_t sector, const uint8_t *s)
{
    unsigned k = track_of(d, cur, sector);
    const MhTrack *t = &d->tracks[k];
    uint32_t bits = 0;
    int r, i;
    if (t->kind != 2 || t->start > sector) return 0;
    if (s[0] != 0 || s[0xb] != 0 || s[0xf] != t->mode) return 8;
    for (i = 1; i <= 10; i++)
        if (s[i] != 0xff) {
            bits |= 8;
            break;
        }
    r = mh_cd_sector_check(s);
    if (r & 1) bits |= 8;
    if (r & 2) bits |= 0x40;
    if (r & 4) bits |= 0x80;
    return bits;
}

static void check_sector(MhDisc *d, unsigned *cur, uint32_t sector, const uint8_t *s)
{
    d->verify_issues |= sector_issues(d, cur, sector, s);
}

static bool source_path_of(const MhDisc *d, char *out, size_t n)
{
    if (d->kind == MH_DISC_KIND_V2_IMAGE) {
        mh_str_copy(out, n, d->source_path);
        return true;
    }
    return snprintf(out, n, "/dev/rdisk%u", d->v2_drive) < (int)n;
}

static bool report(const MhV2Writer *w, uint64_t done, uint64_t total, const char *text)
{
    return mh_progress_report(w ? w->prog : NULL, done, total, text);
}

/* kind 4/5: the RAW slots come from a card or an image. */
static bool stream_raw_source(const MhV2Writer *w, MhIo *dst, MhDisc *d, uint8_t rec[MH_V2_DISC_SIZE],
                              uint64_t *done, uint64_t total, bool analyze, uint32_t *crc,
                              MhSha1 *whole, MhSha1 *trk)
{
    char path[MH_PATH_MAX];
    MhIo src;
    uint8_t *buf;
    uint32_t remaining = d->total_sectors;
    uint64_t src_off = d->v2_raw_lba512 << 9, out_off = mh_v2_rd64(rec + 0x4c) << 9;
    bool ok = false;

    if (d->v2_raw_blocks != (uint64_t)d->total_sectors * 5) return false;
    if (!source_path_of(d, path, sizeof path)) return false;
    if (!mh_io_open(&src, path, MH_IO_READ, false, dst ? dst->err : NULL)) return false;
    buf = mh_alloc(0x500000);
    if (!buf) goto out;
    while (remaining) {
        uint32_t chunk = remaining > CHUNK_SECTORS ? CHUNK_SECTORS : remaining, s;
        uint32_t first = d->total_sectors - remaining;
        uint64_t bytes = (uint64_t)chunk * MH_V2_SLOT;
        if (!mh_io_read(&src, src_off, buf, (size_t)bytes)) goto out;
        for (s = 0; s < chunk; s++) {
            const uint8_t *slot = buf + (size_t)s * MH_V2_SLOT;
            unsigned i;
            *crc = mh_crc32_update(*crc, slot, MH_V2_SECTOR);
            mh_sha1_update(whole, slot, MH_V2_SECTOR);
            for (i = MH_V2_SECTOR; i < MH_V2_SLOT; i++)
                if (slot[i]) {
                    if (!analyze) goto out;
                    d->verify_issues |= 0x20;
                }
        }
        if (analyze) {
            unsigned c1 = 0, c2 = 0;
            for (s = 0; s < chunk; s++)
                check_sector(d, &c1, first + s, buf + (size_t)s * MH_V2_SLOT);
            for (s = 0; s < chunk; s++) {
                unsigned k = track_of(d, &c2, first + s);
                mh_sha1_update(&trk[k], buf + (size_t)s * MH_V2_SLOT, MH_V2_SECTOR);
            }
        } else if (!mh_io_write(dst, out_off, buf, (size_t)bytes)) {
            goto out;
        }
        *done += bytes;
        remaining -= chunk;
        src_off += bytes;
        out_off += bytes;
        if (analyze) mh_v2_set_phase(w ? w->prog : NULL, MH_V2_PHASE_VERIFYING_SOURCES, 0, 0);
        if (w && w->prog &&
            !report(w, *done, total, analyze ? "Identifying trackset before writing"
                                             : "Keeping existing RAW Disc"))
            goto out;
        mh_progress_yield();
    }
    ok = true;
out:
    mh_free(buf);
    mh_io_close(&src);
    return ok;
}

/* Feeds the sectors of a chunk to the SHA-1 of the track each one belongs to (one call per run of sectors
 * of the same track, in order). */
static void hash_tracks(const MhDisc *d, uint32_t first, uint32_t chunk, const uint8_t *rd, MhSha1 *trk)
{
    unsigned cur = 0;
    uint32_t s = 0;
    while (s < chunk) {
        unsigned t = track_of(d, &cur, first + s);
        uint32_t end = chunk;
        if (t + 1u < d->ntracks && track_begin(d, t + 1u) - first < end) end = track_begin(d, t + 1u) - first;
        mh_sha1_update(&trk[t], rd + (size_t)s * MH_V2_SECTOR, (size_t)(end - s) * MH_V2_SECTOR);
        s = end;
    }
}

#define EDC_SLICES 32u

/* The analysis of one chunk of 2352 byte sectors. Four independent jobs: SHA-1 of the whole disc, CRC-32,
 * SHA-1 per track, and the sector checks (EDC/ECC, split in slices). On macOS they run on all cores through
 * libdispatch; elsewhere one after the other. The results do not depend on the order. */
static void analyze_chunk(MhDisc *d, uint32_t first, uint32_t chunk, const uint8_t *rd, uint32_t *crc,
                          MhSha1 *whole, MhSha1 *trk)
{
    size_t bytes = (size_t)chunk * MH_V2_SECTOR;
#ifdef __APPLE__
    __block uint32_t issues = 0;
    const MhDisc *dc = d;
    dispatch_queue_t q = dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0);
    dispatch_group_t g = dispatch_group_create();
    dispatch_group_async(g, q, ^{ mh_sha1_update(whole, rd, bytes); });
    dispatch_group_async(g, q, ^{ *crc = mh_crc32_update(*crc, rd, bytes); });
    dispatch_group_async(g, q, ^{ hash_tracks(dc, first, chunk, rd, trk); });
    dispatch_apply(EDC_SLICES, q, ^(size_t i) {
        uint32_t a = (uint32_t)((uint64_t)chunk * i / EDC_SLICES), b = (uint32_t)((uint64_t)chunk * (i + 1) / EDC_SLICES);
        unsigned cur = 0;
        uint32_t bits = 0, s;
        for (s = a; s < b; s++) bits |= sector_issues(dc, &cur, first + s, rd + (size_t)s * MH_V2_SECTOR);
        if (bits) __atomic_or_fetch(&issues, bits, __ATOMIC_RELAXED);
    });
    dispatch_group_wait(g, DISPATCH_TIME_FOREVER);
    dispatch_release(g);
    d->verify_issues |= issues;
#else
    unsigned cur = 0;
    uint32_t s;
    for (s = 0; s < chunk; s++) check_sector(d, &cur, first + s, rd + (size_t)s * MH_V2_SECTOR);
    *crc = mh_crc32_update(*crc, rd, bytes);
    mh_sha1_update(whole, rd, bytes);
    hash_tracks(d, first, chunk, rd, trk);
#endif
}

/* A read of the next chunk that runs while the current one is analysed. */
typedef struct ReadAhead {
    MhIo *src;
    uint64_t off;
    uint8_t *buf;
    size_t bytes;
    bool ok;
#ifdef __APPLE__
    dispatch_group_t group;
#endif
} ReadAhead;

static void read_ahead_start(ReadAhead *r, MhIo *src, uint64_t off, uint8_t *buf, size_t bytes)
{
    r->src = src;
    r->off = off;
    r->buf = buf;
    r->bytes = bytes;
    r->ok = true;
#ifdef __APPLE__
    r->group = dispatch_group_create();
    dispatch_group_async(r->group, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0),
                         ^{ r->ok = mh_io_read(r->src, r->off, r->buf, r->bytes); });
#endif
}

/* Waits for the read (does it, on platforms without threads). Safe to call when none was started. */
static bool read_ahead_finish(ReadAhead *r)
{
    if (!r->bytes) return true;
#ifdef __APPLE__
    dispatch_group_wait(r->group, DISPATCH_TIME_FOREVER);
    dispatch_release(r->group);
#else
    r->ok = mh_io_read(r->src, r->off, r->buf, r->bytes);
#endif
    r->bytes = 0;
    return r->ok;
}

#define READ_DEPTH 2u             /* chunks being read ahead at once: a USB disk gives more with several requests queued */
#define ANALYZE_BUFFERS (READ_DEPTH + 1u) /* the ones in flight plus the one being analysed */

/* kind 1, analysis only: the BIN files of the CUE are read READ_DEPTH chunks ahead and each chunk is analysed
 * on all cores (analyze_chunk). Same results as the sequential loop of stream_cue_source. */
static bool analyze_cue_source(const MhV2Writer *w, MhDisc *d, uint64_t *done, uint64_t total, uint32_t *crc,
                               MhSha1 *whole, MhSha1 *trk)
{
    uint8_t *buf[ANALYZE_BUFFERS] = {NULL};
    unsigned k, i;
    bool ok = false;
    for (i = 0; i < ANALYZE_BUFFERS; i++)
        if (!(buf[i] = mh_alloc(0x498000))) goto out;
    for (k = 0; k < d->nfiles; k++) {
        const MhDiscFile *f = &d->files[k];
        MhIo src;
        ReadAhead ra[ANALYZE_BUFFERS] = {{0}};
        uint32_t nchunks = (f->sectors + CHUNK_SECTORS - 1) / CHUNK_SECTORS, issued = 0, c;
        bool good = true;
        if (!mh_io_open(&src, f->path, MH_IO_READ, false, NULL)) goto out;
        for (c = 0; c < nchunks && good; c++) {
            uint32_t chunk = f->sectors - c * CHUNK_SECTORS > CHUNK_SECTORS ? CHUNK_SECTORS : f->sectors - c * CHUNK_SECTORS;
            uint32_t first = f->start + c * CHUNK_SECTORS;
            uint64_t wbytes = (uint64_t)chunk * MH_V2_SLOT;
            uint8_t *cur = buf[c % ANALYZE_BUFFERS];
            char base[MH_PATH_MAX];
            MhWbuf text;
            while (issued < nchunks && issued <= c + READ_DEPTH) {
                uint32_t n = f->sectors - issued * CHUNK_SECTORS > CHUNK_SECTORS ? CHUNK_SECTORS
                                                                                  : f->sectors - issued * CHUNK_SECTORS;
                read_ahead_start(&ra[issued % ANALYZE_BUFFERS], &src, (uint64_t)issued * CHUNK_SECTORS * MH_V2_SECTOR,
                                 buf[issued % ANALYZE_BUFFERS], (size_t)n * MH_V2_SECTOR);
                issued++;
            }
            if (!read_ahead_finish(&ra[c % ANALYZE_BUFFERS])) good = false;
            else if (first > d->total_sectors || chunk > d->total_sectors - first) good = false;
            if (!good) break;
            analyze_chunk(d, first, chunk, cur, crc, whole, trk);
            *done += wbytes;
            if (w && w->prog) {
                mh_v2_set_phase(w->prog, MH_V2_PHASE_VERIFYING_SOURCES, 0, 0);
                mh_path_basename(f->path, base, sizeof base);
                mh_wbuf_init(&text);
                mh_wbuf_puts(&text, "Verifying: ");
                mh_wbuf_puts(&text, base);
                good = report(w, *done, total, mh_wbuf_cstr(&text));
                mh_wbuf_free(&text);
            }
            mh_progress_yield();
        }
        for (i = 0; i < ANALYZE_BUFFERS; i++) read_ahead_finish(&ra[i]); /* no read may outlive its buffer */
        mh_io_close(&src);
        if (!good) goto out;
    }
    ok = true;
out:
    for (i = 0; i < ANALYZE_BUFFERS; i++) mh_free(buf[i]);
    return ok;
}

/* kind 1, writing: the sectors come from the BIN files of the CUE (the analysis is analyze_cue_source). */
static bool stream_cue_source(const MhV2Writer *w, MhIo *dst, MhDisc *d, uint8_t rec[MH_V2_DISC_SIZE],
                              uint64_t *done, uint64_t total, bool analyze, uint32_t *crc,
                              MhSha1 *whole, MhSha1 *trk)
{
    uint8_t *rd = mh_alloc(0x498000), *wr = mh_calloc(0x500000);
    uint64_t out_off = mh_v2_rd64(rec + 0x4c) << 9;
    unsigned k;
    bool ok = false;
    if (!rd || !wr) goto out;
    for (k = 0; k < d->nfiles; k++) {
        const MhDiscFile *f = &d->files[k];
        MhIo src;
        uint32_t remaining = f->sectors;
        uint64_t file_off = 0;
        if (!mh_io_open(&src, f->path, MH_IO_READ, false, dst ? dst->err : NULL)) goto out;
        while (remaining) {
            uint32_t chunk = remaining > CHUNK_SECTORS ? CHUNK_SECTORS : remaining, s;
            uint64_t rbytes = (uint64_t)chunk * MH_V2_SECTOR, wbytes = (uint64_t)chunk * MH_V2_SLOT;
            char base[MH_PATH_MAX];
            MhWbuf text;
            bool cont;
            if (!mh_io_read(&src, file_off, rd, (size_t)rbytes)) {
                mh_io_close(&src);
                goto out;
            }
            *crc = mh_crc32_update(*crc, rd, (size_t)rbytes);
            mh_sha1_update(whole, rd, (size_t)rbytes);
            memset(wr, 0, (size_t)wbytes);
            for (s = 0; s < chunk; s++)
                memcpy(wr + (size_t)s * MH_V2_SLOT, rd + (size_t)s * MH_V2_SECTOR, MH_V2_SECTOR);
            if (!mh_io_write(dst, out_off, wr, (size_t)wbytes)) {
                mh_io_close(&src);
                goto out;
            }
            out_off += wbytes;
            remaining -= chunk;
            file_off += rbytes;
            *done += wbytes;
            cont = true;
            if (w && w->prog) {
                mh_path_basename(f->path, base, sizeof base);
                mh_wbuf_init(&text);
                mh_wbuf_puts(&text, "Writing: ");
                mh_wbuf_puts(&text, base);
                cont = report(w, *done, total, mh_wbuf_cstr(&text));
                mh_wbuf_free(&text);
            }
            if (!cont) {
                mh_io_close(&src);
                goto out;
            }
            mh_progress_yield();
        }
        mh_io_close(&src);
    }
    ok = true;
out:
    mh_free(rd);
    mh_free(wr);
    return ok;
}

/* orig: stream_disc @0x1400312f0 */
bool mh_v2_stream_disc(const MhV2Writer *w, MhIo *dst, MhDisc *d, uint8_t rec[MH_V2_DISC_SIZE],
                       uint64_t *done, uint64_t total, MhV2StreamMode mode)
{
    uint32_t crc = mh_crc32_begin();
    MhSha1 whole, trk[MH_DISC_MAX_TRACKS];
    bool analyze = mode != MH_V2_STREAM_WRITE, raw;
    uint64_t dummy = 0;
    unsigned i;
    uint8_t sha[20];

    if (!done) done = &dummy;
    mh_sha1_init(&whole);
    if (!d) return false;
    if (analyze) {
        unsigned nt = d->ntracks;
        if (nt - 1u > 0x62) return false;
        for (i = 0; i < nt; i++)
            if (d->tracks[i].start < d->tracks[i].pregap) return false;
        if (d->tracks[0].start != d->tracks[0].pregap) return false;
        for (i = 0; i < nt; i++) {
            uint32_t b = track_begin(d, i);
            mh_sha1_init(&trk[i]);
            if (i + 1 < nt) {
                uint32_t nb = track_begin(d, i + 1);
                if (nb <= b || d->total_sectors < nb) return false;
            } else if (d->total_sectors <= b) {
                return false;
            }
        }
    }
    raw = d->kind == MH_DISC_KIND_V2_DEVICE || d->kind == MH_DISC_KIND_V2_IMAGE;
    if (raw) {
        if (!stream_raw_source(w, dst, d, rec, done, total, analyze, &crc, &whole, trk)) return false;
    } else if (d->nfiles != 0) {
        if (analyze ? !analyze_cue_source(w, d, done, total, &crc, &whole, trk)
                    : !stream_cue_source(w, dst, d, rec, done, total, analyze, &crc, &whole, trk))
            return false;
    }

    mh_v2_wr32(rec + 0x7c, mh_crc32_end(crc));
    mh_sha1_final(&whole, sha);
    memcpy(rec + 0x80, sha, 20);

    if (!analyze) {
        if (d->verified != 0)
            return mh_v2_rd32(rec + 0x7c) == d->verify_crc32 && memcmp(sha, d->verify_sha1, 20) == 0;
        return true;
    }
    {
        uint8_t hashes[MH_DISC_MAX_TRACKS][20];
        const MhDb *db = w && w->env ? w->env->db : NULL;
        for (i = 0; i < d->ntracks; i++) mh_sha1_final(&trk[i], hashes[i]);
        if (db) mh_db_match_trackset(db, d, &hashes[0][0], d->ntracks);
        if (db && d->cheats == NULL && d->ps.status == MH_PSS_TRACKSET && d->serial[0]) {
            const char *title = mh_db_record_title(db, d->db_index);
            const MhCheatDb *cdb = w->env->cheats ? w->env->cheats : mh_cheat_db_active();
            MhCheatProfile *prof = NULL;
            char err[0x100];
            if (title && title[0] && cdb &&
                mh_cheat_profile_build_for_disc(cdb, d->serial, &prof, err, sizeof err)) {
                d->cheats = prof;
                mh_str_cat(d->notes, MH_DISC_NOTES_MAX,
                           "\r\nFull cheat catalog linked after the exact trackset was confirmed.");
            }
        }
    }
    if (raw) {
        if (mh_v2_rd32(rec + 0x7c) == d->crc32 && memcmp(sha, d->sha1, 20) == 0) return true;
        d->verify_issues |= 0x10;
    }
    return true;
}
