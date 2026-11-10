/* PS1 serial recovery from a CUE/BIN (or a raw V2 slot): ISO 9660 walk, SYSTEM.CNF BOOT line,
 * executable names and executable content. Original names: psserial_*, extractSerial*,
 * extract_mapped.part.0.constprop.0, canonical_serial. */
#include "mh/disc.h"

#include <stdlib.h>
#include <string.h>

/* ---- texts -------------------------------------------------------------------------- */

/* orig: psserial_status_text @0x14002a7c0 */
const char *mh_psserial_status_text(unsigned s)
{
    switch (s) {
    case 1: return "FORTE_INTERNO";
    case 2: return "PROVAVEL";
    case 3: return "AMBIGUO";
    case 4: return "ERRO_IO";
    case 5: return "IMAGEM_INVALIDA";
    case 6: return "CONFIRMADO_HASH_TRACKSET";
    default: return "NAO_LOCALIZADO";
    }
}

/* orig: psserial_source_text @0x14002a770 */
const char *mh_psserial_source_text(unsigned s)
{
    switch (s) {
    case 1: return "SYSTEM_CNF";
    case 2: return "NOME_EXECUTAVEL";
    case 3: return "CONTEUDO_EXECUTAVEL";
    case 4: return "HASH_TRACKSET";
    default: return "NENHUMA";
    }
}

/* ---- small character helpers (ASCII only, like the original) ------------------------- */

static int up(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
static bool is_alpha(int c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
static bool is_digit(int c) { return c >= '0' && c <= '9'; }
/* characters that glue to a serial token: letters, digits, '-', '.', '_' (mask 0x4000000001ffb) */
static bool is_glue(int c) { return is_alpha(c) || is_digit(c) || c == '-' || c == '.' || c == '_'; }
static bool is_ws(int c) { return (c >= 9 && c <= 13) || c == ' '; }

/* ---- canonical_serial ---------------------------------------------------------------- */

/* orig: canonical_serial.constprop.0 @0x1400030c0 (follows the machine code: the decompiler
 * lost the success paths). */
bool mh_canonical_serial(const char *in, char out[16])
{
    size_t n = 0, i;
    int c = (unsigned char)in[0];
    if (c == '-' || c == 0) return false;
    /* prefix: letters/digits up to '-' or end, upper-cased, at most 15 */
    for (;;) {
        if (is_alpha(c)) c = up(c);
        else if (!is_digit(c)) return false;
        if (n + 1 == 0x10) return false;
        out[n++] = (char)c;
        c = (unsigned char)in[n];
        if (c == 0 || c == '-') break;
    }
    if (n < 3 || n > 4 || c != '-') return false;
    out[n] = '-';
    i = n + 1; /* first digit */
    for (size_t k = 0; k < 5; k++) {
        if (!is_digit((unsigned char)in[i + k])) return false;
        out[i + k] = in[i + k];
    }
    i += 5; /* in[i] is after the 5th digit */
    c = (unsigned char)in[i];
    if (is_digit(c)) { /* sixth digit */
        out[i] = (char)c;
        i++;
        c = (unsigned char)in[i];
        if (is_digit(c)) return false; /* 7 or more digits: always rejected */
    }
    if (is_alpha(c)) { /* optional trailing letter */
        out[i] = (char)up(c);
        i++;
    }
    out[i] = 0;
    c = (unsigned char)in[i];
    if (c == 0) return true;
    if (c != '-') return false;
    /* optional repeat of the prefix: "-AAA" or "-AAAA", then end or dashes */
    {
        int a = (unsigned char)in[i + 1];
        if (a == 0 || a == '-' || up(a) != (unsigned char)out[0]) return false;
        a = (unsigned char)in[i + 2];
        if (a == '-' || a == 0 || up(a) != (unsigned char)out[1]) return false;
        a = (unsigned char)in[i + 3];
        if (a == 0 || a == '-' || up(a) != (unsigned char)out[2]) return false;
        size_t j = i + 4;
        a = (unsigned char)in[j];
        if (a != 0 && a != '-') {
            if (n < 4 || up(a) != (unsigned char)out[3]) return false;
            j = i + 5;
            a = (unsigned char)in[j];
            if (a != 0 && a != '-') return false;
        }
        if (a == '-') {
            const char *s = in + j + 1;
            for (; *s == '-'; s++) {}
            return *s == 0;
        }
        return true;
    }
}

/* ---- serial token inside text -------------------------------------------------------- */

/* The 24 four-letter prefixes (orig: psserial_prefixes @0x140f1d0e0, 24 pointers). */
static const char *const kPrefixes[24] = {
    "ESPM", "PAPX", "PBPX", "PCPD", "PCPX", "PDPX", "PEPX", "PTPX", "PUPX", "SCED", "SCES", "SCKA",
    "SCPM", "SCPS", "SCUS", "SCZS", "SIPS", "SLED", "SLES", "SLKA", "SLPM", "SLPS", "SLUS", "SPUS"};

/* orig: psserial_parse_token.constprop.0 @0x140029e40. s has len bytes (not NUL terminated
 * necessarily). Token = PREFIX '_' ddd '.' dd (11 chars) or PREFIX '-' ddddd (10 chars). */
static bool parse_token(const uint8_t *s, size_t len, char raw[32], char canon[16], size_t *consumed)
{
    int ch[4];
    size_t k, size;
    if (!s || len < 10) return false;
    for (k = 0; k < 4; k++) {
        if (!is_alpha(s[k])) return false;
        ch[k] = up(s[k]);
    }
    for (k = 0; k < 24; k++)
        if (ch[0] == kPrefixes[k][0] && ch[1] == kPrefixes[k][1] && ch[2] == kPrefixes[k][2] &&
            ch[3] == kPrefixes[k][3])
            break;
    if (k == 24) return false;
    if (s[4] == '_') {
        if (len == 10) return false;
        if (!is_digit(s[5]) || !is_digit(s[6]) || !is_digit(s[7]) || s[8] != '.' || !is_digit(s[9]) ||
            !is_digit(s[10]))
            return false;
        size = 11;
    } else if (s[4] == '-') {
        for (k = 5; k < 10; k++)
            if (!is_digit(s[k])) return false;
        size = 10;
    } else {
        return false;
    }
    memcpy(raw, s, size);
    raw[size] = 0;
    canon[0] = (char)ch[0];
    canon[1] = (char)ch[1];
    canon[2] = (char)ch[2];
    canon[3] = (char)ch[3];
    canon[4] = '-';
    canon[5] = (char)s[5];
    canon[6] = (char)s[6];
    canon[7] = (char)s[7];
    canon[8] = (char)(s[4] == '_' ? s[9] : s[8]);
    canon[9] = (char)(s[4] == '_' ? s[10] : s[9]);
    canon[10] = 0;
    *consumed = size;
    return true;
}

/* orig: psserial_parse_filename.constprop.0 @0x140029cd0 */
bool mh_psserial_parse_filename(const char *name, char raw[32], char canonical[16])
{
    char buf[256];
    size_t len, consumed = 0;
    mh_str_copy(buf, sizeof buf, name ? name : "");
    for (char *p = buf; *p; p++)
        if (*p == ';') { *p = 0; break; }
    len = strlen(buf);
    if (len > 4 && mh_str_icmp(buf + len - 4, ".EXE") == 0) {
        buf[len - 4] = 0;
        len -= 4;
    }
    if (!parse_token((const uint8_t *)buf, len, raw, canonical, &consumed)) return false;
    return consumed == len;
}

/* ---- candidate list ------------------------------------------------------------------ */

typedef struct Cands { /* orig: 0x230c bytes */
    uint32_t count;     /* distinct candidates, may exceed 16 */
    uint32_t seen;      /* tokens offered */
    uint32_t overflow;  /* more than 16 distinct */
    char serial[16][16];
    char raw[16][32];
    char path[16][512];
} Cands;

/* orig: psserial_add_candidate @0x1400292f0 */
static void cand_add(Cands *c, const char *canon, const char *raw, const char *path)
{
    uint32_t i, n = c->count;
    c->seen++;
    for (i = 0; i < n && i < 16; i++)
        if (mh_str_icmp(c->serial[i], canon) == 0) return;
    if (n > 15) {
        c->overflow = 1;
        c->count = n + 1;
        return;
    }
    mh_str_copy(c->serial[n], 16, canon);
    mh_str_copy(c->raw[n], 32, raw);
    mh_str_copy(c->path[n], 512, path);
    c->count = n + 1;
}

/* orig: psserial_finish_from_candidates.isra.0 @0x140029b80 */
static void cand_finish(MhPsSerial *r, const Cands *c, uint32_t source, const char *unique_msg,
                        const char *multi_msg)
{
    uint32_t i, n = c->count;
    r->cand_count = n;
    if (n == 0) {
        if (c->overflow == 0) {
            r->status = 0;
            r->source = 0;
            r->needs_hash = 1;
            return;
        }
    } else {
        for (i = 0; i < n && i < 16; i++) mh_str_copy(r->cand[i], 16, c->serial[i]);
        if (c->overflow == 0 && n < 2) {
            if (n != 1) { /* unreachable: n == 1 here */
                r->status = 0;
                r->source = 0;
                r->needs_hash = 1;
                return;
            }
            r->source = source;
            r->status = MH_PSS_PROBABLE;
            r->needs_hash = 1;
            mh_str_copy(r->serial, 16, c->serial[0]);
            mh_str_copy(r->token, 32, c->raw[0]);
            mh_str_copy(r->path, 512, c->path[0]);
            mh_str_copy(r->message, 192, unique_msg);
            return;
        }
    }
    r->needs_hash = 1;
    r->status = MH_PSS_AMBIGUOUS;
    r->source = 0;
    mh_str_copy(r->message, 192, multi_msg);
}

/* ---- image access -------------------------------------------------------------------- */

typedef struct PsFile { /* orig: 0x28 byte block from p_calloc */
    MhIo io;
    uint64_t size;   /* bytes (BIN) or sectors * 2352 (raw slot) */
    uint64_t base;   /* raw slot: byte offset of the first slot */
    uint64_t pos;    /* position in the virtual 2352-stride stream */
    int raw;         /* 1: slots of 2560 bytes (0xa00), only 2352 meaningful */
} PsFile;

typedef struct PsIso { /* orig: the 0x20 byte block on extract_mapped's stack */
    PsFile *f;
    uint32_t stride;  /* 0x930 or 0x800 */
    uint32_t udo;     /* offset of the 2048 user bytes inside a sector: 0x10, 0x18 or 0 */
    uint32_t volume;  /* PVD volume space size in sectors */
    uint32_t root_lba, root_len;
    /* walk budget (deviation: the original only limited the depth) */
    uint32_t visited[2048], nvisited, records, reads;
} PsIso;

#define WALK_MAX_RECORDS 200000
#define WALK_MAX_READS 50000

typedef struct PsEntry { /* orig: 0x30c bytes handed to the visitors */
    uint32_t lba, size, is_dir;
    char name[256];
    char path[512];
} PsEntry;

/* orig: psserial_file_read.constprop.0 @0x1400297b0: 0x800 bytes at f->pos. */
static size_t ps_read(PsFile *f, uint8_t *buf)
{
    if (!f->raw) return mh_io_read(&f->io, f->pos, buf, 0x800) ? 0x800 : 0;
    {
        uint64_t pos = f->pos;
        size_t done = 0;
        uint8_t slot[0xa00];
        while (pos < f->size) {
            uint64_t sector = pos / 0x930;
            uint32_t off = (uint32_t)(pos % 0x930);
            size_t take = 0x930 - off;
            if (take > 0x800 - done) take = 0x800 - done;
            if (!mh_io_read(&f->io, sector * 0xa00 + f->base, slot, 0xa00)) return done;
            memcpy(buf + done, slot + off, take);
            done += take;
            pos += take;
            f->pos = pos;
            if (done > 0x7ff) return done;
        }
        return done;
    }
}

/* orig: psserial_read_user_sector @0x14002a3c0 */
static bool ps_read_sector(PsIso *iso, uint32_t sector, uint8_t *buf)
{
    PsFile *f = iso->f;
    uint64_t pos;
    if (sector >= iso->volume || !f || iso->stride < 0x800) return false;
    pos = (uint64_t)sector * iso->stride + iso->udo;
    if (pos > f->size || f->size - pos < 0x800) return false;
    f->pos = pos;
    return ps_read(f, buf) == 0x800;
}

/* orig: psserial_read_iso_bytes.constprop.0 @0x14002a240 */
static bool ps_read_bytes(PsIso *iso, const PsEntry *e, uint8_t *dst, uint64_t n)
{
    uint64_t done = 0, blocks;
    uint8_t tmp[0x800];
    if (n > e->size || e->lba >= iso->volume) return false;
    blocks = ((uint64_t)e->size + 0x7ff) >> 11;
    if (blocks > iso->volume - e->lba) return false;
    if ((blocks + e->lba) * iso->stride > iso->f->size) return false;
    while (done < n) {
        uint32_t off = (uint32_t)(done & 0x7ff);
        uint64_t take = 0x800 - off;
        if (n - done < take) take = n - done;
        if (!ps_read_sector(iso, e->lba + (uint32_t)(done >> 11), tmp)) return false;
        memcpy(dst + done, tmp + off, take);
        done += take;
    }
    return true;
}

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t be32(const uint8_t *p) { return p[3] | p[2] << 8 | p[1] << 16 | (uint32_t)p[0] << 24; }

typedef bool (*Visitor)(PsIso *iso, const PsEntry *e, void *ctx);

/* orig: psserial_walk_directory @0x14002a840 (recursion limit 0x11) */
static bool ps_walk_rec(PsIso *iso, uint32_t lba, uint32_t len, const char *prefix, int depth, Visitor v,
                        void *ctx)
{
    uint8_t blk[0x800];
    uint32_t off;
    if (depth == 0x11) return false;
    for (off = 0; off < len; off += 0x800) {
        uint32_t chunk = len - off;
        uint32_t pos = 0;
        if (chunk > 0x800) chunk = 0x800;
        if (++iso->reads > WALK_MAX_READS) return false;
        if (!ps_read_sector(iso, (off >> 11) + lba, blk)) return false;
        while (pos < chunk) {
            const uint8_t *r = blk + pos;
            uint32_t rl = r[0], ext, sz;
            PsEntry e;
            char nm[256];
            size_t nl;
            if (rl == 0) break;
            if (++iso->records > WALK_MAX_RECORDS) return false;
            if (rl < 0x22) return false;
            if (pos + rl > chunk || rl < (uint32_t)r[0x20] + 0x21) return false;
            pos += rl;
            if (r[0x20] == 1 && r[0x21] <= 1) continue; /* "." and ".." */
            memset(&e, 0, sizeof e);
            nl = r[0x20];
            memcpy(nm, r + 0x21, nl);
            nm[nl] = 0;
            { /* strip ";version" when only digits follow the last ';' */
                char *last = NULL, *q;
                for (q = nm; *q; q++)
                    if (*q == ';') last = q;
                if (last && last[1]) {
                    bool digits = true;
                    for (q = last + 1; *q; q++)
                        if (!is_digit((unsigned char)*q)) { digits = false; break; }
                    if (digits) *last = 0;
                }
            }
            ext = le32(r + 2);
            sz = le32(r + 10);
            e.lba = ext;
            e.size = sz;
            e.is_dir = (r[0x19] >> 1) & 1;
            if (be32(r + 6) != ext || be32(r + 14) != sz) return false;
            if (iso->volume <= ext || (uint64_t)(iso->volume - ext) < (((uint64_t)sz + 0x7ff) >> 11))
                return false;
            mh_str_copy(e.name, sizeof e.name, nm);
            e.path[0] = 0;
            if (prefix[0]) {
                mh_str_copy(e.path, sizeof e.path, prefix);
                mh_str_cat(e.path, sizeof e.path, "/");
            }
            mh_str_cat(e.path, sizeof e.path, nm);
            v(iso, &e, ctx);
            if (e.is_dir && sz != 0) {
                bool seen = false;
                for (uint32_t k = 0; k < iso->nvisited; k++)
                    if (iso->visited[k] == ext) { seen = true; break; }
                if (seen) continue; /* same extent as an ancestor or an earlier directory: skip */
                if (iso->nvisited >= 2048) return false;
                iso->visited[iso->nvisited++] = ext;
                if (!ps_walk_rec(iso, ext, sz, e.path, depth + 1, v, ctx)) return false;
            }
        }
    }
    return true;
}

/* orig: psserial_walk_directory, with a visited set of directory extents and a budget */
static bool ps_walk(PsIso *iso, uint32_t lba, uint32_t len, const char *prefix, int depth, Visitor v, void *ctx)
{
    iso->nvisited = 1;
    iso->visited[0] = lba;
    iso->records = iso->reads = 0;
    return ps_walk_rec(iso, lba, len, prefix, depth, v, ctx);
}

static const char *last_component(const char *path)
{
    const char *r = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\') r = p + 1;
    return r;
}

static bool has_slash(const char *path)
{
    for (; *path; path++)
        if (*path == '/') return true;
    return false;
}

/* ---- visitors ------------------------------------------------------------------------ */

typedef struct SysCnfCtx { /* orig: 0x314 bytes */
    uint32_t count;
    uint32_t chose_root;
    PsEntry entry;
} SysCnfCtx;

/* orig: psserial_find_system_cnf_visitor @0x1400299f0 */
static bool v_system_cnf(PsIso *iso, const PsEntry *e, void *vctx)
{
    SysCnfCtx *c = vctx;
    (void)iso;
    if (e->is_dir) return true;
    if (mh_str_icmp(last_component(e->path), "SYSTEM.CNF") != 0) return true;
    c->count++;
    if (c->chose_root && e->path[0] && has_slash(e->path)) return true;
    c->entry = *e;
    c->chose_root = has_slash(e->path) ? 0 : 1;
    return true;
}

typedef struct FindCtx { /* orig: 0x318 bytes: path pointer, found flag, entry */
    const char *path;
    uint32_t found;
    PsEntry entry;
} FindCtx;

/* orig: psserial_find_entry_visitor @0x140029910 */
static bool v_find_entry(PsIso *iso, const PsEntry *e, void *vctx)
{
    FindCtx *c = vctx;
    (void)iso;
    if (!c->found && mh_str_icmp(e->path, c->path) == 0) {
        c->entry = *e;
        c->found = 1;
    }
    return true;
}

typedef struct ExeCtx { /* orig: 0xe618 bytes */
    uint32_t count;
    uint32_t overflow;
    int32_t psx_index; /* root "PSX.EXE" */
    PsEntry exe[0x40];
    Cands cands;
} ExeCtx;

/* orig: psserial_collect_exes_visitor @0x140029430 */
static bool v_collect_exes(PsIso *iso, const PsEntry *e, void *vctx)
{
    ExeCtx *c = vctx;
    const char *name;
    char raw[32], canon[16];
    bool serial_name, candidate, is_psx;
    uint8_t magic[8];
    if (e->is_dir) return true;
    name = last_component(e->path);
    serial_name = mh_psserial_parse_filename(name, raw, canon);
    candidate = (strlen(name) >= 4 && mh_str_icmp(name + strlen(name) - 4, ".EXE") == 0) || serial_name;
    is_psx = !has_slash(e->path) && mh_str_icmp(name, "PSX.EXE") == 0;
    if (candidate && e->size >= 8) {
        if (ps_read_bytes(iso, e, magic, 8) && memcmp(magic, "PS-X EXE", 8) == 0) {
            uint32_t n = c->count;
            if (n < 0x40) {
                c->exe[n] = *e;
                if (is_psx) c->psx_index = (int32_t)n;
                if (serial_name) cand_add(&c->cands, canon, raw, e->path);
                c->count = n + 1;
            } else {
                c->overflow = 1;
                c->count = n + 1;
            }
        }
    }
    return true;
}

/* orig: psserial_scan_executable @0x14002a450. Looks for serial tokens in the file content;
 * a token must not be glued to identifier characters. */
static bool scan_executable(PsIso *iso, const PsEntry *e, Cands *cands)
{
    uint8_t magic[8], carry[16], tmp[0x800], *buf;
    uint32_t total = e->size, off = 0;
    size_t carry_len = 0;
    if (e->is_dir || e->size < 8) return false;
    if (!ps_read_bytes(iso, e, magic, 8) || memcmp(magic, "PS-X EXE", 8) != 0) return false;
    buf = malloc(0x800 + 16);
    if (!buf) return false;
    while (off < total) {
        size_t n = total - off > 0x800 ? 0x800 : total - off, len, pos;
        uint32_t sector = (off >> 11) + e->lba;
        if (sector >= iso->volume || !ps_read_sector(iso, sector, tmp)) {
            free(buf);
            return false;
        }
        memcpy(buf, carry, carry_len);
        memcpy(buf + carry_len, tmp, n);
        len = carry_len + n;
        pos = 0;
        while (pos < len) {
            char raw[32], canon[16];
            size_t used = 0, end;
            if (pos != 0 && is_glue(buf[pos - 1])) { pos++; continue; }
            if (!parse_token(buf + pos, len - pos, raw, canon, &used)) { pos++; continue; }
            end = pos + used;
            if (end >= len) {
                if (off + n == total || len != end) {
                    cand_add(cands, canon, raw, e->path);
                    pos = end;
                    continue;
                }
                pos++; /* token may continue in the next chunk (it stays in the carry) */
                continue;
            }
            if (is_glue(buf[end])) { pos++; continue; }
            cand_add(cands, canon, raw, e->path);
            pos = end;
        }
        carry_len = len > 16 ? 16 : len;
        memcpy(carry, buf + (len - carry_len), carry_len);
        off += 0x800;
    }
    free(buf);
    return true;
}

/* ---- the extraction ------------------------------------------------------------------ */

static void ps_close(PsFile *f)
{
    if (!f) return;
    mh_io_close(&f->io);
    free(f);
}

static uint32_t fail(MhPsSerial *r, PsFile *f, uint32_t status, const char *msg)
{
    ps_close(f);
    r->status = status;
    mh_str_copy(r->message, sizeof r->message, msg);
    return r->status;
}

/* Validates a PVD in b (0x800 bytes) against a stride; fills iso->volume/root. */
static bool pvd_ok(const uint8_t *b, uint64_t file_size, uint32_t stride, PsIso *iso)
{
    uint32_t lba, len, vol, blocks;
    if (b[0] != 1 || memcmp(b + 1, "CD001", 5) != 0 || b[6] != 1) return false;
    if (b[0x9c] < 0x22 || b[0xbc] != 1 || b[0xbd] != 0 || !(b[0xb5] & 2)) return false;
    lba = le32(b + 0x9e);
    if (be32(b + 0xa2) != lba) return false;
    len = le32(b + 0xa6);
    if (len == 0 || be32(b + 0xaa) != len) return false;
    vol = le32(b + 0x50);
    if (vol == 0 || be32(b + 0x54) != vol || lba == 0 || vol <= lba) return false;
    blocks = (uint32_t)(((uint64_t)len + 0x7ff) >> 11);
    if (vol - lba < blocks) return false;
    if (file_size < ((uint64_t)blocks + lba) * stride) return false;
    iso->volume = vol;
    iso->root_lba = lba;
    iso->root_len = len;
    return true;
}

/* Parses the value of a BOOT= line. Returns false for "empty". out gets the normalised path
 * (relative, '/' separators, no ";n"). orig: inline code of extract_mapped. */
static bool parse_boot_value(const char *v, char out[512])
{
    const char *p = v, *end;
    bool quoted = false;
    size_t n = 0, k;
    while (*p && is_ws((unsigned char)*p)) p++;
    if (*p == 0) return false;
    if (*p == '"') { quoted = true; p++; }
    if (up((unsigned char)p[0]) == 'C' && up((unsigned char)p[1]) == 'D' && up((unsigned char)p[2]) == 'R' &&
        up((unsigned char)p[3]) == 'O' && up((unsigned char)p[4]) == 'M' && p[5] == ':')
        p += 6;
    while (*p == '/' || *p == '\\') p++;
    if (*p == 0) return false;
    end = p;
    if (quoted) { while (*end && *end != '"') end++; }
    else { while (*end && *end != '\r' && *end != '\n') end++; }
    if (end <= p) return false;
    while (end > p && is_ws((unsigned char)end[-1])) end--;
    if (end == p) return false;
    n = (size_t)(end - p);
    if (n > 0x1ff) return false; /* original: more than 511 characters counts as empty */
    for (k = 0; k < n; k++) out[k] = p[k] == '\\' ? '/' : p[k];
    out[n] = 0;
    { /* strip ";digits" */
        char *last = NULL, *q;
        for (q = out; *q; q++)
            if (*q == ';') last = q;
        if (last && last[1]) {
            bool digits = true;
            for (q = last + 1; *q; q++)
                if (!is_digit((unsigned char)*q)) { digits = false; break; }
            if (digits) *last = 0;
        }
    }
    return true;
}

/* Scans SYSTEM.CNF text for BOOT/BOOT2. Returns true with boot filled; false on none or on
 * conflicting values. */
static bool find_boot_line(const char *buf, size_t total, char boot[512], bool *conflict)
{
    size_t pos = 0;
    bool have = false;
    *conflict = false;
    boot[0] = 0;
    while (pos < total) {
        size_t e = pos, next, eq, ks, ke, klen;
        char val[0x220];
        size_t vl;
        char path[512];
        while (e < total && buf[e] != '\n' && buf[e] != '\r') e++;
        next = e;
        while (next < total && (buf[next] == '\r' || buf[next] == '\n')) next++;
        for (eq = pos; eq < e && buf[eq] != '='; eq++) {}
        if (eq >= e) { pos = next; continue; }
        ks = pos;
        while (ks < eq && is_ws((unsigned char)buf[ks])) ks++;
        ke = eq;
        while (ke > ks && is_ws((unsigned char)buf[ke - 1])) ke--;
        klen = ke - ks;
        if (klen == 4) {
            if (mh_str_starts_i(buf + ks, "BOOT") == false) { pos = next; continue; }
        } else if (klen == 5) {
            if (!mh_str_starts_i(buf + ks, "BOOT") || buf[ks + 4] != '2') { pos = next; continue; }
        } else { pos = next; continue; }
        vl = e - eq - 1;
        if (vl > 0x21f) vl = 0x21f;
        memcpy(val, buf + eq + 1, vl);
        val[vl] = 0;
        if (parse_boot_value(val, path)) {
            if (have) {
                if (mh_str_icmp(boot, path) != 0) { *conflict = true; return false; }
            } else {
                have = true;
                mh_str_copy(boot, 512, path);
            }
        }
        pos = next;
    }
    return have;
}

static uint32_t extract_mapped(MhPsSerial *r, const char *path, uint64_t lba512, uint32_t sectors)
{
    PsFile *f = calloc(1, sizeof *f);
    PsIso iso;
    uint8_t blk[0x800];
    uint64_t fsize = 0;
    SysCnfCtx *sc = NULL;
    ExeCtx *ex = NULL;
    uint8_t *cnf = NULL;
    Cands *cd = NULL;
    uint32_t ret;
    if (!f) {
        r->status = MH_PSS_IO_ERROR;
        mh_str_copy(r->message, sizeof r->message, "Nao foi possivel abrir a imagem.");
        return r->status;
    }
    if (!mh_io_open(&f->io, path, MH_IO_READ, false, NULL)) {
        free(f);
        r->status = MH_PSS_IO_ERROR;
        mh_str_copy(r->message, sizeof r->message, "Nao foi possivel abrir a imagem.");
        return r->status;
    }
    if (sectors != 0) {
        f->size = (uint64_t)sectors * 0x930;
        f->raw = 1;
        f->base = lba512 << 9;
    } else {
        if (!mh_io_size(&f->io, &fsize)) {
            mh_io_close(&f->io);
            free(f);
            r->status = MH_PSS_IO_ERROR;
            mh_str_copy(r->message, sizeof r->message, "Nao foi possivel abrir a imagem.");
            return r->status;
        }
        f->size = fsize;
    }
    memset(&iso, 0, sizeof iso);
    iso.f = f;
    /* PVD: sector 16 as mode 2 form 1 (0x9318), mode 1 (0x9310) or cooked 2048 (0x8000) */
    {
        bool found = false;
        if (f->size >= 0x9b18) {
            f->pos = 0x9318;
            if (ps_read(f, blk) == 0x800 && pvd_ok(blk, f->size, 0x930, &iso)) {
                iso.stride = 0x930; iso.udo = 0x18; found = true;
            }
        }
        if (!found && f->size >= 0x9b10) {
            f->pos = 0x9310;
            if (ps_read(f, blk) == 0x800 && pvd_ok(blk, f->size, 0x930, &iso)) {
                iso.stride = 0x930; iso.udo = 0x10; found = true;
            }
        }
        if (!found && f->size >= 0x8800) {
            f->pos = 0x8000;
            if (ps_read(f, blk) == 0x800 && pvd_ok(blk, f->size, 0x800, &iso)) {
                iso.stride = 0x800; iso.udo = 0; found = true;
            }
        }
        if (!found)
            return fail(r, f, MH_PSS_BAD_IMAGE, "PVD ISO9660/layout invalido ou diretorio raiz fora da imagem.");
    }
    r->iso_sectors = iso.volume;
    r->file_sectors = (uint32_t)(f->size / iso.stride);
    r->truncated = f->size < (uint64_t)iso.volume * iso.stride;

    sc = calloc(1, sizeof *sc);
    ex = calloc(1, sizeof *ex);
    cd = calloc(1, sizeof *cd);
    if (!sc || !ex || !cd) {
        free(sc); free(ex); free(cd);
        return fail(r, f, MH_PSS_IO_ERROR, "Nao foi possivel abrir a imagem.");
    }
    if (!ps_walk(&iso, iso.root_lba, iso.root_len, "", 0, v_system_cnf, sc)) {
        free(sc); free(ex); free(cd);
        return fail(r, f, MH_PSS_IO_ERROR, "Falha ao percorrer o sistema de arquivos ISO9660.");
    }

    if (sc->count != 0) {
        uint32_t csz = sc->entry.size;
        r->system_cnf_found = 1;
        if (csz < 0x10001) {
            cnf = malloc((size_t)csz + 1);
            if (cnf && ps_read_bytes(&iso, &sc->entry, cnf, csz)) {
                char boot[512];
                bool conflict;
                cnf[csz] = 0;
                if (csz != 0 && find_boot_line((const char *)cnf, csz, boot, &conflict)) {
                    FindCtx *fc = calloc(1, sizeof *fc);
                    if (fc) {
                        fc->path = boot;
                        if (ps_walk(&iso, iso.root_lba, iso.root_len, "", 0, v_find_entry, fc) && fc->found) {
                            PsEntry *be = &fc->entry;
                            uint8_t magic[8];
                            r->boot_path_found = 1;
                            mh_str_copy(r->path, sizeof r->path, be->path);
                            if (!be->is_dir && be->size >= 8 && ps_read_bytes(&iso, be, magic, 8) &&
                                memcmp(magic, "PS-X EXE", 8) == 0) {
                                char raw[32], canon[16];
                                r->boot_exe_valid = 1;
                                if (mh_psserial_parse_filename(last_component(be->path), raw, canon)) {
                                    r->needs_hash = 1;
                                    r->cand_count = 1;
                                    r->status = MH_PSS_STRONG;
                                    r->source = MH_PSSRC_SYSTEM_CNF;
                                    mh_str_copy(r->token, sizeof r->token, raw);
                                    mh_str_copy(r->serial, sizeof r->serial, canon);
                                    mh_str_copy(r->cand[0], 16, canon);
                                    mh_str_copy(r->message, sizeof r->message,
                                                "Serial extraido do BOOT do SYSTEM.CNF; use trackset hash para confirmar a revisao fisica.");
                                    free(fc); free(cnf); free(sc); free(ex); free(cd);
                                    ret = r->status;
                                    ps_close(f);
                                    return ret;
                                }
                                memset(cd, 0, sizeof *cd);
                                if (scan_executable(&iso, be, cd)) {
                                    cand_finish(r, cd, MH_PSSRC_EXE_CONTENT,
                                                "Serial unico encontrado heuristicamente no executavel de boot.",
                                                "Multiplos serials encontrados no executavel de boot.");
                                    if (cd->count != 0) {
                                        free(fc); free(cnf); free(sc); free(ex); free(cd);
                                        ret = r->status;
                                        ps_close(f);
                                        return ret;
                                    }
                                }
                            }
                        }
                        free(fc);
                    }
                }
            }
            free(cnf);
            cnf = NULL;
        }
    }

    /* no usable SYSTEM.CNF: look at the executables */
    ex->psx_index = -1;
    if (!ps_walk(&iso, iso.root_lba, iso.root_len, "", 0, v_collect_exes, ex)) {
        free(sc); free(ex); free(cd);
        return fail(r, f, MH_PSS_IO_ERROR, "Falha ao enumerar executaveis PS-X EXE.");
    }
    if (ex->psx_index < 0) {
        if (ex->cands.overflow != 0 || ex->cands.count != 0) {
            cand_finish(r, &ex->cands, MH_PSSRC_EXE_NAME,
                        "Executavel PS-X EXE com nome de serial unico, sem SYSTEM.CNF.",
                        "Mais de um serial de executavel e possivel sem SYSTEM.CNF.");
            r->boot_exe_valid = 1;
        } else if (ex->overflow == 0 && ex->count < 0x41) {
            if (ex->count == 0) {
                r->status = MH_PSS_NOT_FOUND;
                r->needs_hash = 1;
                mh_str_copy(r->message, sizeof r->message,
                            "SYSTEM.CNF e executavel PS-X EXE identificavel nao encontrados; resolver pelo trackset hash.");
                free(sc); free(ex); free(cd);
                ps_close(f);
                return r->status;
            }
            memset(cd, 0, sizeof *cd);
            r->boot_exe_valid = 1;
            for (uint32_t i = 0; i < ex->count; i++) {
                if (!scan_executable(&iso, &ex->exe[i], cd)) {
                    free(sc); free(ex); free(cd);
                    return fail(r, f, MH_PSS_IO_ERROR, "Falha durante a leitura de executavel PS-X EXE.");
                }
            }
            cand_finish(r, cd, MH_PSSRC_EXE_CONTENT,
                        "Serial unico encontrado em executavel PS-X EXE; boot nao confirmado.",
                        "Multiplos serials encontrados em executaveis PS-X EXE; boot nao confirmado.");
            if (cd->count == 0)
                mh_str_copy(r->message, sizeof r->message,
                            "Executavel PS-X EXE localizado sem serial interno; resolver pelo trackset hash.");
        } else {
            r->status = MH_PSS_AMBIGUOUS;
            r->needs_hash = 1;
            mh_str_copy(r->message, sizeof r->message,
                        "Quantidade de executaveis excede o limite heuristico; resolver pelo trackset hash.");
        }
    } else {
        PsEntry *pe = &ex->exe[ex->psx_index];
        bool scanned;
        memset(cd, 0, sizeof *cd);
        r->boot_path_found = 1;
        r->boot_exe_valid = 1;
        mh_str_copy(r->path, sizeof r->path, pe->path);
        scanned = scan_executable(&iso, pe, cd);
        if (scanned)
            cand_finish(r, cd, MH_PSSRC_EXE_CONTENT, "PSX.EXE valido; serial interno unico encontrado heuristicamente.",
                        "PSX.EXE valido, mas contem multiplos candidatos de serial.");
        if (!scanned || cd->count == 0) {
            r->status = MH_PSS_NOT_FOUND;
            r->needs_hash = 1;
            mh_str_copy(r->message, sizeof r->message,
                        "PSX.EXE valido localizado, mas ele nao fornece serial; resolver pelo trackset hash.");
        }
    }
    free(sc); free(ex); free(cd);
    ret = r->status;
    ps_close(f);
    return ret;
}

void mh_psserial_init(MhPsSerial *r)
{
    memset(r, 0, sizeof *r);
    r->needs_hash = 1;
}

/* orig: extractSerialDetailed @0x140019b70 */
uint32_t mh_psserial_extract(MhPsSerial *r, const char *path)
{
    if (!r) return MH_PSS_IO_ERROR;
    mh_psserial_init(r);
    if (!path || !path[0]) {
        r->status = MH_PSS_IO_ERROR;
        mh_str_copy(r->message, sizeof r->message, "Caminho da imagem vazio.");
        return r->status;
    }
    return extract_mapped(r, path, 0, 0);
}

/* orig: extractSerialRaw @0x140019c00 */
uint32_t mh_psserial_extract_raw(MhPsSerial *r, const char *path, uint64_t lba512, uint32_t sectors)
{
    if (!r) return MH_PSS_IO_ERROR;
    mh_psserial_init(r);
    if (!path || !path[0]) {
        r->status = MH_PSS_IO_ERROR;
        mh_str_copy(r->message, sizeof r->message, "Caminho da imagem vazio.");
        return r->status;
    }
    return extract_mapped(r, path, lba512, sectors);
}

/* orig: extractSerial @0x140019aa0 */
bool mh_psserial_extract_serial(char serial[16], const char *path)
{
    MhPsSerial r;
    uint32_t st;
    if (!serial) return false;
    serial[0] = 0;
    st = mh_psserial_extract(&r, path);
    if (st == MH_PSS_STRONG || r.status == MH_PSS_PROBABLE) {
        mh_str_copy(serial, 16, r.serial);
        return true;
    }
    return false;
}

/* orig: cue_scan_serial_file @0x140014c60 */
bool mh_cue_scan_serial_file(const char *bin_path, char *out, size_t n)
{
    MhPsSerial r;
    if (!out || n == 0) return false;
    out[0] = 0;
    {
        uint32_t st = mh_psserial_extract(&r, bin_path);
        if (st - 1U < 2) {
            mh_str_copy(out, n, r.serial);
            return out[0] != 0;
        }
    }
    return false;
}
