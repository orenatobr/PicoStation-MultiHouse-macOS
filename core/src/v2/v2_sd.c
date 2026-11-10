/* v2_sd.c: header slot publishing and the SD front ends (orig: sd_* functions, v2_publish_header_slot,
 * v2_invalidate_header_slot, clear_backup_gpt_tail, compare_region, verify_committed_headers_after_reopen,
 * sd_write_image, sd_verify_image, sd_quick_health). */
#include <stdlib.h>
#include <time.h>

#include "mh/v2write.h"
#include "v2_internal.h"

/* ------------------------------------------------------------------ header validity */

/* orig: sd_v2_header_raw_valid @0x14002ec70 */
bool mh_sd_v2_header_raw_valid(const uint8_t *h)
{
    unsigned i;
    if (memcmp(h, MH_V2_MAGIC, 6) != 0 || h[6] != 2 || !(h[7] & 1)) return false;
    if (mh_v2_rd32(h + 0x10) != 0x800 || mh_v2_rd64(h + 0x14) != 5 || mh_v2_rd64(h + 0x1c) != 10 ||
        mh_v2_rd64(h + 0x24) != 0x32 || mh_v2_rd64(h + 0x2c) != 0xb4 || mh_v2_rd64(h + 0x34) != 0x14b4 ||
        mh_v2_rd32(h + 0x3c) - 1u >= 0x5d || mh_v2_rd64(h + 0x40) != 0x1685 || mh_v2_rd64(h + 0x48) != 0x2800)
        return false;
    if (mh_v2_rd32(h + 0x92c) != mh_crc32(h, 0x92c)) return false;
    for (i = 0x930; i < 0xa00; i++)
        if (h[i]) return false;
    return true;
}

/* ------------------------------------------------------------------ v2_* (error record) */

/* orig: v2_invalidate_header_slot @0x140036a50 */
bool mh_v2_invalidate_header_slot(MhIo *io, uint64_t off)
{
    uint8_t blk[512], zero[512];
    if (!mh_io_read(io, off, blk, sizeof blk)) return false;
    if (memcmp(blk, MH_V2_MAGIC, 6) != 0) return true;
    memset(zero, 0, sizeof zero);
    return mh_io_write_block_verified(io, off, zero);
}

/* orig: v2_publish_header_slot @0x140038830 */
bool mh_v2_publish_header_slot(MhIo *io, uint64_t off, const uint8_t *hdr)
{
    uint8_t back[MH_V2_HDR_SLOT];
    unsigned i;
    if (!hdr) return false;
    for (i = 1; i < 5; i++)
        if (!mh_io_write_block_verified(io, off + (uint64_t)i * 512, hdr + (size_t)i * 512)) return false;
    if (!mh_io_write_block_verified(io, off, hdr)) return false;
    if (!mh_io_read(io, off, back, sizeof back)) return false;
    return memcmp(hdr, back, sizeof back) == 0;
}

/* ------------------------------------------------------------------ sd_* (own retries) */

/* orig: sd_write_block_verified @0x14002ef50: 8 write+flush+readback attempts, 30, 65, ... 275 ms */
bool mh_sd_write_block_verified(MhIo *io, uint64_t off, const void *blk)
{
    unsigned wait;
    if (off & 0x1ff) return false;
    for (wait = 30; wait != 0x136; wait += 35) {
        uint8_t back[512];
        if (mh_v2_write_retry_once(io, off, blk, 512) && mh_v2_flush(io) && mh_v2_read_retry(io, off, back, 512) &&
            memcmp(blk, back, 512) == 0)
            return true;
        mh_v2_sleep_ms(wait);
        mh_progress_yield();
    }
    return false;
}

/* orig: sd_invalidate_v2_header @0x14002e380 */
bool mh_sd_invalidate_v2_header(MhIo *io, uint64_t off)
{
    uint8_t blk[512], zero[512];
    if (!mh_v2_read_retry(io, off, blk, sizeof blk)) return false;
    if (memcmp(blk, MH_V2_MAGIC, 6) != 0) return true;
    memset(zero, 0, sizeof zero);
    return mh_sd_write_block_verified(io, off, zero);
}

/* orig: sd_publish_v2_header @0x14002e470 */
bool mh_sd_publish_v2_header(MhIo *io, uint64_t off, const uint8_t *hdr)
{
    uint8_t back[MH_V2_HDR_SLOT];
    unsigned i;
    for (i = 1; i < 5; i++)
        if (!mh_sd_write_block_verified(io, off + (uint64_t)i * 512, hdr + (size_t)i * 512)) return false;
    if (!mh_sd_write_block_verified(io, off, hdr)) return false;
    if (!mh_v2_read_retry(io, off, back, sizeof back)) return false;
    return memcmp(hdr, back, sizeof back) == 0;
}

/* orig: clear_backup_gpt_tail @0x140010d30 */
bool mh_clear_backup_gpt_tail(MhIo *io, uint64_t dev_size, uint64_t used, char *err, size_t errn)
{
    uint64_t sz = dev_size & ~(uint64_t)0x1ff, lim = used > 0x1fe ? used : 0x1ff, start, off;
    uint8_t blk[512], zero[512];
    if (lim >= sz) return true;
    if (!mh_v2_read_retry(io, sz - 512, blk, sizeof blk)) {
        mh_str_copy(err, errn, "Could not check the last sector of the card.");
        return false;
    }
    if (memcmp(blk, MH_V2_GPT_SIG, 8) != 0) return true;
    start = (sz > 0x100000 ? sz : 0x100000) - 0x100000;
    if (start < used) start = (used + 0x1ff) & ~(uint64_t)0x1ff;
    if (start >= sz) return true;
    memset(zero, 0, sizeof zero);
    for (off = start; off < sz; off += 512) {
        unsigned i;
        if (!mh_v2_read_retry(io, off, blk, sizeof blk)) {
            mh_str_copy(err, errn, "Could not read the backup GPT area.");
            return false;
        }
        for (i = 0; i < 512; i++)
            if (blk[i]) {
                if (!mh_sd_write_block_verified(io, off, zero)) {
                    mh_str_copy(err, errn, "Could not remove a backup GPT sector with a 512-byte read-back.");
                    return false;
                }
                break;
            }
        mh_progress_yield();
    }
    return true;
}

/* orig: compare_region @0x140012620 */
bool mh_v2_compare_region(MhIo *a, MhIo *b, uint64_t off, uint64_t len, MhProgress *prog,
                          uint64_t done_base, uint64_t total, char *err, size_t errn)
{
    uint8_t *b1 = malloc(0x40000), *b2 = malloc(0x40000);
    uint64_t done = 0;
    if (!b1 || !b2) {
        free(b1);
        free(b2);
        mh_str_copy(err, errn, "Not enough memory to verify.");
        return false;
    }
    while (done < len) {
        size_t n = len - done > 0x40000 ? 0x40000 : (size_t)(len - done);
        if (!mh_v2_read_retry(a, off + done, b1, n) || !mh_v2_read_retry(b, off + done, b2, n)) {
            free(b1);
            free(b2);
            mh_str_copy(err, errn, "Persistent short read during read-back after 8 attempts at the same offset.");
            return false;
        }
        if (memcmp(b1, b2, n) != 0) {
            free(b1);
            free(b2);
            mh_str_copy(err, errn, "The card read-back differs from the image.");
            return false;
        }
        done += n;
        mh_v2_set_phase(prog, MH_V2_PHASE_VERIFYING_CARD, 0, 0);
        if (!mh_progress_report(prog, done + done_base, total, "Comparing the card byte by byte")) {
            free(b1);
            free(b2);
            mh_str_copy(err, errn, "Verification cancelled.");
            return false;
        }
        mh_progress_yield();
    }
    free(b1);
    free(b2);
    return true;
}

/* orig: verify_committed_headers_after_reopen @0x14003da90 */
bool mh_v2_verify_committed_headers(const MhDiskInfo *disk, const MhDiskOpts *opts,
                                    const uint8_t *expected, char *err, size_t errn)
{
    uint8_t a[MH_V2_HDR_SLOT], b[MH_V2_HDR_SLOT];
    char e2[256];
    MhBlockDev *dev;
    bool ok;
    mh_v2_sleep_ms(250);
    e2[0] = 0;
    dev = mh_blockdev_open_read(disk, opts, e2, sizeof e2);
    if (!dev) {
        mh_str_copy(err, errn, "The headers were written, but the card could not be reopened for the final verification.");
        return false;
    }
    ok = mh_v2_read_retry(&dev->io, 0, a, sizeof a) && mh_v2_read_retry(&dev->io, 0xa00, b, sizeof b);
    mh_blockdev_close(dev);
    if (ok && mh_sd_v2_header_raw_valid(a) && mh_sd_v2_header_raw_valid(b) && memcmp(a, b, sizeof a) == 0 &&
        memcmp(a, expected, sizeof a) == 0)
        return true;
    mh_str_copy(err, errn,
                "The V2 headers changed after closing and reopening the card. The operation was not considered safe; replace "
                "the USB reader.");
    return false;
}

/* orig: sd_verify_image @0x14002ed50 */
bool mh_sd_verify_image(const MhDiskInfo *disk, const MhDiskOpts *opts, const char *image,
                        MhProgress *prog, char *err, size_t errn)
{
    MhDiskInfo fresh;
    MhIo f;
    MhBlockDev *dev;
    uint64_t size;
    char e2[256];
    bool ok;
    if (err && errn) err[0] = 0;
    if (!mh_disk_identity_matches(disk, opts, &fresh)) {
        mh_str_copy(err, errn, "The selected card changed or is not safe.");
        return false;
    }
    if (!mh_io_open(&f, image, MH_IO_READ, false, NULL)) {
        mh_str_copy(err, errn, "The image is invalid or larger than the card.");
        return false;
    }
    if (!mh_io_size(&f, &size) || fresh.size < size) {
        mh_str_copy(err, errn, "The image is invalid or larger than the card.");
        mh_io_close(&f);
        return false;
    }
    e2[0] = 0;
    dev = mh_blockdev_open_read(disk, opts, e2, sizeof e2);
    if (!dev) {
        mh_str_copy(err, errn, "Could not open the card for verification.");
        mh_io_close(&f);
        return false;
    }
    ok = mh_v2_compare_region(&f, &dev->io, 0, size, prog, 0, size, err, errn);
    mh_io_close(&f);
    mh_blockdev_close(dev);
    return ok;
}

/* orig: sd_write_image @0x14002f0d0 */
bool mh_sd_write_image(const MhDiskInfo *disk, const MhDiskOpts *opts, const char *image,
                       MhProgress *prog, char *err, size_t errn)
{
    MhDiskInfo fresh;
    MhIo f;
    MhBlockDev *dev;
    uint64_t size, pos;
    uint8_t first[0x1400], prev[MH_V2_HDR_SLOT], *buf = NULL;
    bool have_prev = false, ok = false;
    char e2[256];
    const char *m;

    if (err && errn) err[0] = 0;
    if (!mh_disk_identity_matches(disk, opts, &fresh)) {
        mh_str_copy(err, errn, "The card changed, was removed, or failed the identity check.");
        return false;
    }
    if (!mh_io_open(&f, image, MH_IO_READ, false, NULL)) {
        mh_str_copy(err, errn, "The image is invalid or does not fit on the card.");
        return false;
    }
    if (!mh_io_size(&f, &size) || size < 0x1400 || fresh.size < size) {
        mh_str_copy(err, errn, "The image is invalid or does not fit on the card.");
        mh_io_close(&f);
        return false;
    }
    if (!mh_v2_read_retry(&f, 0, first, sizeof first) || !mh_sd_v2_header_raw_valid(first) ||
        !mh_sd_v2_header_raw_valid(first + 0xa00) || memcmp(first, first + 0xa00, 0xa00) != 0) {
        mh_str_copy(err, errn, "The image does not have a valid, identical pair of V2 headers.");
        mh_io_close(&f);
        return false;
    }
    e2[0] = 0;
    dev = mh_blockdev_open_write(disk, opts, e2, sizeof e2);
    if (!dev) {
        mh_str_copy(err, errn, e2[0] ? e2 : "Could not open the card for writing. Eject its volumes in Finder and close any app using it, then try again.");
        mh_io_close(&f);
        return false;
    }
    /* remember a previous valid header (A, else B) to restore it if the invalidation half fails */
    if (mh_v2_read_retry(&dev->io, 0, prev, sizeof prev) && mh_sd_v2_header_raw_valid(prev))
        have_prev = true;
    else if (mh_v2_read_retry(&dev->io, 0xa00, prev, sizeof prev) && mh_sd_v2_header_raw_valid(prev))
        have_prev = true;

    if (!mh_sd_invalidate_v2_header(&dev->io, 0)) {
        mh_str_copy(err, errn,
                    "The USB reader did not confirm the sector-level invalidation of header A. No image data was written.");
        goto cleanup;
    }
    if (!mh_sd_invalidate_v2_header(&dev->io, 0xa00)) {
        m = "Invalidating header B failed and there was no recoverable previous header. Replace the USB reader.";
        if (have_prev) {
            mh_sd_publish_v2_header(&dev->io, 0, prev);
            m = "Invalidating header B failed; the previous header A was restored. Replace the USB reader.";
        }
        mh_str_copy(err, errn, m);
        goto cleanup;
    }
    buf = malloc(0x40000);
    if (!buf) {
        mh_str_copy(err, errn, "Not enough memory.");
        goto cleanup;
    }
    for (pos = 0x1400; pos < size;) {
        size_t n = size - pos > 0x40000 ? 0x40000 : (size_t)(size - pos);
        if (!mh_v2_read_retry(&f, pos, buf, n) || !mh_v2_write_retry(&dev->io, pos, buf, n)) {
            mh_str_copy(err, errn, "Persistent failure while writing after 8 attempts at the same offset.");
            goto cleanup;
        }
        pos += n;
        mh_v2_set_phase(prog, MH_V2_PHASE_WRITING_DISCS, 0, 0);
        if (!mh_progress_report(prog, pos, size, "Writing PSXRAW V2 to the card")) {
            mh_str_copy(err, errn, "Write cancelled. The headers stayed invalid.");
            goto cleanup;
        }
        mh_progress_yield();
    }
    if (!mh_v2_flush(&dev->io)) {
        mh_str_copy(err, errn, "Could not confirm the data on the card.");
        goto cleanup;
    }
    if (!mh_v2_compare_region(&f, &dev->io, 0x1400, size - 0x1400, prog, 0x1400, size, err, errn)) goto cleanup;
    if (!mh_clear_backup_gpt_tail(&dev->io, fresh.size, size, err, errn)) goto cleanup;
    if (!mh_sd_publish_v2_header(&dev->io, 0xa00, first + 0xa00)) {
        mh_str_copy(err, errn,
                    "The body was read back, but the sector-level publication of header B failed. The card stayed uncommitted.");
        goto cleanup;
    }
    if (!mh_sd_publish_v2_header(&dev->io, 0, first)) {
        mh_str_copy(err, errn,
                    "Header B is valid, but header A failed. The card can still be recovered from backup B.");
        goto cleanup;
    }
    mh_str_copy(err, errn, "Card written, read back and confirmed, with headers B and A published sector by sector.");
    ok = true;
cleanup:
    free(buf);
    mh_io_close(&f);
    if (ok) mh_blockdev_refresh_properties(dev);
    mh_blockdev_close(dev);
    if (ok) ok = mh_v2_verify_committed_headers(disk, opts, first, err, errn);
    return ok;
}

/* orig: sd_quick_health @0x14002e590 */
bool mh_sd_quick_health(const MhDiskInfo *disk, const MhDiskOpts *opts, MhProgress *prog, MhSdHealth *out)
{
    MhDiskInfo fresh;
    MhBlockDev *dev;
    uint8_t *b1, *b2;
    uint64_t regions[4], total = 0;
    struct timespec t0, t1;
    unsigned r;
    char e2[128];

    if (out) memset(out, 0, sizeof *out);
    if (!mh_disk_identity_matches(disk, opts, &fresh)) {
        if (out) mh_str_copy(out->text, sizeof out->text, "The card changed or is no longer connected.");
        return false;
    }
    e2[0] = 0;
    dev = mh_blockdev_open_read(disk, opts, e2, sizeof e2);
    if (!dev) {
        if (out) mh_str_copy(out->text, sizeof out->text, "Could not open the card for reading.");
        return false;
    }
    b1 = malloc(0x100000);
    b2 = malloc(0x100000);
    if (!b1 || !b2) {
        free(b1);
        free(b2);
        mh_blockdev_close(dev);
        return false;
    }
    regions[0] = 0;
    regions[1] = fresh.size >> 2;
    regions[2] = fresh.size >> 1;
    regions[3] = fresh.size > 0x200000 ? fresh.size - 0x100000 : 0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (r = 0; r < 4; r++) {
        uint64_t off = regions[r] & ~(uint64_t)0x1ff;
        uint64_t len = fresh.size - off;
        uint64_t pos;
        bool bad = false;
        if (len < 0x100000) {
            if (len < 0x200) goto next;
            len &= ~(uint64_t)0x1ff;
        } else {
            len = 0x100000;
        }
        for (pos = 0; pos < len && !bad;) {
            size_t n = len - pos > 0x40000 ? 0x40000 : (size_t)(len - pos);
            if (!mh_v2_read_retry(&dev->io, off + pos, b1 + pos, n)) bad = true;
            pos += n;
        }
        for (pos = 0; pos < len && !bad;) {
            size_t n = len - pos > 0x40000 ? 0x40000 : (size_t)(len - pos);
            if (!mh_v2_read_retry(&dev->io, off + pos, b2 + pos, n)) bad = true;
            pos += n;
        }
        if (bad) {
            if (out) out->read_errors++;
            goto next;
        }
        total += len * 2;
        if (memcmp(b1, b2, (size_t)len) != 0 && out) out->mismatches++;
        mh_v2_set_phase(prog, MH_V2_PHASE_VERIFYING_CARD, 0, 0);
        if (!mh_progress_report(prog, r + 1, 4, "Card read test")) {
            free(b1);
            free(b2);
            mh_blockdev_close(dev);
            if (out) mh_str_copy(out->text, sizeof out->text, "Test cancelled.");
            return false;
        }
        mh_progress_yield();
    next:;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    free(b1);
    free(b2);
    mh_blockdev_close(dev);
    if (!out) return total != 0;
    out->bytes = total;
    {
        uint64_t ns = (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ull + (uint64_t)(t1.tv_nsec - t0.tv_nsec);
        if (ns != 0) out->speed_x100 = (uint32_t)(mh_u64_div(1000000000ull * total * 100, ns * 0x100000ull));
    }
    if (out->read_errors == 0 && out->mismatches == 0 && total != 0) {
        out->ok = 1;
        mh_str_copy(out->text, sizeof out->text, "Repeated reads passed in four regions of the card.");
    } else {
        out->ok = 0;
        mh_str_copy(out->text, sizeof out->text,
                    "The test found a persistent read error or a mismatch between repeated reads.");
    }
    return out->ok != 0;
}

/* ------------------------------------------------------------------ resume */

/* orig: confirm_session_marker @0x140012d60 */
bool mh_v2_confirm_session_marker(MhIo *io, const MhDisc *d, uint64_t card_bytes)
{
    uint8_t blk[512];
    uint64_t lba;
    if (d->v2_pkg_path[0] == 0 && d->v2_card_blocks != card_bytes) return false;
    lba = d->v2_pkg_b + 0xff;
    if (lba >= card_bytes / 0x200) return false;
    if (!mh_io_read(io, lba * 0x200, blk, sizeof blk)) return false;
    return memcmp(blk, d->v2_marker, 0x200) == 0;
}

/* orig: v2_confirm_resume_target @0x140035680 */
bool mh_v2_confirm_resume_target(const MhProject *p, MhIo *io, int32_t disk_number, const char *path,
                                 uint64_t card_bytes, MhV2Result *res)
{
    unsigned i, k;
    if (!p || !io || io->fd < 0) return false;
    for (i = 0; i < MH_PROJECT_MAX_CASES; i++) {
        const MhCase *c = &p->cases[i];
        if (!c->in_use) continue;
        for (k = 0; k < c->disc_count && k < MH_CASE_MAX_DISCS; k++) {
            const MhDisc *d = c->discs[k];
            bool match = false;
            if (!d || !(d->flags & MH_DF_KEEP_PACKAGE)) continue;
            if (disk_number == -1) {
                if (path && path[0] && d->v2_pkg_path[0] && mh_path_equal_full(d->v2_pkg_path, path)) match = true;
            } else if (!path || !path[0]) {
                if (d->v2_pkg_path[0] == 0 && disk_number == d->v2_slot) match = true;
            } else if (d->v2_pkg_path[0] && mh_path_equal_full(d->v2_pkg_path, path)) {
                match = true;
            }
            if (match && !mh_v2_confirm_session_marker(io, d, card_bytes)) {
                if (res) {
                    res->ok = 0;
                    mh_str_copy(res->msg, sizeof res->msg,
                                "The target does not match the resume point. Reconnect the same card used in this session.");
                }
                return false;
            }
        }
    }
    return true;
}

/* orig: sd_write_project_direct @0x14002fae0 */
bool mh_sd_write_project_direct(const MhDiskInfo *disk, const MhDiskOpts *opts, MhProject *project,
                                bool verify_full, const MhV2Writer *w, char *err, size_t errn, MhV2Result *res)
{
    MhDiskInfo fresh;
    MhV2Result local;
    MhBlockDev *dev;
    uint64_t need;
    uint8_t hdr[MH_V2_HDR_SLOT];
    bool ok = false, committed = false;
    char e2[256];
    const char *m;

    if (err && errn) err[0] = 0;
    if (res) memset(res, 0, sizeof *res);
    memset(&local, 0, sizeof local);
    if (!disk || !project || !mh_disk_identity_matches(disk, opts, &fresh)) {
        mh_str_copy(err, errn, "The card changed, was removed, or failed the identity check.");
        return false;
    }
    need = mh_v2_estimate_project_bytes_for_target(project, (int32_t)disk->number, NULL);
    if (need == 0 || fresh.size < need) {
        mh_str_copy(err, errn, "The games do not fit on the selected card, or the RAW extents are invalid.");
        return false;
    }
    e2[0] = 0;
    dev = mh_blockdev_open_write(disk, opts, e2, sizeof e2);
    if (!dev) {
        mh_str_copy(err, errn, e2[0] ? e2 : "Could not open the card for writing. Eject its volumes in Finder and close any app using it, then try again.");
        return false;
    }
    if (!mh_v2_prepare_write(project, &local) ||
        !mh_v2_confirm_resume_target(project, &dev->io, (int32_t)disk->number, NULL, fresh.size, &local)) {
        mh_str_copy(err, errn, local.msg);
        goto cleanup;
    }
    if (!mh_clear_backup_gpt_tail(&dev->io, fresh.size, need, err, errn)) goto cleanup;
    if (!mh_v2_write_handle_direct(w, project, &dev->io, (int32_t)disk->number, NULL, fresh.size, &local)) {
        mh_str_copy(err, errn, local.msg[0] ? local.msg : "Direct V2 write failed.");
        goto cleanup;
    }
    if (!mh_v2_read_retry(&dev->io, 0, hdr, sizeof hdr) || !mh_sd_v2_header_raw_valid(hdr)) {
        mh_str_copy(err, errn, "Header A was not valid just before closing the card.");
        goto cleanup;
    }
    m = verify_full ? "Card updated directly and fully verified."
                    : "Card updated in quick mode; metadata and headers were confirmed.";
    mh_str_copy(err, errn, m);
    if (res) *res = local;
    mh_v2_flush(&dev->io);
    committed = true;
cleanup:
    if (committed) mh_blockdev_refresh_properties(dev);
    mh_blockdev_close(dev);
    if (!committed) return false;
    if (w) mh_v2_set_phase(w->prog, MH_V2_PHASE_VERIFYING_CARD, 0, 0);
    ok = mh_v2_verify_committed_headers(disk, opts, hdr, err, errn);
    if (!ok && res) res->ok = 0;
    if (ok && w && w->prog) {
        mh_v2_set_phase(w->prog, MH_V2_PHASE_DONE, 0, 0);
        mh_progress_report(w->prog, 1, 1, "Done");
    }
    return ok;
}
