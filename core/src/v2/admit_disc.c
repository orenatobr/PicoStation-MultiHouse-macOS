/* Admission and verification of discs (orig: v2_check_disc_source, v2_verify_disc_source,
 * v2_admit_disc_without_full_check, manager_admit, verification_issue_text, auto_attach_cheats). */
#include "write_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

/* Win32 numbers the original printed for the common open failures. */
int mh_v2w_win32_code(int err)
{
    switch (err) {
    case ENOENT: return 2;
    case EACCES: case EPERM: return 5;
    case EBUSY: return 32;
    default: return err;
    }
}

/* orig: verification_issue_text @0x14003d930 */
void mh_v2_verification_issue_text(MhDisc *d)
{
    MhWbuf w;
    uint32_t v = d->verify_issues;
    mh_wbuf_init(&w);
    if (v & 0xc0)
        mh_wbuf_puts(&w, "Some sectors are inconsistent. Videos or audio may fail. You can create a copy with recalculated EDC/ECC.\r\n");
    if (v & 1) mh_wbuf_puts(&w, "Could not confirm the game ID.\r\n");
    if (v & 2) mh_wbuf_puts(&w, "The Disc differs from the known version; it may be a modification.\r\n");
    if (v & 4) mh_wbuf_puts(&w, "The size reported by the game exceeds the file.\r\n");
    if (v & 8) mh_wbuf_puts(&w, "The format of some sectors does not match the CUE.\r\n");
    if (v & 0x10) mh_wbuf_puts(&w, "The data on the card does not match the Disc record.\r\n");
    if (v & 0x20) mh_wbuf_puts(&w, "The card has invalid extra data. Restore this Disc.\r\n");
    if (v == 0) mh_wbuf_puts(&w, "Disc verified: read and structure OK.");
    mh_str_copy(d->verify_text, MH_V2W_MSG_MAX, w.data ? w.data : "");
    mh_wbuf_free(&w);
}

static void raw_path(const MhDisc *d, char *out, size_t n)
{
    if (d->kind == MH_DISC_KIND_V2_IMAGE) mh_str_copy(out, n, d->source_path);
    else snprintf(out, n, "/dev/rdisk%u", d->v2_drive);
}

/* orig: v2_check_disc_source @0x140035290 */
bool mh_v2_check_disc_source(const MhV2Writer *w, MhDisc *d)
{
    static const char incomplete[] = "Incomplete structure: provide the CUE and all readable BIN files.";
    char path[MH_PATH_MAX];
    uint8_t rec[MH_V2_DISC_SIZE];
    uint64_t done = 0;
    bool raw;

    if (!d) return false;
    d->verified = 0; /* verified_aux is left alone, as in the original */
    d->verify_text[0] = 0;
    d->verify_issues = 0;
    d->verify_accepted = 0;
    if (d->status == MH_DISC_STATUS_FIXABLE || d->status == MH_DISC_STATUS_ERROR ||
        d->ntracks - 1u > 0x62 || d->total_sectors == 0) {
        mh_str_copy(d->verify_text, MH_V2W_MSG_MAX, incomplete);
        return false;
    }
    raw = d->kind == MH_DISC_KIND_V2_DEVICE || d->kind == MH_DISC_KIND_V2_IMAGE;
    path[0] = 0;
    if (raw) {
        if (d->v2_raw_blocks != (uint64_t)d->total_sectors * 5) {
            mh_str_copy(d->verify_text, MH_V2W_MSG_MAX, incomplete);
            return false;
        }
        raw_path(d, path, sizeof path);
    } else {
        if (!(d->flags & MH_DF_STRUCT_OK) || d->nfiles == 0) {
            mh_str_copy(d->verify_text, MH_V2W_MSG_MAX, incomplete);
            return false;
        }
        if (d->kind == MH_DISC_KIND_CUE) {
            mh_disc_scan_serial(d);
            if (d->ps.status == MH_PSS_PROBABLE) {
                d->flags &= ~(uint32_t)MH_DF_SERIAL;
                d->serial[0] = 0;
            }
        } else {
            raw_path(d, path, sizeof path);
        }
    }
    if (path[0]) {
        MhIo probe;
        if (!mh_io_open(&probe, path, MH_IO_READ, false, NULL)) {
            int code = mh_v2w_win32_code(errno);
            MhWbuf t;
            mh_wbuf_init(&t);
            mh_wbuf_puts(&t, "Could not open ");
            mh_wbuf_puts(&t, path);
            mh_wbuf_puts(&t, " (error ");
            mh_wbuf_u32(&t, (uint32_t)code);
            mh_wbuf_puts(&t, "). ");
            mh_wbuf_puts(&t, code == 5 ? "Access denied. Allow the app to access this location in System Settings, Privacy & Security."
                             : code == 0x20 ? "The source is in use by another process."
                                            : "Reconnect the source and reload it.");
            mh_str_copy(d->verify_text, MH_V2W_MSG_MAX, t.data ? t.data : "");
            mh_wbuf_free(&t);
            return false;
        }
        mh_io_close(&probe);
        if (d->serial[0] == 0) {
            uint32_t st = mh_psserial_extract_raw(&d->ps, path, d->v2_raw_lba512, d->total_sectors);
            d->ps.status_saved = st;
            d->ps.source_saved = d->ps.source;
            mh_str_copy(d->ps.message_saved, sizeof d->ps.message_saved, d->ps.message);
            if (st == MH_PSS_STRONG) {
                mh_str_copy(d->serial, sizeof d->serial, d->ps.serial);
                d->flags |= MH_DF_SERIAL;
            }
        }
    }
    memset(rec, 0, sizeof rec);
    if (!mh_v2_stream_disc(w, NULL, d, rec, &done, (uint64_t)d->total_sectors * MH_V2_SLOT,
                           MH_V2_STREAM_ANALYZE)) {
        mh_str_copy(d->verify_text, MH_V2W_MSG_MAX,
                    "The full read failed or was cancelled. Nothing was written.");
        return false;
    }
    if (d->serial[0] == 0) d->verify_issues |= 1;
    if (!(d->flags & MH_DF_IDENTITY)) d->verify_issues |= 2;
    if (d->total_sectors < d->ps.iso_sectors) d->verify_issues |= 4;
    d->verify_crc32 = mh_v2_rd32(rec + 0x7c);
    memcpy(d->verify_sha1, rec + 0x80, 20);
    d->verified = 1;
    d->verified_aux = 0;
    mh_v2_verification_issue_text(d);
    d->status = d->verify_issues == 0 ? MH_DISC_STATUS_OK : MH_DISC_STATUS_NOTE;
    return true;
}

static void out_msg(char *msg, size_t n, const char *text)
{
    if (msg && n) mh_str_copy(msg, n, text);
}

/* orig: v2_verify_disc_source @0x14003a390 */
bool mh_v2_verify_disc_source(const MhV2Writer *w, const MhDisc *d, char *msg, size_t msgn)
{
    char path[MH_PATH_MAX];
    MhIo src;
    uint8_t *buf;
    uint32_t crc = mh_crc32_begin(), remaining;
    MhSha1 sha;
    uint8_t digest[20];
    uint64_t total, done = 0, off;
    bool ok = false;

    if (msg && msgn) msg[0] = 0;
    if (!d || (d->kind != MH_DISC_KIND_V2_DEVICE && d->kind != MH_DISC_KIND_V2_IMAGE) ||
        d->v2_raw_lba512 == 0 || d->v2_raw_blocks == 0 ||
        d->v2_raw_blocks != (uint64_t)d->total_sectors * 5) {
        out_msg(msg, msgn, "This Disc has no verifiable V2 RAW source.");
        return false;
    }
    raw_path(d, path, sizeof path);
    if (!mh_io_open(&src, path, MH_IO_READ, false, NULL)) {
        int code = mh_v2w_win32_code(errno);
        MhWbuf t;
        mh_wbuf_init(&t);
        mh_wbuf_puts(&t, "Could not open ");
        mh_wbuf_puts(&t, path);
        mh_wbuf_puts(&t, " (error ");
        mh_wbuf_u32(&t, (uint32_t)code);
        mh_wbuf_puts(&t, "). ");
        mh_wbuf_puts(&t, code == 5 ? "Access denied. Allow the app to access this location in System Settings, Privacy & Security."
                         : code == 0x20 ? "The source is in use by another process."
                                        : "Check that the source is still connected, then refresh and reload the card.");
        out_msg(msg, msgn, t.data ? t.data : "");
        mh_wbuf_free(&t);
        return false;
    }
    buf = mh_alloc(0x500000);
    if (!buf) {
        out_msg(msg, msgn, "Could not position the Disc read.");
        mh_io_close(&src);
        return false;
    }
    mh_sha1_init(&sha);
    remaining = d->total_sectors;
    total = (uint64_t)remaining * MH_V2_SLOT;
    off = d->v2_raw_lba512 << 9;
    while (remaining) {
        uint32_t chunk = remaining > 0x800 ? 0x800 : remaining, s;
        size_t bytes = (size_t)chunk * MH_V2_SLOT;
        if (!mh_io_read(&src, off, buf, bytes)) {
            out_msg(msg, msgn, "Read error while verifying the Disc.");
            goto out;
        }
        for (s = 0; s < chunk; s++) {
            const uint8_t *slot = buf + (size_t)s * MH_V2_SLOT;
            unsigned i;
            crc = mh_crc32_update(crc, slot, MH_V2_SECTOR);
            mh_sha1_update(&sha, slot, MH_V2_SECTOR);
            for (i = MH_V2_SECTOR; i < MH_V2_SLOT; i++)
                if (slot[i]) {
                    out_msg(msg, msgn, "The RAW padding of a sector is not zeroed.");
                    goto out;
                }
        }
        off += bytes;
        done += bytes;
        remaining -= chunk;
        if (w && w->prog) mh_v2_set_phase(w->prog, MH_V2_PHASE_VERIFYING_SOURCES, 0, 0);
        if (w && w->prog && !mh_progress_report(w->prog, done, total, "Verifying existing RAW Disc")) {
            out_msg(msg, msgn, "Verification cancelled.");
            goto out;
        }
        mh_progress_yield();
    }
    crc = mh_crc32_end(crc);
    mh_sha1_final(&sha, digest);
    if (d->crc32 == crc && memcmp(digest, d->sha1, 20) == 0) {
        out_msg(msg, msgn, "RAW Disc verified successfully.");
        ok = true;
    } else {
        out_msg(msg, msgn, "The Disc CRC32/SHA-1 differs from the values stored in the V2 library.");
    }
out:
    mh_free(buf);
    mh_io_close(&src);
    return ok;
}

/* orig: v2_admit_disc_without_full_check @0x140035110 */
bool mh_v2_admit_disc_without_full_check(MhDisc *d)
{
    unsigned k;
    if (!d) return false;
    d->verify_crc32 = 0;
    d->verified = 0;
    d->verified_aux = 0;
    d->verify_issues = 0;
    d->verify_accepted = 0;
    memset(d->verify_sha1, 0, sizeof d->verify_sha1);
    if (d->kind == MH_DISC_KIND_CUE && d->status != MH_DISC_STATUS_FIXABLE &&
        d->status != MH_DISC_STATUS_ERROR && d->ntracks - 1u < 99 && d->total_sectors != 0 &&
        (d->flags & MH_DF_STRUCT_OK) && d->nfiles != 0) {
        for (k = 0; d->files[k].path[0]; ) {
            uint64_t sz;
            if (!mh_file_size64(d->files[k].path, &sz) || sz != d->files[k].size) break;
            if (++k >= d->nfiles) {
                d->verified_aux = 1;
                mh_str_copy(d->verify_text, MH_V2W_MSG_MAX,
                            "Full verification is turned off. The write will still confirm the data on the card.");
                d->status = MH_DISC_STATUS_NOTE;
                return true;
            }
        }
        mh_str_copy(d->verify_text, MH_V2W_MSG_MAX, "A BIN file is missing or changed size.");
    } else {
        mh_str_copy(d->verify_text, MH_V2W_MSG_MAX, "Incomplete or invalid CUE/BIN.");
    }
    return false;
}

/* orig: manager_admit @0x140022770 */
bool mh_v2_manager_admit(const MhV2Writer *w, MhDisc *d, bool full,
                         bool (*ask)(void *ctx, const char *text), void *ctx)
{
    MhWbuf t;
    bool yes;
    if (!d) return false;
    if (!full) return mh_v2_admit_disc_without_full_check(d);
    if (!mh_v2_check_disc_source(w, d)) return false;
    if (d->verify_issues == 0) return true;
    mh_wbuf_init(&t);
    mh_wbuf_puts(&t, "Disc ");
    mh_wbuf_puts(&t, d->name[0] ? d->name : "unnamed");
    mh_wbuf_puts(&t, ":\r\n\r\n");
    mh_wbuf_puts(&t, d->verify_text);
    mh_wbuf_puts(&t, "\r\nAdd anyway?");
    yes = ask && ask(ctx, mh_wbuf_cstr(&t));
    mh_wbuf_free(&t);
    if (!yes) return false;
    d->verify_accepted = d->verify_issues;
    return true;
}

/* orig: auto_attach_cheats @0x140002910 */
void mh_v2_auto_attach_cheats(const MhV2Env *env, MhDisc *d)
{
    const MhCheatDb *db = env && env->cheats ? env->cheats : mh_cheat_db_active();
    MhCheatProfile *prof = NULL;
    uint32_t title = 0xffffffffu;
    char err[0x100];
    if (!d || d->cheats != NULL) return;
    err[0] = 0;
    if (!db || !db->loaded) {
        mh_str_copy(err, sizeof err, "The built-in cheat database failed verification.");
    } else if (d->serial[0] == 0) {
        mh_str_copy(err, sizeof err, "No serial identified, so cheats cannot be linked automatically.");
    } else if (!mh_cheat_db_find_title_for_disc(db, d->serial, &title)) {
        mh_str_copy(err, sizeof err,
                    "This serial has no automatic link in GameShark V5. Open Cheats and choose the game manually.");
    } else {
        if (mh_cheat_profile_build_exact_catalog(db, title, d->serial, &prof, err, sizeof err)) {
            d->cheats = prof;
            d->cheat_note[0] = 0;
            return;
        }
        mh_cheat_profile_free(prof);
    }
    mh_str_copy(d->cheat_note, 0x100, err);
}
