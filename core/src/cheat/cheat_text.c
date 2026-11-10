/* Cheat code text: line parser, MHDialect handling, classic GameShark conversion, import and
 * export of TXT/CHT files. orig: parse_code_line, cheat_parse_codes_w, classic_expand_slide,
 * cheat_codes_to_text, cheat_profile_import_text_dialect, import_flush, cheat_profile_export_text.
 *
 * The original worked on UTF-16 (WCHAR). The importer keeps that representation (`chw`) so the
 * observable limits (511 character lines, 127 character names) are the same. */
#include <stdio.h>

#include "cheat_internal.h"

#define DIALECT_V5 "MHDialect=V5ORIG_BANK_V01"
#define DIALECT_CLASSIC "MHDialect=GAMESHARK_CLASSIC_V1"

static void set_err(char *err, size_t errn, const char *msg) { mh_err_set(err, errn, "%s", msg); }

/* ---- UTF-16 helpers ---------------------------------------------------------------------- */
static size_t wlen(const chw *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static chw wlower(chw c) { return (c >= 'A' && c <= 'Z') ? (chw)(c + 32) : c; }

/* Equal ignoring ASCII case (orig: w_icmp == 0). */
static int weq_i(const chw *a, const char *b)
{
    size_t i = 0;
    for (; a[i] && b[i]; i++)
        if (wlower(a[i]) != wlower((unsigned char)b[i])) return 0;
    return a[i] == 0 && b[i] == 0;
}

static int wstarts_i(const chw *a, const char *prefix)
{
    for (size_t i = 0; prefix[i]; i++)
        if (!a[i] || wlower(a[i]) != wlower((unsigned char)prefix[i])) return 0;
    return 1;
}

/* orig: w_copy: at most n-1 units, always terminated. */
static void wcopy(chw *dst, size_t n, const chw *src)
{
    size_t i = 0;
    if (n == 0) return;
    for (; src && src[i] && i + 1 < n; i++) dst[i] = src[i];
    dst[i] = 0;
}

/* orig: trim_line on UTF-16. */
static void wtrim(chw *s)
{
    size_t skip = 0, len;
    while (s[skip] == ' ' || s[skip] == '\t') skip++;
    len = wlen(s + skip);
    if (skip) memmove(s, s + skip, (len + 1) * sizeof(chw));
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n'))
        s[--len] = 0;
}

static int is_hex(chw c) { return (c >= '0' && c <= '9') || ((c & 0xffdf) - 0x41u) < 6; }

/* The token test of the importer: characters 0x30..0x66 except the punctuation and letters
 * that are not hex. Note that it also accepts '?' (0x3f), as the original. */
static int hexlike(chw c)
{
    static const uint64_t nonhex = 0xff81ffffff817c00ull;
    unsigned k = (unsigned)c - 0x30u;
    return k <= 0x36 && !((nonhex >> k) & 1);
}

/* UTF-8 to UTF-16 (invalid bytes become U+FFFD). Returns a NUL terminated array. */
static chw *utf8_to_w(const char *s, size_t *out_len)
{
    size_t cap = strlen(s) + 1, n = 0;
    chw *w = mh_alloc(cap * sizeof(chw));
    uint32_t cp;
    if (!w) return NULL;
    while ((cp = mh_utf8_next(&s)) != 0) {
        if (cp >= 0x10000) {
            cp -= 0x10000;
            w[n++] = (chw)(0xD800 + (cp >> 10));
            w[n++] = (chw)(0xDC00 + (cp & 0x3FF));
        } else {
            w[n++] = (chw)cp;
        }
    }
    w[n] = 0;
    if (out_len) *out_len = n;
    return w;
}

/* UTF-16 to UTF-8 into a bounded buffer. */
static void w_to_utf8(const chw *w, char *dst, size_t n)
{
    size_t o = 0;
    if (n == 0) return;
    for (; *w; w++) {
        uint32_t cp = *w;
        char tmp[4];
        size_t k;
        if (cp >= 0xD800 && cp < 0xDC00 && w[1] >= 0xDC00 && w[1] < 0xE000) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (w[1] - 0xDC00u);
            w++;
        }
        if (cp < 0x80) { tmp[0] = (char)cp; k = 1; }
        else if (cp < 0x800) { tmp[0] = (char)(0xC0 | (cp >> 6)); tmp[1] = (char)(0x80 | (cp & 0x3F)); k = 2; }
        else if (cp < 0x10000) {
            tmp[0] = (char)(0xE0 | (cp >> 12)); tmp[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
            tmp[2] = (char)(0x80 | (cp & 0x3F)); k = 3;
        } else {
            tmp[0] = (char)(0xF0 | (cp >> 18)); tmp[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
            tmp[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); tmp[3] = (char)(0x80 | (cp & 0x3F)); k = 4;
        }
        if (o + k + 1 > n) break;
        memcpy(dst + o, tmp, k);
        o += k;
    }
    dst[o] = 0;
}

/* ---- parse_code_line ------------------------------------------------------------------------ */

/* orig: parse_code_line @0x140025300 */
static int parse_line_w(const chw *s, size_t n, uint8_t out[6])
{
    static const uint64_t sep = 0x400200100000200ull; /* tab space '-' ':' */
    int nib[12], cnt = 0;
    size_t i = 0;
    if (n == 0) return 0;
    do {
        chw c = s[i];
        int v = -1;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        if (v >= 0) {
            if (cnt > 11) return 0;
            nib[cnt++] = v;
        } else if (c > 0x3a) {
            if (c == 0x3b) break;     /* ';' starts a comment */
            if (c != 0x5c) return 0;  /* '\\': only blanks may follow */
            for (i++; i < n; i++)
                if (s[i] != ' ' && s[i] != '\t') return 0;
            break;
        } else if (!((sep >> c) & 1)) {
            if (c != '#') {
                if (c != '/') return 0;
                if (n <= i + 1 || s[i + 1] != '/') return 0;
            }
            break;
        }
        i++;
    } while (i != n);
    if (cnt != 12) return 0;
    for (int k = 0; k < 6; k++) out[k] = (uint8_t)((nib[k * 2] << 4) | nib[k * 2 + 1]);
    return 1;
}

int mh_cheat_parse_code_line(const char *text, size_t len, uint8_t out[6])
{
    size_t wl;
    chw *w;
    int r;
    char *cut;
    if (!text) return 0;
    cut = mh_alloc(len + 1);
    if (!cut) return 0;
    memcpy(cut, text, len);
    cut[len] = 0;
    w = utf8_to_w(cut, &wl);
    mh_free(cut);
    if (!w) return 0;
    r = parse_line_w(w, wl, out);
    mh_free(w);
    return r;
}

/* ---- classic GameShark conversion -------------------------------------------------------------- */
static inline uint32_t addr24(const uint8_t *l) { return ((uint32_t)l[1] << 16) | ((uint32_t)l[2] << 8) | l[3]; }
static inline int ram_ok(uint32_t a) { return a - 0x10000u < 0x1f0000u; }
static inline int bad_write16(uint8_t op, uint32_t a) { return op == 0x80 && ((a & 1) || a == 0x1fffff); }

#define E_255 "A conversao do GameShark classico excede 255 linhas V5."

/* orig: classic_expand_slide.constprop.0 @0x140010910. `cond` is the D0/D1 line already turned
 * into the V5 prefix (op 70/90) when has_cond. Returns the error text or NULL; *consumed is the
 * number of source lines used. */
static const char *expand_slide(const uint8_t *src, unsigned n, unsigned idx, uint8_t *dst, unsigned *out_n,
                                const uint8_t cond[6], int has_cond, unsigned *consumed)
{
    const uint8_t *l50, *fol;
    unsigned count, step, k = 0;
    uint16_t inc, val;
    uint8_t fop;
    uint32_t cur, cur24;
    uint64_t next;
    if (!(idx + 1 < n)) return "Codigo 50 incompleto: falta a linha seguidora 30/80.";
    l50 = src + (size_t)idx * 6;
    fol = src + (size_t)(idx + 1) * 6;
    if (l50[0] != 0x50 || l50[1] != 0) return "Formato 50 classico invalido. Use 5000NNSS VVVV.";
    count = l50[2];
    step = l50[3];
    inc = (uint16_t)((l50[4] << 8) | l50[5]);
    if (count == 0) return "Codigo 50 com contagem zero nao e importado.";
    fop = fol[0];
    if (fop != 0x80 && fop != 0x30) return "GAMESHARK_CLASSIC_V1: o seguidor do 50 precisa ser 30 ou 80.";
    cur = cur24 = addr24(fol);
    val = (uint16_t)((fol[4] << 8) | fol[5]);
    next = cur;
    if (ram_ok(cur)) {
        do {
            uint8_t *o;
            next += step;
            if (bad_write16(fop, cur)) break;
            if (has_cond) {
                uint32_t a = ((uint32_t)cond[1] << 16) | ((uint32_t)cond[2] << 8) | cond[3];
                if (a <= (uint32_t)(fop == 0x80) + cur && cur <= a + 1)
                    return "Condicao D0/D1 + slide 50 altera o proprio endereco testado; essa sequencia nao "
                           "possui conversao exata para o V5.";
                if (*out_n > 0xfe) return E_255;
                memcpy(dst + (size_t)*out_n * 6, cond, 6);
                (*out_n)++;
            }
            if (*out_n > 0xfe) return E_255;
            k++;
            o = dst + (size_t)*out_n * 6;
            o[0] = fop;
            o[1] = (uint8_t)(cur >> 16);
            o[2] = (uint8_t)(cur >> 8);
            o[3] = (uint8_t)cur;
            o[4] = (uint8_t)(val >> 8);
            o[5] = (uint8_t)val;
            (*out_n)++;
            if (count == k) {
                *consumed = 2;
                return NULL;
            }
            if (next > 0xffffff) return "Codigo 50 ultrapassa o espaco de endereco do PS1.";
            cur = (uint32_t)next;
            cur24 = cur & 0xffffff;
            val = (uint16_t)(val + inc);
        } while (ram_ok(cur24));
    }
    return "Codigo 50 gera endereco fora da RAM segura do V5 ou WRITE16 desalinhado.";
}

/* Converts classic GameShark lines (30, 80, D0, D1, 50) to V5 lines. dst holds 255 lines. */
static const char *classic_to_v5(const uint8_t *src, unsigned n, uint8_t *dst, unsigned *out_n, char *msgbuf,
                                 size_t msgn)
{
    unsigned idx = 0, consumed = 0;
    const char *e;
    *out_n = 0;
    while (idx < n) {
        const uint8_t *l = src + (size_t)idx * 6;
        uint8_t op = l[0];
        uint32_t a = addr24(l);
        if (op == 0x30 || op == 0x80) {
            if (!ram_ok(a) || bad_write16(op, a))
                return "Escrita 30/80 usa endereco fora da RAM segura do V5 ou WRITE16 desalinhado.";
            if (*out_n > 0xfe) return E_255;
            memcpy(dst + (size_t)*out_n * 6, l, 6);
            (*out_n)++;
            idx++;
        } else if (op == 0x50) {
            e = expand_slide(src, n, idx, dst, out_n, NULL, 0, &consumed);
            if (e) return e;
            idx += consumed;
        } else if (op != 0xd0 && op != 0xd1) {
            snprintf(msgbuf, msgn,
                     "Opcode classico 0x%02X nao tem conversao exata comprovada para o GameShark V5 original. "
                     "Use apenas 30, 80, D0, D1 e 50+30/80.",
                     op);
            return msgbuf;
        } else {
            const uint8_t *nl;
            uint8_t nop, prefix = op == 0xd0 ? 0x70 : 0x90;
            if (a - 0x10000u > 0x1efffe || (a & 1)) return "Condicao D0/D1 usa endereco invalido ou desalinhado para o V5.";
            if (idx + 1 >= n) return "Condicao D0/D1 sem acao seguinte.";
            nl = src + (size_t)(idx + 1) * 6;
            nop = nl[0];
            if (nop == 0xd0 || nop == 0xd1 || (nop >= 0xe0 && nop <= 0xe3))
                return "Cadeias de duas ou mais condicoes classicas nao possuem conversao exata para o V5 original.";
            if (nop == 0x30 || nop == 0x80) {
                uint32_t na = addr24(nl);
                if (!ram_ok(na) || bad_write16(nop, na))
                    return "A escrita controlada por D0/D1 usa endereco fora da RAM segura do V5 ou WRITE16 "
                           "desalinhado.";
                if (*out_n >= 0xff || *out_n + 1 == 0xff) return E_255;
                dst[(size_t)*out_n * 6] = prefix;
                memcpy(dst + (size_t)*out_n * 6 + 1, l + 1, 5);
                memcpy(dst + (size_t)(*out_n + 1) * 6, nl, 6);
                *out_n += 2;
                idx += 2;
            } else if (nop != 0x50) {
                return "D0/D1 classico precisa controlar uma escrita 30/80 ou um slide 50 + 30/80.";
            } else {
                uint8_t cond[6];
                memcpy(cond, l, 6);
                cond[0] = prefix;
                e = expand_slide(src, n, idx + 1, dst, out_n, cond, 1, &consumed);
                if (e) return e;
                idx += 1 + consumed;
            }
        }
    }
    if (*out_n == 0 || *out_n > 0xff || !ch_lines_structure_ok(dst, *out_n))
        return "A conversao classica nao produziu um programa V5 valido.";
    return NULL;
}

/* ---- parse_codes ------------------------------------------------------------------------------- */

/* orig: cheat_parse_codes_w @0x140008ff0 */
static int parse_codes_w(const chw *text, uint8_t **out, uint16_t *nout, char *err, size_t errn)
{
    uint8_t *lines = NULL;
    unsigned count = 0;
    int dialect = 0;
    size_t pos = 0;
    const char *msg = NULL;
    char msgbuf[256];
    if (out) *out = NULL;
    if (nout) *nout = 0;
    if (err && errn) err[0] = 0;
    if (!text) {
        set_err(err, errn, "Informe os codigos GameShark.");
        return 0;
    }
    lines = mh_alloc(0x5fa);
    if (!lines) {
        set_err(err, errn, "Memoria insuficiente.");
        return 0;
    }
    while (text[pos]) {
        size_t s = pos, e = pos, len;
        while (text[e] && text[e] != '\n' && text[e] != '\r') e++;
        pos = e;
        while (text[pos] == '\r' || text[pos] == '\n') pos++;
        while (e > s && (text[e - 1] == ' ' || text[e - 1] == '\t')) e--;
        while (s < e && (text[s] == ' ' || text[s] == '\t')) s++;
        len = e - s;
        if (len == 0 || text[s] == '#' || text[s] == ';') continue;
        if (len < 0x40) {
            chw buf[0x40];
            int d = 0;
            memcpy(buf, text + s, len * sizeof(chw));
            buf[len] = 0;
            if (weq_i(buf, DIALECT_V5)) d = 1;
            else if (weq_i(buf, DIALECT_CLASSIC)) d = 2;
            else if (wstarts_i(buf, "MHDialect=")) {
                msg = "MHDialect aceitos: V5ORIG_BANK_V01 ou GAMESHARK_CLASSIC_V1.";
                goto fail;
            }
            if (d) {
                if (dialect) {
                    msg = "Informe MHDialect apenas uma vez, antes dos codigos.";
                    goto fail;
                }
                dialect = d;
                continue;
            }
        }
        if (!dialect) {
            msg = "A primeira linha deve ser MHDialect=V5ORIG_BANK_V01 ou MHDialect=GAMESHARK_CLASSIC_V1.";
            goto fail;
        }
        if (count > 0xfe || !parse_line_w(text + s, len, lines + (size_t)count * 6)) {
            msg = "Linha de cheat invalida. Use XXXXXXXX XXXX.";
            goto fail;
        }
        count++;
    }
    if (!dialect) {
        msg = "A primeira linha deve informar MHDialect.";
        goto fail;
    }
    if (dialect == 1) {
        if (count == 0 || !ch_lines_structure_ok(lines, count)) {
            msg = "V5ORIG_BANK_V01 aceita somente os tipos nativos 30/31/70/80/90 do runtime auditado.";
            goto fail;
        }
        *out = lines;
        *nout = (uint16_t)count;
        return 1;
    }
    if (count == 0) {
        msg = "Nenhuma linha GameShark classica foi informada.";
        goto fail;
    }
    {
        uint8_t *conv = mh_alloc(0x5fa);
        unsigned cn = 0;
        if (!conv) {
            msg = "Memoria insuficiente para converter GameShark classico.";
            goto fail;
        }
        msg = classic_to_v5(lines, count, conv, &cn, msgbuf, sizeof msgbuf);
        if (msg) {
            mh_free(conv);
            goto fail;
        }
        mh_free(lines);
        *out = conv;
        *nout = (uint16_t)cn;
        return 1;
    }
fail:
    set_err(err, errn, msg);
    mh_free(lines);
    return 0;
}

int mh_cheat_parse_codes(const char *text, uint8_t **lines, uint16_t *nlines, char *err, size_t errn)
{
    chw *w = NULL;
    int r;
    if (text) {
        w = utf8_to_w(text, NULL);
        if (!w) {
            if (lines) *lines = NULL;
            if (nlines) *nlines = 0;
            set_err(err, errn, "Memoria insuficiente.");
            return 0;
        }
    }
    r = parse_codes_w(w, lines, nlines, err, errn);
    mh_free(w);
    return r;
}

/* ---- text out ------------------------------------------------------------------------------------ */

/* orig: cheat_codes_to_text @0x140006110 */
int mh_cheat_codes_to_text(const uint8_t *lines, uint16_t nlines, MhWbuf *out)
{
    static const char hex[] = "0123456789ABCDEF";
    if (!lines || !out) return 0;
    for (unsigned i = 0; i < nlines; i++) {
        const uint8_t *l = lines + (size_t)i * 6;
        for (int k = 0; k < 6; k++) {
            if (k == 4 && !mh_wbuf_putc(out, ' ')) return 0;
            if (!mh_wbuf_putc(out, hex[l[k] >> 4]) || !mh_wbuf_putc(out, hex[l[k] & 15])) return 0;
        }
        if (!mh_wbuf_puts(out, "\r\n")) return 0;
    }
    return 1;
}

/* orig: cheat_profile_export_text @0x14000c7c0 */
int mh_cheat_profile_export_text(const MhCheatProfile *p, const char *path, char *err, size_t errn)
{
    MhWbuf b;
    uint8_t *file = NULL;
    size_t n = 0, o = 0;
    const char *s;
    int ok = 0;
    mh_wbuf_init(&b);
    mh_wbuf_puts(&b, DIALECT_V5 "\r\n");
    if (p)
        for (uint32_t i = 0; i < p->count; i++) {
            const MhCheatEntry *e = &p->entries[i];
            char name[MH_CHEAT_NAME_MAX * 2];
            mh_utf8_from_latin1(e->name, name, sizeof name);
            mh_wbuf_putc(&b, '[');
            mh_wbuf_puts(&b, name);
            mh_wbuf_puts(&b, "]\r\n");
            mh_cheat_codes_to_text(e->lines, e->nlines, &b);
            mh_wbuf_puts(&b, "\r\n");
        }
    s = b.data;
    if (s) {
        chw *w = utf8_to_w(s, &n);
        if (w) {
            file = mh_alloc(n * 2 + 2);
            if (file) {
                file[o++] = 0xff;
                file[o++] = 0xfe;
                for (size_t i = 0; i < n; i++) {
                    file[o++] = (uint8_t)w[i];
                    file[o++] = (uint8_t)(w[i] >> 8);
                }
                ok = path && mh_write_entire_file(path, file, o);
            }
            mh_free(file);
            mh_free(w);
        }
    }
    mh_wbuf_free(&b);
    if (!ok) set_err(err, errn, "Nao foi possivel exportar o perfil.");
    return ok;
}

/* ---- import --------------------------------------------------------------------------------------- */

typedef struct {
    MhCheatProfile *work;
    chw dialect[0x40];
    MhWbuf code; /* pending code lines as UTF-8 text, CRLF separated */
    int imported;
} Importer;

/* orig: import_flush @0x14001cdb0. Adds the pending block as a custom entry. */
static int import_flush(Importer *im, const chw *name, char *err, size_t errn)
{
    char name8[512], norm[MH_CHEAT_NAME_MAX], dialect8[0x40 * 4];
    MhWbuf text;
    uint8_t *lines = NULL;
    uint16_t nl = 0;
    char perr[256];
    MhCheatEntry e;
    int ok;
    if (im->code.len == 0) return 1;
    w_to_utf8(name, name8, sizeof name8);
    mh_normalize_title(name8, norm, MH_CHEAT_NAME_MAX);
    if (!norm[0]) {
        set_err(err, errn, "Um bloco de codigos nao possui nome.");
        return 0;
    }
    w_to_utf8(im->dialect, dialect8, sizeof dialect8);
    mh_wbuf_init(&text);
    mh_wbuf_puts(&text, dialect8);
    mh_wbuf_puts(&text, "\r\n");
    mh_wbuf_puts(&text, mh_wbuf_cstr(&im->code));
    ok = mh_cheat_parse_codes(mh_wbuf_cstr(&text), &lines, &nl, perr, sizeof perr);
    mh_wbuf_free(&text);
    if (!ok) {
        set_err(err, errn, perr);
        return 0;
    }
    memset(&e, 0, sizeof e);
    mh_str_copy(e.name, MH_CHEAT_NAME_MAX, norm);
    e.flags = 5;
    if (!weq_i(im->dialect, DIALECT_V5) && weq_i(im->dialect, DIALECT_CLASSIC)) e.flags = 0x45;
    e.game = 0;
    e.nlines = nl;
    e.variant = e.id = 0xFFFFFFFFu;
    e.lines = lines;
    ok = mh_cheat_profile_add_entry(im->work, &e);
    mh_free(lines);
    if (!ok) {
        set_err(err, errn, "A importacao excede os limites do perfil ou cria conflito entre codigos C0.");
        return 0;
    }
    im->imported++;
    mh_wbuf_free(&im->code);
    return 1;
}

/* Number of strict hex digits starting at s. */
static size_t hex_run(const chw *s)
{
    size_t n = 0;
    while (is_hex(s[n])) n++;
    return n;
}

static int is_name_sep(chw c) { return c == '-' || c == ':' || c == '='; }

/* A line that is not a code and not "NAME CODE": is it a bare title? Returns 1 when it looks
 * like a malformed code (hex run longer than 7), the original's "unrecognised" error. */
static int looks_like_code(const chw *line)
{
    size_t i;
    if (!((line[0] & 0xffdf) == 0 || line[0] == '\t')) {
        size_t cnt = 0;
        chw ch = line[0];
        int stopped = 0;
        do {
            if (!hexlike(ch)) { stopped = 1; break; }
            cnt++;
            ch = line[cnt];
        } while ((ch & 0xffdf) != 0 && ch != '\t');
        if (!stopped && cnt > 7) return 1;
        if (is_hex(line[0]) && hex_run(line) > 7) return 1;
    }
    for (i = 0; line[i] && line[i + 1]; i++) {
        chw ch = line[i + 1], prev = line[i];
        if (is_hex(ch) && (prev == ' ' || prev == '\t' || is_name_sep(prev)) && hex_run(line + i + 1) > 7) return 1;
    }
    return 0;
}

/* Reads the file into UTF-16. Returns NULL on any failure. */
static chw *read_text_file(const char *path)
{
    uint8_t *raw = NULL;
    uint64_t len = 0;
    chw *w = NULL;
    size_t skip = 0, wn = 0;
    if (!mh_read_entire_file(path, &raw, &len)) return NULL;
    if (len - 1 > 0x3fffff) goto fail; /* empty or over 4 MiB */
    if (len == 1 || raw[0] != 0xff) {
        if (len != 1 && len != 2 && raw[0] == 0xef && raw[1] == 0xbb && raw[2] == 0xbf) {
            skip = 3;
            if (len == 3) goto fail;
        }
    } else if (raw[1] == 0xfe) {
        uint64_t units;
        if ((len & 1) || (units = (len - 2) >> 1) == 0) goto fail;
        w = mh_alloc((size_t)(units + 1) * sizeof(chw));
        if (!w) goto fail;
        for (uint64_t i = 0; i < units; i++) {
            w[i] = (chw)(raw[2 + i * 2] | (raw[3 + i * 2] << 8));
            if (w[i] == 0) goto fail;
        }
        w[units] = 0;
        mh_free(raw);
        return w;
    }
    for (uint64_t i = skip; i < len; i++)
        if (raw[i] == 0) goto fail;
    {
        /* strict UTF-8, as MultiByteToWideChar with MB_ERR_INVALID_CHARS */
        const char *s = (const char *)raw + skip;
        const char *end = (const char *)raw + len;
        w = mh_alloc((size_t)(len - skip + 1) * sizeof(chw));
        if (!w) goto fail;
        while (s < end) {
            const char *before = s;
            uint32_t cp = mh_utf8_next(&s);
            if (cp == 0xFFFD && !(s - before == 3 && (uint8_t)before[0] == 0xEF && (uint8_t)before[1] == 0xBF &&
                                  (uint8_t)before[2] == 0xBD))
                goto fail;
            if (cp >= 0x10000) {
                cp -= 0x10000;
                w[wn++] = (chw)(0xD800 + (cp >> 10));
                w[wn++] = (chw)(0xDC00 + (cp & 0x3FF));
            } else {
                w[wn++] = (chw)cp;
            }
        }
        if (wn == 0) goto fail;
        w[wn] = 0;
    }
    mh_free(raw);
    return w;
fail:
    mh_free(raw);
    mh_free(w);
    return NULL;
}

/* orig: cheat_profile_import_text_dialect @0x14000ca70 */
int mh_cheat_profile_import_text_dialect(MhCheatProfile *p, const char *path, const char *dialect_arg,
                                         int *imported, char *err, size_t errn)
{
    Importer im;
    chw *text = NULL;
    chw line[0x200 + 1], name[0x80], codebuf[0x200 + 1];
    size_t pos = 0;
    int dialect_set, arg_pending, hdr_seen = 0, name_ctx = 0, ok = 0;
    const char *msg = NULL;
    char stem8[MH_PATH_MAX];

    if (imported) *imported = 0;
    if (err && errn) err[0] = 0;
    if (dialect_arg && !(mh_str_icmp(dialect_arg, DIALECT_V5) == 0 || mh_str_icmp(dialect_arg, DIALECT_CLASSIC) == 0)) {
        set_err(err, errn, "Selecione GameShark classico ou V5/Xploder para importar.");
        return 0;
    }
    if (!p || !path || !(text = read_text_file(path))) {
        set_err(err, errn, "Nao foi possivel ler o TXT/CHT.");
        return 0;
    }
    memset(&im, 0, sizeof im);
    mh_wbuf_init(&im.code);
    im.work = mh_cheat_profile_clone(p);
    if (!im.work) {
        mh_free(text);
        set_err(err, errn, "Memoria insuficiente para importar cheats.");
        return 0;
    }
    name[0] = 0;
    dialect_set = arg_pending = dialect_arg != NULL;
    if (dialect_arg) {
        chw *w = utf8_to_w(dialect_arg, NULL);
        if (w) {
            wcopy(im.dialect, 0x40, w);
            mh_free(w);
        }
    }

    for (;;) {
        size_t e = pos, len, next;
        int more;
        while (text[e] && text[e] != '\n' && text[e] != '\r') e++;
        len = e - pos;
        if (len > 0x1ff) {
            msg = "A importacao foi cancelada: existe uma linha maior que 511 caracteres.";
            goto fail;
        }
        memcpy(line, text + pos, len * sizeof(chw));
        line[len] = 0;
        next = e;
        while (text[next] == '\n' || text[next] == '\r') next++;
        more = text[next] != 0;
        pos = next;
        wtrim(line);

        if (wstarts_i(line, "#activation=") || wstarts_i(line, "#type=") || wstarts_i(line, "#group=")) {
            msg = "Metadados do DuckStation nao sao importados. Use [NOME] e codigos literais de hardware.";
            goto fail;
        }
        if (line[0] == 0 || line[0] == '#' || line[0] == ';' || (line[0] == '/' && line[1] == '/')) {
            if (more) continue;
            if (!dialect_set) {
                msg = "O arquivo nao informa MHDialect.";
                goto fail;
            }
            goto final;
        }
        if (wstarts_i(line, "MHDialect=")) {
            if (hdr_seen || im.code.len != 0 || (!arg_pending && dialect_set)) {
                msg = "MHDialect deve aparecer uma unica vez, antes do primeiro cheat.";
                goto fail;
            }
            if (!weq_i(line, DIALECT_V5) && !weq_i(line, DIALECT_CLASSIC)) {
                msg = "MHDialect aceitos: V5ORIG_BANK_V01 ou GAMESHARK_CLASSIC_V1.";
                goto fail;
            }
            if (arg_pending) {
                char l8[0x200 * 4];
                w_to_utf8(line, l8, sizeof l8);
                if (mh_str_icmp(l8, dialect_arg) != 0) {
                    msg = "A versao indicada no arquivo difere da escolhida na importacao.";
                    goto fail;
                }
            }
            wcopy(im.dialect, 0x40, line);
            if (more) {
                hdr_seen = 0;
                dialect_set = 1;
                arg_pending = 0;
                continue;
            }
            goto final;
        }
        if (!dialect_set) {
            msg = "O arquivo deve comecar com MHDialect=V5ORIG_BANK_V01 ou GAMESHARK_CLASSIC_V1.";
            goto fail;
        }
        if (line[0] == '[') {
            size_t l = wlen(line);
            if (l < 3 || line[l - 1] != ']') {
                msg = "Cabecalho [NOME] incompleto.";
                goto fail;
            }
            if (!import_flush(&im, name, err, errn)) goto fail_msg_set;
            line[l - 1] = 0;
            wcopy(name, 0x80, line + 1);
            wtrim(name);
            if (name[0] == 0) {
                msg = "Nome de cheat vazio.";
                goto fail;
            }
            hdr_seen = 1;
            name_ctx = 1;
        } else {
            uint8_t tmp[6];
            size_t l = wlen(line);
            if (parse_line_w(line, l, tmp)) {
                if (!name_ctx) {
                    chw *sw;
                    mh_path_stem(path, stem8, sizeof stem8);
                    sw = utf8_to_w(stem8, NULL);
                    name[0] = 0;
                    if (sw) {
                        wcopy(name, 0x80, sw);
                        mh_free(sw);
                    }
                    if (name[0] == 0) {
                        msg = "Memoria insuficiente durante a importacao.";
                        goto fail;
                    }
                }
                {
                    char l8[0x200 * 4];
                    w_to_utf8(line, l8, sizeof l8);
                    if (!mh_wbuf_puts(&im.code, l8) || !mh_wbuf_puts(&im.code, "\r\n")) {
                        msg = "Memoria insuficiente durante a importacao.";
                        goto fail;
                    }
                }
                name_ctx = 1;
            } else {
                /* "NAME CODE" on one line? */
                chw namebuf[0x80];
                const chw *codepart = NULL;
                int unrecognised = 1;
                if (l > 1)
                    for (size_t k = 1; k < l; k++) {
                        if (is_hex(line[k])) {
                            chw prev = line[k - 1];
                            if ((prev == ' ' || prev == '\t' || is_name_sep(prev)) && parse_line_w(line + k, l - k, tmp)) {
                                if (k < 0x80) {
                                    memcpy(namebuf, line, k * sizeof(chw));
                                    namebuf[k] = 0;
                                    codepart = line + k;
                                }
                                break;
                            }
                        }
                    }
                if (codepart) {
                    size_t nl;
                    for (;;) {
                        wtrim(namebuf);
                        nl = wlen(namebuf);
                        if (nl == 0) break;
                        if ((namebuf[nl - 1] & 0xffef) == 0x2d || namebuf[nl - 1] == ':') {
                            namebuf[nl - 1] = 0;
                            continue;
                        }
                        break;
                    }
                    if (nl != 0) {
                        int has_eq = 0, accept = 1;
                        for (size_t k = 0; namebuf[k]; k++)
                            if (namebuf[k] == '=') has_eq = 1;
                        if (!has_eq && (namebuf[0] & 0xffdf) != 0 && namebuf[0] != '\t') {
                            size_t cnt = 0;
                            chw ch = namebuf[0];
                            for (;;) {
                                if (!hexlike(ch)) break; /* not a code-like token: accept */
                                cnt++;
                                ch = namebuf[cnt];
                                if ((ch & 0xffdf) == 0 || ch == '\t') {
                                    if (cnt > 7) accept = 0;
                                    break;
                                }
                            }
                        }
                        if (has_eq) accept = 0;
                        if (accept) {
                            char l8[0x200 * 4];
                            unrecognised = 0;
                            wcopy(codebuf, 0x200 + 1, codepart);
                            if (im.code.len != 0 && !import_flush(&im, name, err, errn)) goto fail_msg_set;
                            wcopy(name, 0x80, namebuf);
                            w_to_utf8(codebuf, l8, sizeof l8);
                            if (!mh_wbuf_puts(&im.code, l8) || !mh_wbuf_puts(&im.code, "\r\n")) {
                                msg = "Memoria insuficiente durante a importacao.";
                                goto fail;
                            }
                            hdr_seen = 0;
                            name_ctx = 1;
                        }
                    }
                }
                if (unrecognised) {
                    int eq = 0;
                    for (size_t k = 0; line[k]; k++)
                        if (line[k] == '=') eq = 1;
                    if (line[0] != 0 && eq) {
                        msg = "Existe metadata nao suportada no arquivo. Nenhum cheat foi importado parcialmente.";
                        goto fail;
                    }
                    if (hdr_seen || looks_like_code(line)) {
                        msg = "Instrucao ou parametro nao reconhecido. Use [NOME] e linhas XXXXXXXX YYYY.";
                        goto fail;
                    }
                    if (line[0] != 0) {
                        if (im.code.len != 0 && !import_flush(&im, name, err, errn)) goto fail_msg_set;
                        wcopy(name, 0x80, line);
                        hdr_seen = 0;
                        name_ctx = 1;
                    }
                }
            }
        }
        if (more) continue;
        goto final;
    }

final:
    if (!import_flush(&im, name, err, errn)) goto fail_msg_set;
    if (im.imported != 0) {
        MhCheatProfile *w = im.work;
        ch_profile_release(p);
        *p = *w;
        mh_free(w);
        im.work = NULL;
        if (imported) *imported = im.imported;
        ok = 1;
        goto done;
    }
    msg = "Nenhum cheat foi importado.";
fail:
    set_err(err, errn, msg);
fail_msg_set:
done:
    mh_cheat_profile_free(im.work);
    mh_wbuf_free(&im.code);
    mh_free(text);
    return ok;
}

int mh_cheat_profile_import_text(MhCheatProfile *p, const char *path, int *imported, char *err, size_t errn)
{
    return mh_cheat_profile_import_text_dialect(p, path, NULL, imported, err, errn);
}
