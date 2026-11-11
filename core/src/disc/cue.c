/* CUE sheet handling: parse, count, rewrite, write corrected. Original names: cue_parse,
 * cue_count_files, decode_cue, msf, cue_rewrite_files, cue_write_corrected. */
#include "mh/disc.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MH_CUE_MAX_BYTES (64u << 20)

static bool is_ws(int c) { return c == 9 || c == 10 || c == 13 || c == 32; } /* mask 0x100002600 */

/* orig: obs_add @0x1400251b0 and the open-coded "if (notes) cat CRLF; cat msg" pairs */
void mh_disc_note(MhDisc *d, const char *msg)
{
    if (!d) return;
    if (d->notes[0]) mh_str_cat(d->notes, sizeof d->notes, "\r\n");
    mh_str_cat(d->notes, sizeof d->notes, msg);
}

/* orig: msf @0x140024cf0 */
bool mh_cue_msf(const char *s, uint32_t *frames)
{
    uint32_t v[3];
    const unsigned char *p = (const unsigned char *)s;
    for (int k = 0; k < 3; k++) {
        uint32_t acc = 0;
        if (*p < '0' || *p > '9') return false;
        while (*p >= '0' && *p <= '9') {
            uint32_t d = *p - '0';
            if (~d / 10 < acc) return false; /* overflow test of the original */
            acc = d + acc * 10;
            p++;
        }
        v[k] = acc;
        if (k < 2) {
            if (*p != ':') return false;
            p++;
        }
    }
    if (v[1] >= 60 || v[2] >= 75) return false;
    *frames = (v[0] * 60 + v[1]) * 75 + v[2];
    return true;
}

/* Windows-1252 0x80..0x9F to Unicode (the other bytes map to themselves). */
static const uint16_t kCp1252[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
    0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};

/* strict UTF-8 validity (what MB_ERR_INVALID_CHARS enforces) */
static bool utf8_valid(const uint8_t *s, size_t n)
{
    size_t i = 0;
    while (i < n) {
        uint8_t c = s[i];
        if (c < 0x80) { i++; continue; }
        size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC2 ? 2 : 0;
        uint32_t cp;
        if (len == 0 || c > 0xF4 || i + len > n) return false;
        cp = c & (0xFF >> (len + 1));
        for (size_t k = 1; k < len; k++) {
            if ((s[i + k] & 0xC0) != 0x80) return false;
            cp = cp << 6 | (s[i + k] & 0x3F);
        }
        if ((len == 3 && cp < 0x800) || (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) ||
            (cp >= 0xD800 && cp <= 0xDFFF))
            return false;
        i += len;
    }
    return true;
}

/* orig: decode_cue.constprop.0 @0x140017a60 */
bool mh_cue_read_text(const char *path, char **text)
{
    uint8_t *data = NULL;
    uint64_t size = 0;
    char *out;
    *text = NULL;
    if (!mh_read_entire_file(path, &data, &size)) return false;
    /* MultiByteToWideChar fails for an empty buffer, so the original rejected an empty file;
     * the size cap is lower than the original 2 GiB (a CUE sheet is a few KB). */
    if (size == 0 || size >= MH_CUE_MAX_BYTES) { free(data); return false; }
    if (utf8_valid(data, (size_t)size)) {
        out = mh_alloc((size_t)size + 1);
        if (!out) { free(data); return false; }
        memcpy(out, data, (size_t)size);
        out[size] = 0;
    } else {
        size_t n = 0;
        out = mh_alloc((size_t)size * 3 + 1);
        if (!out) { free(data); return false; }
        for (uint64_t i = 0; i < size; i++) {
            uint32_t cp = data[i];
            if (cp >= 0x80 && cp < 0xA0) cp = kCp1252[cp - 0x80];
            if (cp < 0x80) out[n++] = (char)cp;
            else if (cp < 0x800) { out[n++] = (char)(0xC0 | cp >> 6); out[n++] = (char)(0x80 | (cp & 0x3F)); }
            else { out[n++] = (char)(0xE0 | cp >> 12); out[n++] = (char)(0x80 | (cp >> 6 & 0x3F)); out[n++] = (char)(0x80 | (cp & 0x3F)); }
        }
        out[n] = 0;
    }
    free(data);
    *text = out;
    return true;
}

/* Iterates the lines of a NUL terminated buffer the way the original did: a line ends at CR
 * or LF, the whole run of CR/LF that follows is skipped (blank lines vanish), the buffer is
 * modified in place. next = start of the following line or NULL at the end. */
static char *next_line(char *p, char **line)
{
    char *q = p;
    *line = p;
    if (*q == 0) return NULL; /* empty last line (cue_rewrite_files visits it) */
    while (*q != '\r' && *q != '\n') {
        q++;
        if (*q == 0) return NULL;
    }
    *q = 0;
    q++;
    while (*q == '\r' || *q == '\n') q++;
    return q;
}

/* skip leading blanks and cut trailing blanks; returns the content start */
static char *trim_line_in_place(char *p)
{
    char *end;
    while (*p && is_ws((unsigned char)*p)) p++;
    end = p + strlen(p);
    while (end > p && is_ws((unsigned char)end[-1])) end--;
    *end = 0;
    return p;
}

/* orig: cue_count_files @0x140012fe0 */
int mh_cue_count_files(const char *path)
{
    char *text, *p, *line, *next;
    int n = 0;
    if (!mh_cue_read_text(path, &text)) return 0;
    p = text;
    while (*p) {
        next = next_line(p, &line);
        if (mh_str_starts_i(trim_line_in_place(line), "FILE ")) n++;
        if (!next) break;
        p = next;
    }
    free(text);
    return n;
}

typedef struct TmpTrack { /* orig: 0x18 byte entries on cue_parse's stack */
    uint16_t file;
    uint8_t number, kind, mode, has0, has1, has_pregap, postgap, dcp, isrc;
    uint32_t idx0, idx1, pregap;
} TmpTrack;

static void skip_ws(const char **p)
{
    while (**p && is_ws((unsigned char)**p)) (*p)++;
}

/* parses a decimal like the original (leading digits, overflow aborts) */
static bool parse_uint(const char **p, uint32_t *out)
{
    uint32_t acc = 0;
    const char *s = *p;
    if (*s < '0' || *s > '9') return false;
    while (*s >= '0' && *s <= '9') {
        uint32_t d = (uint32_t)(*s - '0');
        if (~d / 10 < acc) return false;
        acc = d + acc * 10;
        s++;
    }
    *p = s;
    *out = acc;
    return true;
}

/* first directory entry (not a directory) whose name equals `base` ignoring ASCII case */
static bool find_icase(const char *dir, const char *base, char *out, size_t n)
{
    DIR *d = opendir(dir[0] ? dir : ".");
    struct dirent *e;
    bool found = false;
    if (!d) return false;
    while ((e = readdir(d)) != NULL) {
        char full[MH_PATH_MAX];
        struct stat st;
        if (mh_str_icmp(e->d_name, base) != 0) continue;
        mh_path_join(dir, e->d_name, full, sizeof full);
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) continue;
        mh_str_copy(out, n, full);
        found = true;
        break;
    }
    closedir(d);
    return found;
}

/* orig: cue_parse @0x140013160 */
MhDisc *mh_cue_parse(const char *cue_path)
{
    MhDisc *d = mh_calloc(sizeof *d);
    char *text = NULL, *p, *line, *next, stem[MH_PATH_MAX], cue_dir[MH_PATH_MAX];
    TmpTrack tt[MH_DISC_MAX_TRACKS];
    unsigned nfiles = 0, ntracks = 0;
    int cur_file = -1, cur_track = -1;

    if (!d) return NULL;
    d->kind = MH_DISC_KIND_CUE;
    d->status = MH_DISC_STATUS_ERROR;
    d->db_index = 0xFFFFFFFFu;
    mh_str_copy(d->cue_path, sizeof d->cue_path, cue_path);
    mh_path_stem(cue_path, stem, sizeof stem);
    mh_card_title(stem, d->name, sizeof d->name);
    if (!mh_cue_read_text(cue_path, &text)) {
        mh_disc_note(d, "Could not open or decode the CUE.");
        return d;
    }
    mh_path_dirname(cue_path, cue_dir, sizeof cue_dir);
    memset(tt, 0, sizeof tt);

    p = text;
    while (*p) {
        char *c;
        next = next_line(p, &line);
        c = trim_line_in_place(line);
        if (*c && !mh_str_starts_i(c, "REM ")) {
            if (mh_str_starts_i(c, "FILE ")) {
                char name[MH_PATH_MAX], type[32], joined[MH_PATH_MAX], conv[MH_PATH_MAX];
                size_t n = 0;
                const char *q = c + 4;
                if (nfiles > 98) {
                    mh_disc_note(d, "More than 99 BIN files in the CUE.");
                    d->flags |= MH_DF_INVALID;
                    break;
                }
                name[0] = 0;
                type[0] = 0;
                skip_ws(&q);
                if (*q == '"') {
                    q++;
                    if (*q != '"' && *q != 0) {
                        while (*q && *q != '"' && n < MH_PATH_MAX - 1) name[n++] = *q++;
                    }
                    name[n] = 0;
                    if (*q == '"') q++;
                } else if (*q) {
                    while (*q && !is_ws((unsigned char)*q) && n < MH_PATH_MAX - 1) name[n++] = *q++;
                    name[n] = 0;
                }
                skip_ws(&q);
                mh_str_copy(type, sizeof type, q);
                if (name[0] == 0) {
                    mh_disc_note(d, "Invalid FILE line.");
                    break;
                }
                cur_file = (int)nfiles;
                {
                    MhDiscFile *f = &d->files[nfiles];
                    bool is_abs;
                    mh_str_copy(f->name, sizeof f->name, name);
                    is_abs = name[0] == '/' || (name[0] && (name[1] == ':' || (name[1] == '\\' && name[0] == '\\')));
                    mh_str_copy(conv, sizeof conv, name);
                    for (char *s = conv; *s; s++)
                        if (*s == '\\') *s = '/';
                    if (is_abs) mh_str_copy(joined, sizeof joined, conv);
                    else mh_path_join(cue_dir, conv, joined, sizeof joined);
                    if (mh_file_exists(joined)) {
                        mh_str_copy(f->path, sizeof f->path, joined);
                    } else {
                        char base[MH_PATH_MAX], found[MH_PATH_MAX];
                        mh_path_basename(name, base, sizeof base);
                        if (find_icase(cue_dir, base, found, sizeof found))
                            mh_str_copy(f->path, sizeof f->path, found);
                        else
                            mh_str_copy(f->path, sizeof f->path, joined);
                    }
                }
                if (mh_str_icmp(type, "BINARY") != 0) {
                    d->flags |= MH_DF_INVALID;
                    mh_disc_note(d, "Only BINARY files are supported in this version.");
                }
                nfiles++;
                cur_track = -1;
            } else if (mh_str_starts_i(c, "TRACK ")) {
                const char *q = c + 6;
                uint32_t num = 0;
                char type[32];
                size_t n = 0;
                TmpTrack *t;
                skip_ws(&q);
                if (!parse_uint(&q, &num) || num < 1 || num > 99 || cur_file < 0 || ntracks >= 99) {
                    mh_disc_note(d, "Invalid or out-of-order TRACK line.");
                    break;
                }
                skip_ws(&q);
                while (*q && !is_ws((unsigned char)*q) && n < 31) type[n++] = *q++;
                type[n] = 0;
                t = &tt[ntracks];
                if (mh_str_icmp(type, "AUDIO") == 0) { t->mode = 0; t->kind = 1; }
                else if (mh_str_icmp(type, "MODE1/2352") == 0) { t->mode = 1; t->kind = 2; }
                else if (mh_str_icmp(type, "MODE2/2352") == 0) { t->mode = 2; t->kind = 2; }
                else {
                    d->flags |= MH_DF_INVALID;
                    mh_disc_note(d, "Unsupported track mode. Use AUDIO, MODE1/2352 or MODE2/2352.");
                    t->mode = 0;
                    t->kind = 0;
                }
                t->file = (uint16_t)cur_file;
                t->number = (uint8_t)num;
                cur_track = (int)ntracks;
                ntracks++;
            } else if (mh_str_starts_i(c, "INDEX ")) {
                const char *q = c + 6;
                uint32_t idx = 0, fr = 0;
                skip_ws(&q);
                if (cur_track == -1 || *q < '0' || *q > '9' || !parse_uint(&q, &idx)) {
                    mh_disc_note(d, "INDEX without a valid TRACK.");
                    break;
                }
                skip_ws(&q);
                if (!mh_cue_msf(q, &fr)) {
                    mh_disc_note(d, "Invalid INDEX time.");
                    break;
                }
                if (idx == 0) { tt[cur_track].has0 = 1; tt[cur_track].idx0 = fr; }
                else if (idx == 1) { tt[cur_track].has1 = 1; tt[cur_track].idx1 = fr; }
            } else if (mh_str_starts_i(c, "PREGAP ")) {
                const char *q = c + 6;
                uint32_t fr = 0;
                skip_ws(&q);
                if (cur_track != -1 && mh_cue_msf(q, &fr)) {
                    tt[cur_track].has_pregap = 1;
                    tt[cur_track].pregap = fr;
                } else {
                    mh_disc_note(d, "Invalid PREGAP.");
                    break;
                }
            } else if (mh_str_starts_i(c, "FLAGS ")) {
                const char *q = c + 5;
                skip_ws(&q);
                if (cur_track == -1) {
                    mh_disc_note(d, "FLAGS without a valid TRACK.");
                    break;
                }
                if (mh_str_starts_i(q, "DCP") || mh_str_starts_i(q, "PRE DCP") || mh_str_starts_i(q, "4CH DCP"))
                    tt[cur_track].dcp = 1;
            } else if (mh_str_starts_i(c, "ISRC ")) {
                if (cur_track == -1) {
                    mh_disc_note(d, "ISRC without a valid TRACK.");
                    break;
                }
                tt[cur_track].isrc = 1;
            } else if (mh_str_starts_i(c, "POSTGAP ")) {
                if (cur_track != -1) tt[cur_track].postgap = 1;
                d->flags |= MH_DF_INCOMPLETE;
                mh_disc_note(d, "A virtual POSTGAP cannot be accepted safely.");
            }
        }
        if (!next) break;
        p = next;
    }
    free(text);
    d->nfiles = (uint16_t)nfiles;
    d->ntracks = (uint16_t)ntracks;

    if (nfiles != 0 && ntracks != 0) {
        uint32_t used[MH_DISC_MAX_FILES];
        uint32_t total = 0;
        memset(used, 0, sizeof used);
        for (unsigned i = 0; i < ntracks; i++)
            if (tt[i].file < nfiles) used[tt[i].file] = 1;
        for (unsigned f = 0; f < nfiles; f++) {
            if (!used[f]) {
                d->flags |= MH_DF_INVALID;
                mh_disc_note(d, "There is a FILE with no TRACK.");
            }
            for (unsigned g = 0; g < f; g++)
                if (mh_str_icmp(d->files[f].path, d->files[g].path) == 0) {
                    d->flags |= MH_DF_INVALID;
                    mh_disc_note(d, "The same BIN is declared more than once, so the layout is ambiguous.");
                }
        }
        if (tt[0].kind != 2) {
            d->flags |= MH_DF_INVALID;
            mh_disc_note(d, "The first track of a PlayStation disc must be a data track.");
        }
        for (unsigned f = 0; f < nfiles; f++) {
            MhDiscFile *fl = &d->files[f];
            uint64_t sz;
            if (!mh_file_size64(fl->path, &sz)) {
                d->flags |= MH_DF_MISSING_BIN;
                mh_disc_note(d, "One or more referenced BINs were not found.");
                continue;
            }
            fl->size = sz;
            if (sz != 0 && sz % MH_SECTOR_SIZE == 0) {
                uint64_t sec = sz / MH_SECTOR_SIZE;
                if (sec < 0x6ddd1 && total <= (uint32_t)(MH_DISC_MAX_SECTORS - (uint32_t)sec)) {
                    fl->sectors = (uint32_t)sec;
                    fl->start = total;
                    total += (uint32_t)sec;
                } else {
                    d->flags |= MH_DF_INVALID;
                    mh_disc_note(d, "The image exceeds the safe limit of 450,000 CD sectors.");
                }
            } else {
                d->flags |= MH_DF_BAD_ALIGN;
                mh_disc_note(d, "A BIN is not aligned to 2352-byte sectors.");
            }
        }
        d->total_sectors = total;
        d->total_bytes = (uint64_t)total * MH_SECTOR_SIZE;

        if ((d->flags & (MH_DF_MISSING_BIN | MH_DF_BAD_ALIGN | MH_DF_INVALID)) == 0) {
            uint32_t pregap[MH_DISC_MAX_TRACKS], start[MH_DISC_MAX_TRACKS];
            bool ok = true;
            for (unsigned i = 0; i < ntracks && ok; i++) {
                const TmpTrack *t = &tt[i];
                uint32_t pg;
                if (t->number != i + 1) {
                    mh_disc_note(d, "Tracks must be consecutive starting at 01.");
                    ok = false; break;
                }
                if (!t->has1 || d->files[t->file].sectors <= t->idx1) {
                    mh_disc_note(d, "INDEX 01 is missing or outside the BIN.");
                    ok = false; break;
                }
                if (!t->has0) {
                    pg = 0;
                    if (t->has_pregap && t->pregap != 0) {
                        d->flags |= MH_DF_INCOMPLETE;
                        mh_disc_note(d, "Virtual PREGAP without INDEX 00: the image may be incomplete.");
                        ok = false; break;
                    }
                } else {
                    if (t->idx1 < t->idx0) {
                        mh_disc_note(d, "Invalid INDEX 00.");
                        ok = false; break;
                    }
                    pg = t->idx1 - t->idx0;
                    if (t->has_pregap && t->pregap != pg && t->pregap != 0) {
                        mh_disc_note(d, "PREGAP and INDEX 00/01 give different values.");
                        ok = false; break;
                    }
                }
                pregap[i] = pg;
                start[i] = t->idx1 + d->files[t->file].start;
            }
            if (ok) {
                if (!(start[0] == 0 && pregap[0] == 0)) {
                    mh_disc_note(d, "The first track must start at sector 0 with no pregap.");
                    ok = false;
                }
            }
            if (ok) {
                uint32_t prev = 0;
                for (unsigned i = 1; i < ntracks; i++) {
                    uint32_t s = start[i];
                    if (s <= prev || s - pregap[i] < prev) {
                        mh_disc_note(d, "Track INDEX values are out of order or a pregap overlaps.");
                        ok = false; break;
                    }
                    prev = s;
                }
            }
            if (ok) {
                for (unsigned i = 0; i < ntracks; i++) {
                    const TmpTrack *t = &tt[i];
                    MhTrack *o = &d->tracks[i];
                    uint32_t next_start = (i + 1 < ntracks) ? start[i + 1] : d->total_sectors;
                    o->number = t->number;
                    o->kind = t->kind;
                    o->mode = t->mode;
                    o->flags = (uint8_t)((t->dcp ? MH_TRACK_DCP : 0) | (t->isrc ? MH_TRACK_ISRC : 0) |
                                         (t->has0 ? MH_TRACK_HAS_INDEX0 : 0));
                    o->file = t->file;
                    o->pregap = pregap[i];
                    o->index0 = t->has0 ? t->idx0 : t->idx1;
                    o->index1 = t->idx1;
                    o->start = start[i];
                    o->length = next_start - start[i];
                }
                d->flags |= MH_DF_STRUCT_OK;
            }
            mh_disc_scan_serial(d);
        }
    } else if (d->notes[0] == 0) {
        mh_disc_note(d, "CUE has no valid FILE/TRACK.");
    }
    mh_disc_refresh_status(d);
    return d;
}

/* orig: cue_rewrite_files @0x140014820 */
bool mh_cue_rewrite_files(const char *src, const char *dst, const char *const *paths, unsigned nfiles)
{
    char *text, *p, *line, *next;
    MhWbuf w;
    unsigned counter = 0;
    bool ok;
    if (!mh_cue_read_text(src, &text)) return false;
    mh_wbuf_init(&w);
    p = text;
    for (;;) { /* like the original: a text ending in CR/LF gets one more (empty) line, written as CRLF */
        char *content;
        bool is_file;
        next = next_line(p, &line);
        content = trim_line_in_place(line);
        is_file = mh_str_starts_i(content, "FILE ");
        if (counter < nfiles && is_file && paths && paths[counter] && paths[counter][0]) {
            char d1[MH_PATH_MAX], d2[MH_PATH_MAX], base[MH_PATH_MAX];
            const char *use = paths[counter];
            mh_path_dirname(paths[counter], d1, sizeof d1);
            mh_path_dirname(dst, d2, sizeof d2);
            if (mh_path_equal_full(d1, d2)) {
                mh_path_basename(paths[counter], base, sizeof base);
                use = base;
            }
            counter++;
            mh_wbuf_puts(&w, "FILE \"");
            mh_wbuf_puts(&w, use);
            mh_wbuf_puts(&w, "\" BINARY");
            mh_wbuf_puts(&w, "\r\n");
        } else {
            mh_wbuf_puts(&w, line);
            mh_wbuf_puts(&w, "\r\n");
            if (is_file) counter++;
        }
        if (!next) break;
        p = next;
    }
    free(text);
    ok = mh_write_entire_file(dst, mh_wbuf_cstr(&w), w.len);
    mh_wbuf_free(&w);
    return ok;
}

static void put_msf(MhWbuf *w, uint32_t fr)
{
    uint32_t m = fr / 4500, s = (fr / 75) % 60, f = fr % 75;
    if (fr < 45000) mh_wbuf_putc(w, '0');
    mh_wbuf_u32(w, m);
    mh_wbuf_putc(w, ':');
    if (s < 10) mh_wbuf_putc(w, '0');
    mh_wbuf_u32(w, s);
    mh_wbuf_putc(w, ':');
    if (f < 10) mh_wbuf_putc(w, '0');
    mh_wbuf_u32(w, f);
}

/* orig: cue_write_corrected @0x140014ce0 */
bool mh_cue_write_corrected(const MhDisc *d, const char *dst)
{
    MhWbuf w;
    bool ok;
    mh_wbuf_init(&w);
    for (unsigned f = 0; f < d->nfiles; f++) {
        char base[MH_PATH_MAX];
        mh_path_basename(d->files[f].path, base, sizeof base);
        mh_wbuf_puts(&w, "FILE \"");
        mh_wbuf_puts(&w, base);
        mh_wbuf_puts(&w, "\" BINARY\r\n");
        for (unsigned t = 0; t < d->ntracks; t++) {
            const MhTrack *tr = &d->tracks[t];
            if (tr->file != f) continue;
            mh_wbuf_puts(&w, "  TRACK ");
            if (tr->number < 10) mh_wbuf_putc(&w, '0');
            mh_wbuf_u32(&w, tr->number);
            mh_wbuf_putc(&w, ' ');
            mh_wbuf_puts(&w, tr->kind == 1 ? "AUDIO" : tr->mode == 1 ? "MODE1/2352" : "MODE2/2352");
            mh_wbuf_puts(&w, "\r\n");
            if (tr->pregap != 0) {
                mh_wbuf_puts(&w, "    INDEX 00 ");
                put_msf(&w, tr->index0);
                mh_wbuf_puts(&w, "\r\n");
            }
            mh_wbuf_puts(&w, "    INDEX 01 ");
            put_msf(&w, tr->index1);
            mh_wbuf_puts(&w, "\r\n");
        }
    }
    ok = mh_write_entire_file(dst, mh_wbuf_cstr(&w), w.len);
    mh_wbuf_free(&w);
    return ok;
}
