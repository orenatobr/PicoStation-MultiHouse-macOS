/* Firmware oracle for the V2 card layout.
 *
 * The checks below are NOT a port of the Manager: they are transcribed from the Ghidra
 * decompilation of PicoStation_MH_V3.uf2 (RP2040, flash at 0x10000000, see
 * docs/FIRMWARE_CROSSCHECK.md). Every fw_* function names the firmware function it mirrors.
 * Images written by the V2 writer must pass them, otherwise the Pico would not read the card.
 * The writer harness of test_v2_write.c is reused (that file is included, its main renamed). */
#define main write_tests_main
#include "test_v2_write.c"
#undef main

/* ---- little helpers (own decoders, on purpose not mh_v2_rd*) ------------------------------ */

static uint32_t L16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t L32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

/* FUN_10021748 / FUN_10021774: reflected CRC-32, poly 0xEDB88320, init 0xFFFFFFFF, final xor. */
static uint32_t fw_crc(const uint8_t *p, size_t n)
{
    uint32_t c = 0xffffffffu;
    while (n--) {
        int k;
        c ^= *p++;
        for (k = 0; k < 8; k++) c = (c >> 1) ^ (-(c & 1) & 0xedb88320u);
    }
    return ~c;
}

static bool all_zero(const uint8_t *p, size_t n)
{
    while (n--) if (*p++) return false;
    return true;
}

typedef struct { FILE *f; uint64_t size; } Fw;

/* reads from a 512-byte LBA, `off` bytes further; false past the end of the card */
static bool fw_rd(const Fw *w, uint64_t lba, uint64_t off, void *out, size_t n)
{
    uint64_t at = lba * 512 + off;
    if (at + n > w->size) return false;
    if (fseeko(w->f, (off_t)at, SEEK_SET) != 0) return false;
    return fread(out, 1, n, w->f) == n;
}

typedef struct {
    uint32_t gen, ncases, ndiscs, case_lba, disc_lba, art_lba, menu_lba, menu_sectors, cfg_lba, min_lba;
} FwSt;

/* FUN_10021aa4 (fields) plus FUN_1002200c (bytes 0x930..0xA00 must be zero). */
static bool fw_hdr_ok(const uint8_t *s)
{
    uint32_t nc = L16(s + 8), nd = L16(s + 10), ms = L32(s + 0x3c);
    if (memcmp(s, "PSXRAW", 6) != 0 || s[6] != 2 || !(s[7] & 1)) return false;
    if (nc > 0x100 || nd > 0x100 || (nc == 0) != (nd == 0)) return false;
    if (L32(s + 0x10) != 0x800 || L32(s + 0x14) != 5 || L32(s + 0x18) != 0) return false;
    if (L32(s + 0x1c) != 10 || L32(s + 0x20) != 0 || L32(s + 0x24) != 0x32 || L32(s + 0x28) != 0) return false;
    if (L32(s + 0x2c) != 0xb4 || L32(s + 0x30) != 0 || L32(s + 0x34) != 0x14b4 || L32(s + 0x38) != 0) return false;
    if (ms - 1 >= 0x5d || L32(s + 0x40) != 0x1685 || L32(s + 0x44) != 0) return false;
    if (L32(s + 0x48) != 0x2800 || L32(s + 0x4c) != 0) return false;
    if (fw_crc(s, 0x92c) != L32(s + 0x92c)) return false;
    return all_zero(s + 0x930, 0xd0);
}

/* The A/B choice of FUN_10021280: A is kept on a tie, B wins only when it is newer in serial
 * number arithmetic. Written literally as the decompilation reads (uVar12 = B, uVar11 = A). */
static bool fw_b_wins(uint32_t a, uint32_t b)
{
    if (a == b) return false;
    if (a < b) return !((int32_t)(b - a) < 0);
    return !(a - b < 0x80000001u);
}

static bool fw_mount(const Fw *w, FwSt *st)
{
    uint8_t a[0xa00], b[0xa00], *h;
    bool aok = fw_rd(w, 0, 0, a, sizeof a) && fw_hdr_ok(a);
    bool bok = fw_rd(w, 5, 0, b, sizeof b) && fw_hdr_ok(b);
    if (!aok && !bok) return false;
    h = (aok && !(bok && fw_b_wins(L32(a + 12), L32(b + 12)))) ? a : b;
    st->gen = L32(h + 12);
    st->ncases = L16(h + 8);
    st->ndiscs = L16(h + 10);
    st->case_lba = L32(h + 0x1c);
    st->disc_lba = L32(h + 0x24);
    st->art_lba = L32(h + 0x2c);
    st->menu_lba = L32(h + 0x34);
    st->menu_sectors = L32(h + 0x3c);
    st->cfg_lba = L32(h + 0x40);
    st->min_lba = L32(h + 0x48); /* the firmware reuses art_size (0x2800) as the first package LBA */
    return true;
}

typedef struct { uint8_t idx, ndisc, ids[5]; uint16_t art; } FwCase;

/* FUN_10021960: -1 invalid, 0 empty (all 0x50 bytes zero), 1 valid. */
static int fw_case(const Fw *w, const FwSt *st, unsigned idx, FwCase *o)
{
    uint8_t r[0x50];
    unsigned i, j;
    if (!fw_rd(w, st->case_lba, (uint64_t)idx * 0x50, r, sizeof r)) return -1;
    if (r[4] == 0) return all_zero(r, sizeof r) ? 0 : -1;
    if (memcmp(r, "CASE", 4) != 0 || r[6] != (idx & 0xff) || (unsigned)(r[5] - 1) >= 5) return -1;
    if (!all_zero(r + 0x3f, 0xd)) return -1;
    if ((uint16_t)(L16(r + 0x38) + 1) >= 0x101) return -1;
    if (fw_crc(r, 0x4c) != L32(r + 0x4c)) return -1;
    for (i = 0; i < r[5]; i++)
        for (j = 0; j < i; j++)
            if (r[0x3a + i] == r[0x3a + j]) return -1;
    for (i = r[5]; i < 5; i++)
        if (r[0x3a + i]) return -1;
    o->idx = (uint8_t)idx;
    o->ndisc = r[5];
    o->art = (uint16_t)L16(r + 0x38);
    memcpy(o->ids, r + 0x3a, 5);
    return 1;
}

typedef struct {
    uint8_t id, case_idx, slot, tracks;
    uint32_t flags, base, track_lba, raw_lba, raw_blocks, sectors, cheat_lba, cheat_size, extra_lba, extra_size;
    uint16_t art;
} FwDisc;

/* FUN_10021d84 */
static bool fw_disc(const Fw *w, const FwSt *st, unsigned id, FwDisc *o)
{
    uint8_t r[0x100];
    uint32_t flags, base, sectors, cmax, ext;
    if (!fw_rd(w, st->disc_lba, (uint64_t)id * 0x100, r, sizeof r) || r[4] == 0) return false;
    flags = L32(r + 8);
    ext = (flags >> 1) & 1;
    cmax = ext ? 0x2800 : 0x1400;
    base = L32(r + 0x3c);
    sectors = L32(r + 0x5c);
    if (memcmp(r, "DISC", 4) != 0 || r[5] != id || flags >= 4) return false;
    if ((unsigned)(r[0x60] - 1) >= 99 || sectors == 0) return false;
    if (L32(r + 0x40) || L32(r + 0x48) || L32(r + 0x50) || L32(r + 0x58) || L32(r + 0x68) || L32(r + 0x74)) return false;
    if (base < st->min_lba || (base & 0xff) || L32(r + 0x44) != base || L32(r + 0x4c) != base + 0x100) return false;
    if (sectors > 0x19999999u || L32(r + 0x54) != sectors * 5) return false;
    if ((uint16_t)(L16(r + 0x62) + 1) >= 0x101 || L32(r + 0x6c) > cmax) return false;
    if (L32(r + 0x64) == 0 ? L32(r + 0x6c) != 0 : L32(r + 0x64) != base + 5) return false;
    if (L32(r + 0x78) > 0xa00) return false;
    if (L32(r + 0x70) == 0 ? L32(r + 0x78) != 0 : L32(r + 0x70) != base + (ext ? 0x19 : 0xf)) return false;
    if (!all_zero(r + 0x94, 0x68)) return false;
    if (fw_crc(r, 0xfc) != L32(r + 0xfc)) return false;
    o->id = r[5]; o->case_idx = r[6]; o->slot = r[7]; o->tracks = r[0x60];
    o->flags = flags; o->base = base; o->track_lba = L32(r + 0x44); o->raw_lba = L32(r + 0x4c);
    o->raw_blocks = L32(r + 0x54); o->sectors = sectors; o->art = (uint16_t)L16(r + 0x62);
    o->cheat_lba = L32(r + 0x64); o->cheat_size = L32(r + 0x6c);
    o->extra_lba = L32(r + 0x70); o->extra_size = L32(r + 0x78);
    return true;
}

/* FUN_10022084 (the slot read goes through FUN_1002200c: 0x930..0xA00 zero). */
static bool fw_trackmap(const Fw *w, const FwDisc *d)
{
    uint8_t s[0xa00], t[0x930];
    uint32_t crc, run = 0, i;
    bool data = false;
    if (!fw_rd(w, d->track_lba, 0, s, sizeof s) || !all_zero(s + 0x930, 0xd0)) return false;
    if (memcmp(s, "PSTTRK1\0", 8) != 0 || s[8] != d->tracks || s[9] != 0x10 || L16(s + 10) >= 2) return false;
    /* three accepted CRC forms: 0x930 or 0x640 bytes with the field zeroed, or the entries alone */
    memcpy(t, s, sizeof t);
    memset(t + 0xc, 0, 4);
    crc = L32(s + 0xc);
    if (crc != fw_crc(t, 0x930) && crc != fw_crc(t, 0x640) && crc != fw_crc(s + 0x10, 0x630)) return false;
    for (i = 0; i < d->tracks; i++) {
        const uint8_t *e = s + 0x10 + 16 * i;
        uint32_t start = L32(e), idx1 = L32(e + 4), len = L32(e + 8), end = start + len;
        if (len == 0 || start != run || end < run || idx1 < run || end <= idx1 || end > d->sectors) return false;
        if (e[0xc] > 2 || e[0xd] > 0xf || e[0xf]) return false;
        if (i == 0 && e[0xc] == 0) return false;
        if (e[0xc]) data = true;
        run = end;
    }
    return run == d->sectors && data && all_zero(s + 0x10 + 16 * d->tracks, (99 - d->tracks) * 16);
}

/* printable name test of the cheat records: 0x20..0x7E without '[' and ']' */
static bool fw_name_char(uint8_t c) { return c >= 0x20 && c <= 0x7e && c != '[' && c != ']'; }

/* The cheat blob: header common to v4 and v6 (FUN_100228c8 for v4, FUN_1002c0a0 for v6). */
static bool fw_cheat(const Fw *w, const FwDisc *d)
{
    uint8_t *b;
    uint32_t n, size = d->cheat_size, run, i, lines = 0, enabled = 0, ver, crc;
    uint8_t seen[32] = {0};
    bool ok = false;
    if (size == 0) return true;
    b = malloc(size);
    if (!b || !fw_rd(w, d->cheat_lba, 0, b, size)) goto out;
    if (size < 0x40 || memcmp(b, "PSTCHT1\0", 8) != 0) goto out;
    ver = L16(b + 8);
    n = L32(b + 0x10);
    if (b[0xa] != 0x40 || L32(b + 0xc) != 0 || n - 1 >= 0x100 || L32(b + 0x20) != 0x40 || b[0x3a] != 0) goto out;
    if (b[0x38] != 2 || b[0x39] > 1 || L32(b + 0x28) != size) goto out; /* only disc scope (2) is served */
    {
        uint8_t *t = malloc(size);
        memcpy(t, b, size);
        memset(t + 0x3c, 0, 4);
        crc = fw_crc(t, size);
        free(t);
        if (crc != L32(b + 0x3c)) goto out;
    }
    if (ver == 6) {
        run = (n + 8) * 8;
        if (L32(b + 0x1c) != 8 || L32(b + 0x24) != run || run > size) goto out;
        if (L32(b + 0x2c) >= 0x3ec || L32(b + 0x30) != L32(b + 0x2c) || L32(b + 0x34) != 0xa06cfc31u || b[0x39] != 1) goto out;
        for (i = 0; i < n; i++) {
            const uint8_t *r = b + 0x40 + 8 * i;
            uint32_t name_off = L16(r), code_off = L16(r + 2), pos = L16(r + 4), p, k;
            uint8_t nl = r[6], fl = r[7];
            if (pos > 0xff || nl == 0 || (fl & 0xcc) != 8 || (fl & 3) == 2 || name_off != run) goto out;
            if (seen[pos >> 3] & (1u << (pos & 7))) goto out;
            seen[pos >> 3] |= (uint8_t)(1u << (pos & 7));
            for (p = run;; p++) {
                uint8_t c;
                if (p >= size) goto out;
                c = b[p];
                if (c == 0) break;
                if (p + 1 == run + 0x30) goto out;
                if (!(c == ' ' || c == '&' || c == '\'' || c == '-' || c == '.' || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z'))) goto out;
            }
            if (p == run || code_off != p + 1 || size - code_off < (uint32_t)nl * 6u) goto out;
            for (k = 0; k < nl; k++) {
                uint8_t o = b[code_off + k * 6];
                if (!((o & 0x7f) == 0 || (o & 0xee) == 0x20 || o == 0x70 || o == 0x90 || o == 0xc1)) goto out;
            }
            run = code_off + nl * 6u;
            enabled += fl & 1;
            lines += nl;
        }
        ok = run == size && enabled == L32(b + 0x14) && lines == L32(b + 0x18) && lines < 0x1ff;
    } else if (ver == 4) {
        run = n * 0x60 + 0x40;
        if (L32(b + 0x1c) != 0x60 || L32(b + 0x24) != run || run > size || L32(b + 0x18) > 0x100) goto out;
        if (b[0x39] == 0) {
            if (L32(b + 0x2c) != 0xffffffffu || L32(b + 0x30) != 0xffffffffu || L32(b + 0x34) != 0) goto out;
        } else if (L32(b + 0x2c) == 0xffffffffu || L32(b + 0x30) == 0xffffffffu || L32(b + 0x34) != 0xa06cfc31u) {
            goto out;
        }
        for (i = 0; i < n; i++) {
            const uint8_t *r = b + 0x40 + 0x60 * i;
            uint32_t nl = L16(r + 0x4c), code_off = L32(r + 0x48), k;
            uint8_t fl = r[0x4f], game = r[0x4e];
            if (nl == 0 || code_off != run || size - run < nl * 6 || fl > 0x3f) goto out;
            if (((fl >> 3) & 1) == ((fl >> 2) & 1) || ((fl & 0x10) && !(fl & 8))) goto out;
            if ((fl & 2) && (fl & 9) != 9) goto out;
            if (!(fl & 8)) {
                if (game != 0 || L32(r + 0x40) != 0xffffffffu || L32(r + 0x44) != 0xffffffffu) goto out;
            } else if (b[0x39] == 0 || game != b[0x39] || L32(r + 0x40) != L32(b + 0x30) || L32(r + 0x44) == 0xffffffffu) {
                goto out;
            }
            if (!all_zero(r + 0x54, 0xc) || fw_crc(b + code_off, nl * 6) != L32(r + 0x50)) goto out;
            for (k = 0; r[k]; k++)
                if (!fw_name_char(r[k]) || k + 1 == 0x40) goto out;
            if (k == 0 || !all_zero(r + k + 1, 0x3f - k)) goto out;
            run += nl * 6;
            enabled += fl & 1;
            lines += nl;
        }
        ok = run == size && enabled == L32(b + 0x14) && lines == L32(b + 0x18);
    }
out:
    free(b);
    return ok;
}

/* FUN_10024450 (PSTCFG2) and FUN_100245e0 (the music window). */
static bool fw_cfg(const Fw *w, const FwSt *st)
{
    uint8_t c[0xa00];
    uint32_t i, lba, size, crc;
    if (st->cfg_lba == 0 || st->cfg_lba + 5 > st->min_lba || !fw_rd(w, st->cfg_lba, 0, c, sizeof c)) return false;
    if (memcmp(c, "PSTCFG2\0", 8) != 0 || c[8] != 1 || c[9] != 0 || L32(c + 0x20) != 0) return false;
    if (fw_crc(c, 0x92c) != L32(c + 0x92c)) return false;
    for (i = 0; i < 0x800 && c[0x2c + i]; i++) {}
    if (i == 0 || i == 0x800) return false;
    lba = L32(c + 0x1c); size = L32(c + 0x24); crc = L32(c + 0x28);
    /* the music is served only when all three are set and it fits below the first package */
    return size != 0 && lba != 0 && crc != 0 && lba < st->min_lba && size <= (st->min_lba - lba) * 0x200u;
}

/* Everything the firmware touches when it lists the library and opens each disc. */
static bool fw_walk(const char *img, unsigned *ncases, unsigned *ndiscs)
{
    Fw w;
    FwSt st;
    unsigned i, j, nc = 0, nd = 0;
    bool ok = false;
    struct stat sb;
    if (stat(img, &sb) != 0) return false;
    w.f = fopen(img, "rb");
    w.size = (uint64_t)sb.st_size;
    if (!w.f) return false;
    if (!fw_mount(&w, &st) || !fw_cfg(&w, &st)) goto out;
    if (st.menu_lba * 512 + (uint64_t)st.menu_sectors * 0xa00 > (uint64_t)st.cfg_lba * 512) goto out;
    for (i = 0; i < 256; i++) {
        FwCase c;
        int r = fw_case(&w, &st, i, &c);
        if (r < 0) goto out;
        if (r == 0) continue;
        nc++;
        if (c.art != 0xffff && (uint64_t)st.art_lba * 512 + (uint64_t)(c.art + 1) * 0x2800 > (uint64_t)st.min_lba * 512) goto out;
        for (j = 0; j < c.ndisc; j++) {
            FwDisc d;
            uint64_t last;
            if (!fw_disc(&w, &st, c.ids[j], &d) || d.case_idx != c.idx || d.slot != j) goto out;
            if (!fw_trackmap(&w, &d) || !fw_cheat(&w, &d)) goto out;
            /* sector n of the disc is read from raw_lba + 5 * n (0x930 of the 0xA00 bytes are used) */
            last = (uint64_t)d.raw_lba * 512 + (uint64_t)(d.sectors - 1) * 5 * 512 + 0x930;
            if (last > w.size) goto out;
            if (d.art != 0xffff) {
                uint32_t at = d.base + ((d.flags & 2) ? 0x1e : 0x14);
                if ((uint64_t)at * 512 + 0x2800 > w.size) goto out;
            }
            nd++;
        }
    }
    ok = nc == st.ncases && nd == st.ndiscs;
    if (ncases) *ncases = nc;
    if (ndiscs) *ndiscs = nd;
out:
    fclose(w.f);
    return ok;
}

/* ============================================================================================
 * SECOND ORACLE: "firmware 0.2.0", card format 2.1 (long names).
 *
 * Everything above is the ORIGINAL firmware (PicoStation_MH_V3.uf2) and is left as it was: it
 * refuses any record that uses the extension. The functions below (fw2_*) implement the rules of
 * picostation-multihouse-fw/docs/LONG_NAMES.md on top of the same reads. A card without the
 * extension must pass BOTH oracles; a card with it passes only this one.
 * ============================================================================================ */

static bool fw2_printable(const uint8_t *p, size_t n)
{
    while (n--) if (*p < 0x20 || *p++ > 0x7e) return false;
    return true;
}

/* 31 printable characters followed by NUL */
static bool fw2_field31(const uint8_t *f) { return fw2_printable(f, 31) && f[31] == 0; }

/* FUN_10021960 with the 2.1 rule: +0x3F is 0 or 1 (1: 31 character base name), +0x40..+0x4B zero. */
static int fw2_case(const Fw *w, const FwSt *st, unsigned idx, FwCase *o, uint8_t raw[0x50])
{
    uint8_t *r = raw;
    unsigned i, j;
    if (!fw_rd(w, st->case_lba, (uint64_t)idx * 0x50, r, 0x50)) return -1;
    if (r[4] == 0) return all_zero(r, 0x50) ? 0 : -1;
    if (memcmp(r, "CASE", 4) != 0 || r[6] != (idx & 0xff) || (unsigned)(r[5] - 1) >= 5) return -1;
    if (r[0x3f] > 1 || !all_zero(r + 0x40, 0xc)) return -1;
    if (r[0x3f] == 1 && !fw2_field31(r + 8)) return -1;
    if ((uint16_t)(L16(r + 0x38) + 1) >= 0x101) return -1;
    if (fw_crc(r, 0x4c) != L32(r + 0x4c)) return -1;
    for (i = 0; i < r[5]; i++)
        for (j = 0; j < i; j++)
            if (r[0x3a + i] == r[0x3a + j]) return -1;
    for (i = r[5]; i < 5; i++)
        if (r[0x3a + i]) return -1;
    o->idx = (uint8_t)idx;
    o->ndisc = r[5];
    o->art = (uint16_t)L16(r + 0x38);
    memcpy(o->ids, r + 0x3a, 5);
    return 1;
}

/* The bytes +0x94..+0xFB of a Disc record under the 2.1 rules. `case_name` is the 0x20 byte name
 * field of the Case that owns the record. */
static bool fw2_ext_ok(const uint8_t *r, const uint8_t *case_name)
{
    unsigned tl, cl;
    if (memcmp(r + 0x94, "LNX1", 4) != 0) return all_zero(r + 0x94, 0x68);
    tl = r[0x98];
    cl = r[0x99];
    if (tl > 40 || cl > 40) return false;
    if (!all_zero(r + 0x9a + tl, 40 - tl) || !all_zero(r + 0xc2 + cl, 40 - cl) || !all_zero(r + 0xea, 18)) return false;
    if (!fw2_printable(r + 0x9a, tl) || !fw2_printable(r + 0xc2, cl)) return false;
    if (cl && r[7] != 0) return false;
    if (tl && !fw2_field31(r + 0x1c)) return false;
    if (cl && !fw2_field31(case_name)) return false;
    return true;
}

/* FUN_10021d84 with the 2.1 reserved bytes rule; raw gets the 0x100 bytes. */
static bool fw2_disc(const Fw *w, const FwSt *st, unsigned id, const uint8_t *case_name, FwDisc *o, uint8_t raw[0x100])
{
    uint8_t *r = raw;
    uint32_t flags, base, sectors, cmax, ext;
    if (!fw_rd(w, st->disc_lba, (uint64_t)id * 0x100, r, 0x100) || r[4] == 0) return false;
    flags = L32(r + 8);
    ext = (flags >> 1) & 1;
    cmax = ext ? 0x2800 : 0x1400;
    base = L32(r + 0x3c);
    sectors = L32(r + 0x5c);
    if (memcmp(r, "DISC", 4) != 0 || r[5] != id || flags >= 4) return false;
    if ((unsigned)(r[0x60] - 1) >= 99 || sectors == 0) return false;
    if (L32(r + 0x40) || L32(r + 0x48) || L32(r + 0x50) || L32(r + 0x58) || L32(r + 0x68) || L32(r + 0x74)) return false;
    if (base < st->min_lba || (base & 0xff) || L32(r + 0x44) != base || L32(r + 0x4c) != base + 0x100) return false;
    if (sectors > 0x19999999u || L32(r + 0x54) != sectors * 5) return false;
    if ((uint16_t)(L16(r + 0x62) + 1) >= 0x101 || L32(r + 0x6c) > cmax) return false;
    if (L32(r + 0x64) == 0 ? L32(r + 0x6c) != 0 : L32(r + 0x64) != base + 5) return false;
    if (L32(r + 0x78) > 0xa00) return false;
    if (L32(r + 0x70) == 0 ? L32(r + 0x78) != 0 : L32(r + 0x70) != base + (ext ? 0x19 : 0xf)) return false;
    if (!fw2_ext_ok(r, case_name)) return false;
    if (fw_crc(r, 0xfc) != L32(r + 0xfc)) return false;
    o->id = r[5]; o->case_idx = r[6]; o->slot = r[7]; o->tracks = r[0x60];
    o->flags = flags; o->base = base; o->track_lba = L32(r + 0x44); o->raw_lba = L32(r + 0x4c);
    o->raw_blocks = L32(r + 0x54); o->sectors = sectors; o->art = (uint16_t)L16(r + 0x62);
    o->cheat_lba = L32(r + 0x64); o->cheat_size = L32(r + 0x6c);
    o->extra_lba = L32(r + 0x70); o->extra_size = L32(r + 0x78);
    return true;
}

typedef struct {
    unsigned slot, ndisc;
    char name[96];       /* what the firmware shows for the Case */
    char title[5][96];   /* what it shows for each Disc, before " - DISCO n" */
} Fw2Row;

/* The library walk of firmware 0.2.0. rows (may be NULL) receives one row per Case in table order. */
static bool fw2_walk(const char *img, Fw2Row *rows, unsigned *ncases, unsigned *ndiscs)
{
    Fw w;
    FwSt st;
    unsigned i, j, nc = 0, nd = 0;
    bool ok = false;
    struct stat sb;
    if (stat(img, &sb) != 0) return false;
    w.f = fopen(img, "rb");
    w.size = (uint64_t)sb.st_size;
    if (!w.f) return false;
    if (!fw_mount(&w, &st) || !fw_cfg(&w, &st)) goto out;
    for (i = 0; i < 256; i++) {
        FwCase c;
        uint8_t craw[0x50];
        int r = fw2_case(&w, &st, i, &c, craw);
        Fw2Row *row = rows ? &rows[nc] : NULL;
        if (r < 0) goto out;
        if (r == 0) continue;
        if (row) {
            memset(row, 0, sizeof *row);
            row->slot = i;
            row->ndisc = c.ndisc;
            memcpy(row->name, craw + 8, 31);
        }
        nc++;
        for (j = 0; j < c.ndisc; j++) {
            FwDisc d;
            uint8_t draw[0x100];
            if (!fw2_disc(&w, &st, c.ids[j], craw + 8, &d, draw) || d.case_idx != c.idx || d.slot != j) goto out;
            if (!fw_trackmap(&w, &d) || !fw_cheat(&w, &d)) goto out;
            if (row) {
                memcpy(row->title[j], draw + 0x1c, strnlen((const char *)draw + 0x1c, 31));
                if (memcmp(draw + 0x94, "LNX1", 4) == 0) {
                    strncat(row->title[j], (const char *)draw + 0x9a, draw[0x98]);
                    /* the Case continuation comes from the record of slot 0 */
                    if (j == 0 && craw[0x3f] == 1 && draw[0x99])
                        strncat(row->name, (const char *)draw + 0xc2, draw[0x99]);
                }
            }
            nd++;
        }
    }
    ok = nc == st.ncases && nd == st.ndiscs;
    if (ncases) *ncases = nc;
    if (ndiscs) *ndiscs = nd;
out:
    fclose(w.f);
    return ok;
}

/* ---- tests -------------------------------------------------------------------------------- */

static void copy_file(const char *from, const char *to)
{
    uint64_t n;
    uint8_t *d = slurp_all(from, &n);
    write_file(to, d, (size_t)n);
    free(d);
}

/* the generation rule of the firmware equals the Manager's (v2_load.c: (int32)(b - a) < 1 keeps A) */
static void test_generation_rule(void)
{
    static const uint32_t v[] = {0, 1, 2, 5, 0x7fffffffu, 0x80000000u, 0x80000001u, 0xfffffffeu, 0xffffffffu};
    unsigned i, j;
    for (i = 0; i < sizeof v / sizeof *v; i++)
        for (j = 0; j < sizeof v / sizeof *v; j++)
            CHECK(fw_b_wins(v[i], v[j]) == !((int32_t)(v[j] - v[i]) < 1));
}

static void test_firmware_reads_writer_output(void)
{
    MhProject *p = new_project();
    char cue[MH_PATH_MAX], *img = tpath("fw_multi.img"), *bad = tpath("fw_bad.img");
    MhV2Result res;
    MhCheatProfile *cp;
    uint8_t lines[12] = {0x30, 0x00, 0x12, 0x34, 0x00, 0x05, 0x80, 0x00, 0x56, 0x78, 0x00, 0x09};
    unsigned nc = 0, nd = 0;

    make_synth("fw_alpha", 600, 400, false, 1, cue);
    project_add(p, cue, 256);
    make_synth("fw_bravo", 700, 300, true, 2, cue);
    project_add(p, cue, 0);
    make_synth("fw_charlie", 500, 0, false, 3, cue);
    project_add(p, cue, 256);
    mh_project_move_case(p, 1, 255);
    mh_project_recount(p);
    make_art(tpath("fw_case0.art"), 11);
    make_art(tpath("fw_disc_b.art"), 22);
    mh_str_copy(p->cases[0].art_path, MH_PATH_MAX, tpath("fw_case0.art"));
    mh_str_copy(p->cases[0].discs[1]->cd_art_path, MH_PATH_MAX, tpath("fw_disc_b.art"));
    cp = mh_cheat_profile_new();
    CHECK(mh_cheat_profile_add_custom(cp, "Vidas infinitas", lines, 2, 0));
    p->cases[255].discs[0]->cheats = cp; /* a version 4 blob, scope 2 */
    reset_progress();
    CHECK(mh_v2_write_image_direct(&g_w, p, img, &res));
    CHECK(res.ok);
    CHECK(fw_walk(img, &nc, &nd));
    CHECK(nc == 2 && nd == 3);

    /* the oracle is not vacuous: every region, when damaged, makes the firmware refuse the card */
    copy_file(img, bad);
    poke(bad, 0x1400 + 0x4c, 0x55 ^ 0); /* Case 0 CRC */
    CHECK(!fw_walk(bad, NULL, NULL));
    copy_file(img, bad);
    poke(bad, 0x6400 + 4, 0);           /* Disc 0 loses its used flag: an unused record must be all zero */
    CHECK(!fw_walk(bad, NULL, NULL));
    copy_file(img, bad);
    poke(bad, 0x2d0a00 + 0x92c, 1);     /* PSTCFG2 CRC */
    CHECK(!fw_walk(bad, NULL, NULL));
    copy_file(img, bad);
    poke(bad, 0x0, 'X');                /* header A: B (identical) takes over, still readable */
    CHECK(fw_walk(bad, NULL, NULL));
    poke(bad, 0xa00, 'X');              /* and with B gone too, nothing is left */
    CHECK(!fw_walk(bad, NULL, NULL));
    copy_file(img, bad);
    {
        MhV2DiscRec dr;
        uint8_t *rec = pread_file(img, 0x6400, 0x100);
        bool ok;
        mh_v2_disc_decode(rec, &dr, &ok);
        CHECK(ok);
        free(rec);
        poke(bad, (dr.track_lba << 9) + 0x930, 1); /* track map padding must be zero */
        CHECK(!fw_walk(bad, NULL, NULL));
    }
    mh_project_clear(p);
    free(p);
}

/* The same walk on real games: audio tracks in a second BIN and a two disc Case. Skipped when the
 * fixture games are absent. */
static void test_firmware_reads_real_games(void)
{
    static const char *const g[] = {FIXTURES "/Breath of Fire III (USA).cue",
                                    FIXTURES "/Chrono Cross (USA, Canada) (Disc 1).cue",
                                    FIXTURES "/Chrono Cross (USA, Canada) (Disc 2).cue"};
    MhProject *p;
    MhV2Result res;
    char *img = tpath("fw_real.img");
    unsigned nc = 0, nd = 0, i;

    for (i = 0; i < 3; i++)
        if (access(g[i], R_OK) != 0) {
            printf("real games absent, skipping the real game walk\n");
            return;
        }
    p = new_project();
    project_add(p, g[0], 256);
    project_add(p, g[1], 256);
    project_add(p, g[2], 1); /* joins the Case of Disc 1 */
    reset_progress();
    CHECK(mh_v2_write_image_direct(&g_w, p, img, &res));
    CHECK(res.ok);
    /* Names longer than 31 characters (Chrono Cross (USA, Canada) (Disc 1) is one) use the long names
     * extension: the original firmware refuses such a card, firmware 0.2.0 reads it. */
    {
        bool longn = false;
        unsigned c, k;
        for (c = 0; c < 256; c++)
            if (p->cases[c].in_use) {
                if (strlen(p->cases[c].title) > 31) longn = true;
                for (k = 0; k < p->cases[c].disc_count; k++)
                    if (strlen(p->cases[c].discs[k]->name) > 31) longn = true;
            }
        CHECK(fw_walk(img, &nc, &nd) == !longn);
        if (!longn) CHECK(nc == 2 && nd == 3);
        CHECK(fw2_walk(img, NULL, &nc, &nd));
        CHECK(nc == 2 && nd == 3);
        printf("real games: %u cases, %u discs accepted by the firmware 0.2.0 oracle (long names: %s)\n", nc, nd, longn ? "yes" : "no");
    }
    mh_project_clear(p);
    free(p);
}

/* Firmware rule of FUN_10022084: INDEX 01 of a track must lie before the end of the track, that
 * is before the INDEX 00 of the next one. The writer used to emit such a map without noticing. */
static void test_trackmap_index_inside_track(void)
{
    MhDisc *d = calloc(1, sizeof *d);
    uint8_t rec[MH_V2_DISC_SIZE] = {0}, *out = malloc(MH_V2_PKG_META_BYTES);
    char err[256];
    assert(d && out);
    d->ntracks = 3;
    d->total_sectors = 9000;
    d->tracks[0] = (MhTrack){.number = 1, .kind = 2, .mode = 1, .start = 0};
    /* track 2: INDEX 01 at 5000; track 3 pregap starts at 4950, before it */
    d->tracks[1] = (MhTrack){.number = 2, .kind = 1, .pregap = 100, .start = 5000};
    d->tracks[2] = (MhTrack){.number = 3, .kind = 1, .pregap = 100, .start = 5050};
    CHECK(!mh_v2_build_package_metadata(d, rec, out, err, sizeof err));
    /* the same disc without the overlap is fine and the firmware accepts the map */
    d->tracks[2].start = 5200;
    CHECK(mh_v2_build_package_metadata(d, rec, out, err, sizeof err));
    {
        FwDisc fd = {0};
        char *img = tpath("fw_trk.img");
        FILE *f = fopen(img, "wb");
        Fw w;
        fd.tracks = 3; fd.sectors = 9000; fd.track_lba = 0;
        fwrite(out, 1, 0xa00, f);
        fclose(f);
        w.f = fopen(img, "rb");
        w.size = 0xa00;
        CHECK(fw_trackmap(&w, &fd));
        fclose(w.f);
    }
    free(out);
    free(d);
}


/* ---- tests of the long names ---------------------------------------------------------------- */

static void name_of(char *out, size_t len, char tag)
{
    static const char alpha[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789 ()-',.&";
    size_t i;
    for (i = 0; i < len; i++) out[i] = alpha[(i * 7 + (size_t)tag) % (sizeof alpha - 1)];
    if (len && out[len - 1] == ' ') out[len - 1] = 'Z';
    if (len && out[0] == ' ') out[0] = tag;
    out[len] = 0;
}

static void set_names(MhCase *cs, const char *title, const char *d1, const char *d2)
{
    mh_str_copy(cs->title, sizeof cs->title, title);
    mh_str_copy(cs->discs[0]->name, sizeof cs->discs[0]->name, d1);
    cs->discs[0]->flags |= MH_DF_USER_NAMED;
    if (d2) {
        mh_str_copy(cs->discs[1]->name, sizeof cs->discs[1]->name, d2);
        cs->discs[1]->flags |= MH_DF_USER_NAMED;
    }
}

/* the Case/Disc records of a written image, for byte checks */
static uint8_t *case_rec(const char *img, unsigned idx) { return pread_file(img, 0x1400 + (uint64_t)idx * 0x50, 0x50); }
static uint8_t *disc_rec(const char *img, unsigned id) { return pread_file(img, 0x6400 + (uint64_t)id * 0x100, 0x100); }

static void test_long_names_writer(void)
{
    static const unsigned lens[] = {31, 32, 40, 71, 72};
    MhProject *p = new_project(), *q = new_project();
    char cue[MH_PATH_MAX], *img = tpath("ln_multi.img"), *img2 = tpath("ln_round.img"), cache[MH_PATH_MAX];
    char nm[5][96], want[96], cname[80];
    MhV2Result res, vres;
    Fw2Row rows[8];
    unsigned i, nc = 0, nd = 0;
    uint8_t *r;

    /* five single disc Cases with names of 31, 32, 40, 71 and 72 characters (parentheses and a
     * region tag included), the last one over the limit and cut at 71 */
    for (i = 0; i < 5; i++) {
        char n[16];
        snprintf(n, sizeof n, "ln%u", i);
        make_synth(n, 300 + 10 * i, 0, false, 40 + i, cue);
        project_add(p, cue, 256);
        name_of(nm[i], lens[i], (char)('A' + i));
        memcpy(nm[i] + lens[i] - 14, " (USA) (Rev 1)", 14); /* parentheses and a region tag at the end */
        set_names(&p->cases[i], nm[i], nm[i], NULL);
    }
    /* a two disc Case: Case name 71, Disc titles 61 and 62 (the second is cut at 61) */
    make_synth("ln_m1", 300, 0, false, 50, cue);
    project_add(p, cue, 256);
    make_synth("ln_m2", 310, 0, false, 51, cue);
    project_add(p, cue, 5);
    name_of(cname, 71, 'M');
    {
        char t1[96], t2[96];
        name_of(t1, 61, 'P');
        name_of(t2, 62, 'Q');
        set_names(&p->cases[5], cname, t1, t2);
        reset_progress();
        CHECK(mh_v2_write_image_direct(&g_w, p, img, &res) && res.ok);
        CHECK(fw2_walk(img, rows, &nc, &nd) && nc == 6 && nd == 7);
        CHECK_STR(rows[5].title[0], t1);
        CHECK(strlen(rows[5].title[1]) == 61 && strncmp(rows[5].title[1], t2, 61) == 0);
        CHECK(strlen(rows[5].title[1]) + strlen(" - DISCO 2") <= 71);
    }
    CHECK_STR(rows[5].name, cname);
    for (i = 0; i < 5; i++) {
        unsigned want_len = lens[i] > 71 ? 71 : lens[i];
        strncpy(want, nm[i], 71);
        want[71] = 0;
        CHECK(strlen(rows[i].name) == want_len && strcmp(rows[i].name, want) == 0);
        CHECK(strcmp(rows[i].title[0], want) == 0);
        /* bytes: no extension for 31 characters or less, flag and LNX1 otherwise */
        r = case_rec(img, i);
        CHECK(r[0x3f] == (lens[i] > 31 ? 1 : 0) && all_zero(r + 0x40, 0xc));
        free(r);
        r = disc_rec(img, i);
        if (lens[i] <= 31) {
            CHECK(all_zero(r + 0x94, 0x68));
        } else {
            CHECK(memcmp(r + 0x94, "LNX1", 4) == 0 && r[0x98] == want_len - 31 && r[0x99] == want_len - 31);
            CHECK(memcmp(r + 0x9a, want + 31, want_len - 31) == 0 && memcmp(r + 0xc2, want + 31, want_len - 31) == 0);
            CHECK(r[0x1c + 31] == 0 && memcmp(r + 0x1c, want, 31) == 0);
        }
        free(r);
    }
    /* the second disc of a Case carries no case_ext */
    r = disc_rec(img, 6);
    CHECK(memcmp(r + 0x94, "LNX1", 4) == 0 && r[0x98] == 30 && r[0x99] == 0 && all_zero(r + 0xc2, 40 + 18));
    free(r);
    r = disc_rec(img, 5);
    CHECK(r[0x98] == 30 && r[0x99] == 40);
    free(r);
    /* the original firmware refuses this card: that is the compatibility warning of the spec */
    CHECK(!fw_walk(img, NULL, NULL));

    /* load, write back, load again: the names survive and the card is valid for 0.2.0 */
    snprintf(cache, sizeof cache, "%s/cache_ln", g_tmp);
    mh_ensure_dir(cache);
    memset(&vres, 0, sizeof vres);
    CHECK(mh_v2_load_image(img, cache, &g_env, q, &vres));
    mh_project_recount(q);
    CHECK(q->case_count == 6 && q->disc_count == 7);
    for (i = 0; i < 5; i++) {
        strncpy(want, nm[i], 71);
        want[71] = 0;
        CHECK_STR(q->cases[i].title, want);
        CHECK_STR(q->cases[i].discs[0]->name, want);
        CHECK_STR(q->cases[i].discs[0]->title, want);
    }
    CHECK_STR(q->cases[5].title, cname);
    CHECK(strlen(q->cases[5].discs[1]->name) == 61);
    reset_progress();
    CHECK(mh_v2_write_image_direct(&g_w, q, img2, &res) && res.ok);
    CHECK(fw2_walk(img2, rows, &nc, &nd) && nc == 6 && nd == 7);
    for (i = 0; i < 6; i++) {
        uint8_t *a = case_rec(img, i), *b = case_rec(img2, i);
        CHECK(memcmp(a, b, 0x50) == 0);
        free(a);
        free(b);
    }
    for (i = 0; i < 7; i++) {
        uint8_t *a = disc_rec(img, i), *b = disc_rec(img2, i);
        CHECK(memcmp(a, b, 0x1c) == 0 && memcmp(a + 0x1c, b + 0x1c, 0x20) == 0 && memcmp(a + 0x94, b + 0x94, 0x68) == 0);
        free(a);
        free(b);
    }
    mh_project_clear(q);
    free(q);
    mh_project_clear(p);
    free(p);
}

/* A project whose names all fit 31 characters writes exactly what the original writer did: no
 * extension anywhere, and both oracles accept the card. */
static void test_legacy_names_unchanged(void)
{
    MhProject *p = new_project();
    char cue[MH_PATH_MAX], *img = tpath("ln_legacy.img");
    MhV2Result res;
    Fw2Row rows[4];
    unsigned i, nc = 0, nd = 0;
    uint8_t *tbl;

    make_synth("lg_a", 600, 400, false, 60, cue);
    project_add(p, cue, 256);
    make_synth("lg_b", 700, 300, false, 61, cue);
    project_add(p, cue, 0);
    make_synth("lg_c", 500, 0, false, 62, cue);
    project_add(p, cue, 256);
    set_names(&p->cases[0], "FINAL FANTASY VII (USA) (REV1)", "FINAL FANTASY VII (DISC 1)", "FINAL FANTASY VII (DISC 2)");
    mh_str_copy(p->cases[1].title, sizeof p->cases[1].title, "X");
    mh_str_copy(p->cases[1].discs[0]->name, sizeof p->cases[1].discs[0]->name, "1234567890123456789012345678901"); /* 31 */
    reset_progress();
    CHECK(mh_v2_write_image_direct(&g_w, p, img, &res) && res.ok);
    CHECK(fw_walk(img, &nc, &nd) && nc == 2 && nd == 3);
    CHECK(fw2_walk(img, rows, &nc, &nd) && nc == 2 && nd == 3);
    CHECK_STR(rows[0].name, "FINAL FANTASY VII (USA) (REV1)");
    CHECK_STR(rows[1].title[0], "1234567890123456789012345678901");
    tbl = pread_file(img, 0x1400, 0x5000);
    for (i = 0; i < 256; i++) CHECK(tbl[i * 0x50 + 0x3f] == 0 && all_zero(tbl + i * 0x50 + 0x3f, 0xd));
    free(tbl);
    tbl = pread_file(img, 0x6400, 0x10000);
    for (i = 0; i < 256; i++) CHECK(all_zero(tbl + i * 0x100 + 0x94, 0x68));
    free(tbl);
    mh_project_clear(p);
    free(p);
}

/* Every violation of the 2.1 rules on a real card is refused by the 0.2.0 oracle. The card has one
 * Case of two Discs: slot 0 with title_ext 40 and case_ext 40, slot 1 with title_ext 30 and no case_ext. */
static void test_long_names_violations(void)
{
    MhProject *p = new_project();
    char cue[MH_PATH_MAX], *img = tpath("ln_v.img"), *bad = tpath("ln_vbad.img");
    MhV2Result res, vres;
    char n71[96];
    unsigned k;
    /* rec: 0 = Case 0, 1 = Disc 0 (slot 0), 2 = Disc 1 (slot 1) */
    static const struct { unsigned rec, off; uint8_t val; } cases[] = {
        {0, 0x3f, 2}, {0, 0x3f, 0xff}, {0, 0x40, 1}, {0, 0x4b, 1}, {0, 8 + 3, 0}, {0, 8 + 31, 'A'}, {0, 8, 0x1f},
        {1, 0x98, 41}, {1, 0x99, 41}, {1, 0x9a, 0x1f}, {1, 0x9a, 0x7f}, {1, 0xc2, 0x80}, {1, 0xea, 1}, {1, 0xfb, 1},
        {1, 0x1c + 4, 0}, {1, 0x1c + 31, 'A'}, {1, 0x94, 'X'}, {1, 0x97, 0},
        {2, 0x9a + 39, 'z'}, {2, 0xc2, 'z'}, {2, 0x99, 3}, {2, 0x98, 0},
    };

    make_synth("lv_a", 300, 0, false, 70, cue);
    project_add(p, cue, 256);
    make_synth("lv_b", 310, 0, false, 71, cue);
    project_add(p, cue, 0);
    name_of(n71, 71, 'V');
    set_names(&p->cases[0], n71, n71, n71);
    reset_progress();
    CHECK(mh_v2_write_image_direct(&g_w, p, img, &res) && res.ok);
    CHECK(fw2_walk(img, NULL, NULL, NULL));
    CHECK(mh_v2_validate_image(img, NULL, &vres) && vres.ok);
    for (k = 0; k < sizeof cases / sizeof *cases; k++) {
        uint64_t at = cases[k].rec == 0 ? 0x1400 : 0x6400 + (uint64_t)(cases[k].rec - 1) * 0x100;
        size_t rec = cases[k].rec == 0 ? 0x50 : 0x100;
        uint8_t *blob;
        FILE *f;
        copy_file(img, bad);
        blob = pread_file(bad, at, rec);
        blob[cases[k].off] = cases[k].val;
        if (cases[k].rec == 2 && cases[k].off == 0x99) memcpy(blob + 0xc2, "abc", 3); /* case_ext in slot 1 */
        if (cases[k].rec == 2 && cases[k].off == 0x98) memset(blob + 0x9a, 0, 40);     /* length 0 is fine ... */
        mh_v2_wr32(blob + rec - 4, mh_crc_record(blob, rec));
        f = fopen(bad, "r+b");
        CHECK(f && fseeko(f, (off_t)at, SEEK_SET) == 0 && fwrite(blob, 1, rec, f) == rec);
        if (f) fclose(f);
        free(blob);
        /* ... so the last entry is the one case that stays valid: title_ext_len 0 in slot 1 */
        if (cases[k].rec == 2 && cases[k].off == 0x98) CHECK(fw2_walk(bad, NULL, NULL, NULL));
        else CHECK(!fw2_walk(bad, NULL, NULL, NULL));
    }
    mh_project_clear(p);
    free(p);
}

int main(void)
{
    const char *t = getenv("TMPDIR");
    uint8_t *blob;
    uint64_t blen;

    snprintf(g_tmp, sizeof g_tmp, "%s/mhv2fw-XXXXXX", t && t[0] ? t : "/tmp");
    if (!mkdtemp(g_tmp)) { perror("mkdtemp"); return 2; }

    test_generation_rule();
    test_trackmap_index_inside_track();

    if (access(EMBEDDED "/picostation_menu.bin", R_OK) != 0) {
        printf("embedded resources absent, skipping the image tests\n");
        goto done;
    }
    CHECK(mh_db_load_dir(&g_db, EMBEDDED, FIXTURES));
    blob = slurp(EMBEDDED "/cdx_cheats.bin", &blen);
    CHECK(blob && mh_cheat_db_load(&g_cdb, blob, blen));
    g_menu = slurp(EMBEDDED "/picostation_menu.bin", &g_menu_size);
    g_music = slurp(EMBEDDED "/umbrella_music.vag", &g_music_size);
    CHECK(g_menu && g_music);
    g_env.menu = g_menu;
    g_env.menu_size = g_menu_size;
    g_env.db = &g_db;
    g_env.cheats = &g_cdb;
    g_w.env = &g_env;
    g_w.music = g_music;
    g_w.music_size = g_music_size;
    g_w.prog = &g_prog;
    g_w.stamp = fixed_stamp;
    reset_progress();
    test_firmware_reads_writer_output();
    test_firmware_reads_real_games();
    test_legacy_names_unchanged();
    test_long_names_writer();
    test_long_names_violations();

done:
    rm_rf(g_tmp);
    printf("test_v2_firmware: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
