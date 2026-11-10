/* menu: raw sector access, ISO 9660 root lookup and the menu image validator. */
#include <stdlib.h>
#include <string.h>

#include "mh/base.h"
#include "mh/menu.h"

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint8_t fold(uint8_t c) { return (uint8_t)(c - 'a') < 26 ? (uint8_t)(c - 0x20) : c; }

/* orig: menu_user_sector @0x140024900 */
bool mh_menu_user_sector(const uint8_t *img, uint32_t stride, uint32_t nsectors, uint32_t index,
                         const uint8_t **data, uint32_t *len) {
    if (!img || index >= nsectors) return false;
    const uint8_t *p = img + (uint64_t)index * stride;
    if (p[0] != 0 || p[0xb] != 0) return false;
    for (int i = 1; i <= 10; i++)
        if (p[i] != 0xff) return false;
    if (p[0xf] == 1) {
        *data = p + 0x10;
        *len = 0x800;
    } else if (p[0xf] == 2 && memcmp(p + 0x10, p + 0x14, 4) == 0) {
        *data = p + 0x18;
        *len = (p[0x12] & 0x20) == 0 ? 0x800 : 0x914;
    } else {
        return false;
    }
    return true;
}

/* orig: menu_find_root_file @0x140024680 */
bool mh_menu_find_root_file(const uint8_t *img, uint32_t stride, uint32_t nsectors, uint32_t dir_lba,
                            uint32_t dir_sectors, const char *name, bool strip_version,
                            uint32_t *lba_out, uint32_t *size_out) {
    if (!img) return false;
    *lba_out = 0;
    *size_out = 0;
    size_t want = strlen(name);
    for (uint32_t s = 0; s < dir_sectors; s++) {
        const uint8_t *d;
        uint32_t dl;
        if (!mh_menu_user_sector(img, stride, nsectors, dir_lba + s, &d, &dl) || dl < 0x800) return false;
        uint32_t pos = 0;
        while (d[pos] != 0) {
            uint8_t reclen = d[pos];
            if (reclen < 0x22 || pos + reclen > 0x800) return false;
            uint32_t nlen = d[pos + 0x20];
            if (reclen < nlen + 0x21) return false;
            if ((d[pos + 0x19] & 2) == 0) {
                const uint8_t *nm = d + pos + 0x21;
                uint32_t cmp_len = nlen;
                if (strip_version)
                    for (uint32_t i = 0; i < nlen; i++)
                        if (nm[i] == ';') { cmp_len = i; break; }
                bool hit = cmp_len == want;
                for (uint32_t i = 0; hit && i < cmp_len; i++)
                    hit = fold(nm[i]) == fold((uint8_t)name[i]);
                if (hit) {
                    uint32_t lba = rd32(d + pos + 2), size = rd32(d + pos + 10);
                    if (lba == 0 || size == 0 || nsectors <= lba) return false;
                    if (size + 0x7ffu < 0x800u) return false; /* 32 bit wrap, as the original */
                    if (nsectors - lba < (size + 0x7ffu) >> 11) return false;
                    *lba_out = lba;
                    *size_out = size;
                    return true;
                }
            }
            pos += reclen;
            if (pos == 0x800) break;
        }
    }
    return false;
}

static bool is_eol(uint8_t c) { return c == 0 || c == '\n' || c == '\r'; }
static bool is_ws(uint8_t c) { return c == ' ' || c == '\t'; }

/* Parses the first usable "BOOT[2] = cdrom:\dir\NAME" line of a SYSTEM.CNF image into name
 * (at most 127 printable characters). False when there is none. */
static bool boot_name(const uint8_t *p, size_t len, char name[0x80]) {
    const uint8_t *end = p + len, *line = p;
    while (line < end) {
        const uint8_t *eol = line;
        while (eol < end && !is_eol(*eol)) eol++;
        const uint8_t *s = line;
        while (s < eol && is_ws(*s)) s++;
        if (eol - s > 3 && fold(s[0]) == 'B' && fold(s[1]) == 'O' && fold(s[2]) == 'O' && fold(s[3]) == 'T') {
            const uint8_t *q = s + 4;
            if (q < eol) {
                if (*q == '2') q++;
                while (q < eol && is_ws(*q)) q++;
                if (q < eol && *q == '=') {
                    q++;
                    while (q < eol && is_ws(*q)) q++;
                    const uint8_t *vend = eol;
                    while (vend > q && is_ws(vend[-1])) vend--;
                    if (vend - q >= 6 && fold(q[0]) == 'C' && fold(q[1]) == 'D' && fold(q[2]) == 'R' &&
                        fold(q[3]) == 'O' && fold(q[4]) == 'M' && q[5] == ':') {
                        q += 6;
                        if (vend <= q) return false;
                        while (q < vend && (*q == '\\' || *q == '/')) q++;
                        const uint8_t *base = q;
                        for (; q < vend; q++)
                            if (*q == '\\' || *q == '/') base = q + 1;
                        if (base >= vend) return false;
                        size_t n = 0;
                        for (; base + n < vend; n++) {
                            if ((uint8_t)(base[n] - 0x20) > 0x5e || n + 1 == 0x80) return false;
                            name[n] = (char)base[n];
                        }
                        name[n] = 0;
                        return true;
                    }
                }
            }
        }
        line = eol;
        while (line < end && is_eol(*line)) line++;
    }
    return false;
}

/* orig: v2_validate_menu_blob @0x140039a70 */
bool mh_menu_validate_blob(const uint8_t *img, uint64_t size, uint32_t stride, char *err, size_t en) {
    if (err && en) err[0] = 0;
    if (!img || (stride != MH_MENU_SLOT && stride != MH_MENU_SECTOR) || size == 0 ||
        size % stride != 0 || size / stride > MH_MENU_MAX_SECTORS) {
        mh_err_set(err, en, "O menu deve possuir de 1 a 93 setores RAW completos para este firmware.");
        return false;
    }
    uint32_t n = (uint32_t)(size / stride);
    for (uint32_t s = 0; s < n; s++) {
        const uint8_t *p = img + (uint64_t)s * stride;
        const uint8_t *d;
        uint32_t dl;
        if (!mh_menu_user_sector(img, stride, n, s, &d, &dl)) {
            mh_err_set(err, en, "O menu possui setor CD-ROM RAW invalido.");
            return false;
        }
        if (stride == MH_MENU_SLOT)
            for (uint32_t i = MH_MENU_SECTOR; i < MH_MENU_SLOT; i++)
                if (p[i]) {
                    mh_err_set(err, en, "O padding PSXRAW do menu nao esta zerado.");
                    return false;
                }
    }
    const uint8_t *pvd;
    uint32_t pl;
    /* the original read sector 16 without checking the sector count */
    if (!mh_menu_user_sector(img, stride, n, 0x10, &pvd, &pl) || pl < 0x800 || pvd[0] != 1 ||
        memcmp(pvd + 1, "CD001", 5) != 0 || pvd[6] != 1) {
        mh_err_set(err, en, "O menu nao possui PVD ISO9660 valido no setor 16.");
        return false;
    }
    if (pvd[0x9c] < 0x22) {
        mh_err_set(err, en, "O diretorio raiz ISO9660 do menu e invalido.");
        return false;
    }
    uint32_t root_lba = rd32(pvd + 0x9e), root_size = rd32(pvd + 0xa6);
    if (root_size == 0 || n <= root_lba) {
        mh_err_set(err, en, "O diretorio raiz ISO9660 aponta para fora do menu.");
        return false;
    }
    uint32_t root_secs = (root_size + 0x7ffu) >> 11;
    if (root_secs - 1 > 7 || n - root_lba < root_secs) {
        mh_err_set(err, en, "O diretorio raiz ISO9660 excede o limite seguro do menu.");
        return false;
    }
    uint32_t cnf_lba, cnf_size;
    const uint8_t *cnf;
    uint32_t cl;
    if (!mh_menu_find_root_file(img, stride, n, root_lba, root_secs, "SYSTEM.CNF", true, &cnf_lba, &cnf_size) ||
        !mh_menu_user_sector(img, stride, n, cnf_lba, &cnf, &cl)) {
        mh_err_set(err, en, "SYSTEM.CNF nao foi encontrado na raiz do menu.");
        return false;
    }
    if (cnf_size < cl) cl = cnf_size;
    char exe[0x80];
    if (!boot_name(cnf, cl, exe)) {
        mh_err_set(err, en, "SYSTEM.CNF do menu nao possui BOOT=cdrom valido.");
        return false;
    }
    uint32_t exe_lba, exe_size, el;
    const uint8_t *ex;
    if (!mh_menu_find_root_file(img, stride, n, root_lba, root_secs, exe, false, &exe_lba, &exe_size) ||
        exe_size <= 7 || !mh_menu_user_sector(img, stride, n, exe_lba, &ex, &el) || el <= 7 ||
        memcmp(ex, "PS-X EXE", 8) != 0) {
        mh_err_set(err, en, "O executavel indicado pelo SYSTEM.CNF nao e um PS-X EXE valido.");
        return false;
    }
    return true;
}

/* orig: v2_validate_menu_file @0x14003a240 */
bool mh_menu_validate_file(const char *path, bool must_match, const uint8_t *emb, uint64_t emb_size,
                           char *err, size_t en) {
    uint8_t *d = NULL;
    uint64_t n = 0;
    if (err && en) err[0] = 0;
    if (!path || !*path || !mh_read_entire_file(path, &d, &n)) {
        mh_err_set(err, en, "Nao foi possivel ler o menu selecionado.");
        return false;
    }
    bool ok = mh_menu_validate_blob(d, n, MH_MENU_SECTOR, err, en);
    if (ok && must_match && !mh_menu_defaults_compatible(d, n, emb, emb_size)) {
        mh_err_set(err, en, "O menu selecionado nao corresponde ao menu PSn00bSDK incorporado neste Manager.");
        ok = false;
    }
    free(d);
    return ok;
}

bool mh_menu_embedded_load(const char *path, uint8_t **img, uint64_t *size) {
    uint8_t *d;
    uint64_t n;
    if (!mh_read_entire_file(path, &d, &n)) return false;
    if (!mh_menu_validate_blob(d, n, MH_MENU_SECTOR, NULL, 0) || !mh_menu_defaults_read(d, n, NULL, NULL)) {
        free(d);
        return false;
    }
    *img = d;
    *size = n;
    return true;
}
