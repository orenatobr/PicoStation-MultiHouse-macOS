/* The disc object: lifetime, status, titles, serial scan hook, small V2 helpers. */
#include "mh/disc.h"

#include <stdlib.h>
#include <string.h>

/* orig: disc_status_text @0x1400183c0 */
const char *mh_disc_status_text(unsigned s)
{
    switch (s) {
    case 1: return "OK";
    case 2: return "Review";
    case 3: return "Fixable";
    case 4: return "Error";
    default: return "Empty";
    }
}

/* orig: disc_free @0x140018000 */
void mh_disc_free(MhDisc *d)
{
    if (!d) return;
    mh_cheat_profile_free(d->cheats);
    d->cheats = NULL;
    mh_free(d);
}

/* orig: disc_refresh_status @0x140018150 */
void mh_disc_refresh_status(MhDisc *d)
{
    uint32_t f;
    if (!d) return;
    f = d->flags;
    if (f & (MH_DF_MISSING_BIN | MH_DF_BAD_ALIGN | MH_DF_INVALID | MH_DF_INCOMPLETE)) {
        d->status = (f & MH_DF_MISSING_BIN) == 0 ? MH_DISC_STATUS_ERROR : MH_DISC_STATUS_FIXABLE;
        return;
    }
    if ((f & MH_DF_STRUCT_OK) == 0) {
        d->status = MH_DISC_STATUS_ERROR;
        return;
    }
    if (f & MH_DF_TRACKS_SUSPECT) {
        d->status = MH_DISC_STATUS_NOTE;
        return;
    }
    if (d->serial[0] == 0) {
        d->status = MH_DISC_STATUS_NOTE;
        if (d->notes[0] == 0) mh_str_copy(d->notes, sizeof d->notes, "Serial not identified automatically.");
        return;
    }
    if ((f & MH_DF_INTEGRITY_BAD) || d->total_sectors < d->ps.iso_sectors) {
        d->status = MH_DISC_STATUS_NOTE;
        return;
    }
    d->status = MH_DISC_STATUS_OK;
    if (d->notes[0] == 0) mh_str_copy(d->notes, sizeof d->notes, "Valid CUE/BIN structure.");
}

/* orig: disc_scan_serial @0x140018220 */
bool mh_disc_scan_serial(MhDisc *d)
{
    uint32_t st;
    if (!d || d->nfiles == 0) return false;
    memset(&d->ps, 0, sizeof d->ps);
    st = mh_psserial_extract(&d->ps, d->files[0].path);
    d->ps.status_saved = st;
    d->ps.source_saved = d->ps.source;
    mh_str_copy(d->ps.message_saved, sizeof d->ps.message_saved, d->ps.message);
    d->flags &= ~(uint32_t)MH_DF_SERIAL;
    d->serial[0] = 0;
    if (d->total_sectors < d->ps.iso_sectors) {
        MhWbuf w;
        mh_wbuf_init(&w);
        mh_wbuf_puts(&w, "ISO declares ");
        mh_wbuf_u32(&w, d->ps.iso_sectors);
        mh_wbuf_puts(&w, " sectors; CUE/BIN contains ");
        mh_wbuf_u32(&w, d->total_sectors);
        mh_wbuf_puts(&w, ". Missing ");
        mh_wbuf_u32(&w, d->ps.iso_sectors - d->total_sectors);
        mh_wbuf_puts(&w, " sectors compared with the declared volume. Full integrity not confirmed; BIN preserved.");
        mh_disc_note(d, mh_wbuf_cstr(&w));
        mh_wbuf_free(&w);
    }
    if (st == MH_PSS_STRONG) {
        mh_str_copy(d->serial, sizeof d->serial, d->ps.serial);
        d->flags |= MH_DF_SERIAL;
        return true;
    }
    return false;
}

/* orig: disc_progress.part.0 @0x1400180b0 */
bool mh_disc_progress(MhProgress *prog, uint64_t done, uint64_t total, const char *text, const MhDisc *d)
{
    MhWbuf w;
    char nm[0x80];
    bool r;
    if (!prog) return true;
    mh_utf8_from_latin1(d ? d->name : "", nm, sizeof nm);
    mh_wbuf_init(&w);
    mh_wbuf_puts(&w, text);
    mh_wbuf_puts(&w, nm);
    r = mh_progress_report(prog, done, total, mh_wbuf_cstr(&w));
    mh_wbuf_free(&w);
    return r;
}

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t rd64(const uint8_t *p) { return rd32(p) | (uint64_t)rd32(p + 4) << 32; }

/* orig: disc_layout_valid @0x140018040 */
bool mh_disc_layout_valid(const uint8_t *hdr)
{
    uint32_t v = rd32(hdr + 8);
    uint64_t a = rd64(hdr + 0x3c);
    if (v >= 4 || rd64(hdr + 0x64) != a + 5) return false;
    if ((v & 2) == 0) {
        if (rd32(hdr + 0x6c) > 0x1400) return false;
        if (rd64(hdr + 0x70) != a + 0xf) return false;
    } else {
        if (rd32(hdr + 0x6c) > 0x2800) return false;
        if (rd64(hdr + 0x70) != a + 0x19) return false;
    }
    return rd32(hdr + 0x78) == 0xa00;
}

/* orig: keep_disc_with_observations @0x14001d660 */
bool mh_keep_disc_with_observations(void *ctx, const char *text)
{
    (void)ctx;
    (void)text;
    return true;
}

/* orig: kept_package @0x14001d670 (control flow kept as in the original; labels are the
 * original's join points). */
uint64_t mh_kept_package(const MhDisc *d, int idx, const char *path)
{
    bool keep = (d->flags & MH_DF_KEEP_PACKAGE) != 0;
    bool path_nonempty = path && path[0];
    if (d->kind == MH_DISC_KIND_V2_DEVICE) {
        if (idx == -1) {
            if (!keep || !path_nonempty) return 0;
            goto path_check;
        }
        if (idx == (int)d->v2_drive) return d->v2_pkg_a;
        if (!keep) return 0;
        if (path) goto have_path;
    } else if (d->kind == MH_DISC_KIND_V2_IMAGE && path) {
        if (path[0] == 0) {
            if (!keep) return 0;
        } else {
            if (mh_path_equal_full(d->source_path, path)) return d->v2_pkg_a;
            if (!keep) return 0;
            goto have_path;
        }
    } else {
        if (!keep) return 0;
        if (path) goto have_path;
    }
    if (idx == -1) return 0;
    goto slot_check;
have_path:
    if (path[0] == 0) {
        if (idx == -1) return 0;
        goto slot_check;
    }
path_check:
    if (d->v2_pkg_path[0] == 0) return 0;
    if (!mh_path_equal_full(d->v2_pkg_path, path)) return 0;
    return d->v2_pkg_b;
slot_check:
    if (d->v2_pkg_path[0] != 0 || idx != d->v2_slot) return 0;
    return d->v2_pkg_b;
}

/* orig: case_title_for_disc @0x140003f00 */
bool mh_case_title_for_disc(const MhDisc *d, char *out, size_t n)
{
    char buf[0x60];
    int pos;
    size_t len;
    mh_str_copy(buf, sizeof buf, d->title[0] ? d->title : d->name);
    pos = mh_str_find_i(buf, " (DISC ");
    if (pos < 0) pos = mh_str_find_i(buf, " DISC ");
    if (pos >= 0) buf[pos] = 0;
    while ((len = strlen(buf)) != 0 && buf[len - 1] == ' ') buf[len - 1] = 0;
    /* a card name (small buffer) must fit the firmware; grouping compares the full text */
    if (n <= MH_CASE_TITLE_MAX) mh_fit_name(buf, d->title[0] ? d->title_base_len : 0, out, n < MH_NAME_FIT ? n : MH_NAME_FIT);
    else mh_str_copy(out, n, buf);
    return pos >= 0;
}

/* Port only, see disc.h. */
void mh_fit_name(const char *norm, size_t base_len, char *out, size_t n)
{
    const char *tag = norm + (base_len < strlen(norm) ? base_len : strlen(norm));
    size_t tag_len, keep;
    if (n == 0) return;
    while (*tag == ' ') tag++;
    tag_len = strlen(tag);
    if (strlen(norm) < n || base_len == 0 || tag_len == 0 || tag_len + 2 > n - 1) {
        mh_str_copy(out, n, norm);
        return;
    }
    keep = n - 2 - tag_len;
    if (keep > base_len) keep = base_len;
    memcpy(out, norm, keep);
    while (keep > 0 && out[keep - 1] == ' ') keep--;
    out[keep++] = ' ';
    memcpy(out + keep, tag, tag_len + 1);
}

/* orig: short_disc_name @0x140030f50 */
void mh_short_disc_name(const char *title, char *out, size_t n)
{
    size_t len = strlen(title), pos = 0, i = 0, u3;
    int m;
    if (n == 0) return;
    m = mh_str_find_i(title, " (DISC ");
    if (len <= n - 1 || m < 0) {
        mh_str_copy(out, n, title);
        return;
    }
    {
        size_t L = n - 2 - (len - (size_t)m);
        if ((len + 1) - (size_t)m < n - 1 && L != 0) {
            while (i < L && title[i]) {
                out[i] = title[i];
                i++;
            }
        }
    }
    u3 = i;
    while (u3 > 0 && out[u3 - 1] == ' ') u3--;
    if (u3 != 0) {
        pos = u3 + 1;
        if (pos < n) {
            out[u3] = ' ';
        } else {
            out[u3] = 0;
            return;
        }
    }
    {
        const char *t = title + m + 1;
        size_t k = pos;
        while (*t && k + 1 < n) out[k++] = *t++;
        out[k] = 0;
    }
}

/* orig: apply_trackset_title @0x140002310. The original converted UTF-8 to UTF-16 (max 0xa0
 * units) and ran normalize_ascii_title(.., 0x60). */
void mh_apply_trackset_title(MhDisc *d, const char *title)
{
    char norm[MH_DISC_TITLE_MAX];
    if (!title || !title[0]) return;
    mh_card_title(title, norm, sizeof norm);
    if (norm[0]) {
        mh_str_copy(d->title, sizeof d->title, norm);
        d->title_base_len = (uint16_t)mh_title_base_len(title);
        if ((d->flags & MH_DF_USER_NAMED) == 0) mh_fit_name(norm, d->title_base_len, d->name, MH_NAME_FIT);
    }
}

/* orig: recover_disc_serial_internal.isra.0 @0x14002bb40 */
MhRecoverStatus mh_disc_recover_serial(MhDisc *d, const char *device_path, MhProject *p, bool silent,
                                       char *msg, size_t msgn)
{
    MhPsSerial res;
    uint32_t st;
    const char *fail_text = NULL;
    if (msg && msgn) msg[0] = 0;
    if (!d || d->serial[0]) return MH_RECOVER_SKIPPED;
    if (d->kind == MH_DISC_KIND_CUE) {
        if (d->nfiles == 0 || d->files[0].path[0] == 0) {
            if (!silent) mh_str_copy(msg, msgn, "The original CUE/BIN is not available to identify the serial.");
            return MH_RECOVER_UNAVAILABLE;
        }
        st = mh_psserial_extract(&res, d->files[0].path);
    } else if (d->kind == MH_DISC_KIND_V2_DEVICE || d->kind == MH_DISC_KIND_V2_IMAGE) {
        const char *path;
        if (d->v2_raw_lba512 == 0 || d->total_sectors == 0) {
            if (!silent) mh_str_copy(msg, msgn, "The RAW record has no valid location to read from.");
            return MH_RECOVER_UNAVAILABLE;
        }
        path = d->kind == MH_DISC_KIND_V2_IMAGE ? d->source_path : device_path;
        if (!path || !path[0]) {
            if (!silent) mh_str_copy(msg, msgn, "The disc's RAW source is not available.");
            return MH_RECOVER_UNAVAILABLE;
        }
        st = mh_psserial_extract_raw(&res, path, d->v2_raw_lba512, d->total_sectors);
    } else {
        if (!silent) mh_str_copy(msg, msgn, "This source does not allow recovering the serial.");
        return MH_RECOVER_UNAVAILABLE;
    }
    if (st == MH_PSS_STRONG && res.serial[0]) {
        mh_str_copy(d->serial, sizeof d->serial, res.serial);
        d->ps = res;
        d->flags |= MH_DF_SERIAL;
        if (p) {
            p->dirty = 1;
            mh_project_recount(p);
        }
        if (silent) {
            mh_str_copy(msg, msgn, "Proven internal serial recovered. Apply the changes to the card.");
        } else {
            MhWbuf w;
            mh_wbuf_init(&w);
            mh_wbuf_puts(&w, "Internal serial recovered: ");
            mh_wbuf_puts(&w, d->serial);
            mh_wbuf_puts(&w, ". The game's RAW was preserved. Apply the changes to write the identity to the card.");
            mh_str_copy(msg, msgn, mh_wbuf_cstr(&w));
            mh_wbuf_free(&w);
        }
        return MH_RECOVER_OK;
    }
    fail_text = "No proven internal serial was found. No data was changed.\r\n\r\n";
    if (!silent) {
        MhWbuf w;
        mh_wbuf_init(&w);
        mh_wbuf_puts(&w, fail_text);
        mh_wbuf_puts(&w, res.message);
        mh_str_copy(msg, msgn, mh_wbuf_cstr(&w));
        mh_wbuf_free(&w);
    }
    return MH_RECOVER_NOT_FOUND;
}
