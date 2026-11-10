/* menu: the defaults record (menu-defaults.dat) and its place inside the menu image. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mh/base.h"
#include "mh/menu.h"

const char *const MH_MENU_THEMES[3] = {"PS One (Slim)", "PlayStation (Fat)", "Umbrella"};
const char *const MH_MENU_LANGUAGES[3] = {"Portuguese", "English", "Spanish"};
const char *const MH_MENU_VISUALS[2] = {"List", "Carousel"};

static const char kMagic[16] = {'P', 'S', 'T', '_', 'D', 'E', 'F', 'A', 'U', 'L', 'T', 'S', '_', 'V', '1', '!'};

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }

/* orig: the check repeated in menu_defaults_patch, menu_defaults_dialog and
 * project_use_default_menu (menu_defaults_read has the same one on its local copy) */
bool mh_menu_defaults_valid(const uint8_t *r) {
    if (memcmp(r, kMagic, 16) != 0 || rd32(r + 0x10) != 0x400001) return false;
    uint32_t w = rd16(r + 0x14), h = rd16(r + 0x16);
    if (w != 0x140 && w != 0x170 && w != 0x200) return false;
    if (h != 0xf0 && h != 0x100) return false;
    if (r[0x18] >= 3 || r[0x19] >= 3 || r[0x1a] >= 2 || r[0x1b] >= 2 || r[0x1c] >= 2 || r[0x1d] >= 0x65)
        return false;
    for (int i = 0x1e; i < 0x3c; i++)
        if (r[i]) return false;
    return mh_crc32(r, 0x3c) == rd32(r + 0x3c);
}

void mh_menu_defaults_seal(uint8_t *r) {
    uint32_t c = mh_crc32(r, 0x3c);
    r[0x3c] = (uint8_t)c;
    r[0x3d] = (uint8_t)(c >> 8);
    r[0x3e] = (uint8_t)(c >> 16);
    r[0x3f] = (uint8_t)(c >> 24);
}

bool mh_menu_defaults_parse(const uint8_t *r, MhMenuDefaults *o) {
    if (!mh_menu_defaults_valid(r)) return false;
    o->width = (uint16_t)rd16(r + 0x14);
    o->height = (uint16_t)rd16(r + 0x16);
    o->theme = r[0x18];
    o->language = r[0x19];
    o->visual = r[0x1a];
    o->favorites = r[0x1b];
    o->music = r[0x1c];
    o->volume = r[0x1d];
    return true;
}

bool mh_menu_defaults_build(uint8_t *r, const MhMenuDefaults *d) {
    memset(r, 0, MH_MENU_DEFAULTS_SIZE);
    memcpy(r, kMagic, 16);
    r[0x10] = 0x01; r[0x12] = 0x40; /* 0x00400001 */
    r[0x14] = (uint8_t)d->width; r[0x15] = (uint8_t)(d->width >> 8);
    r[0x16] = (uint8_t)d->height; r[0x17] = (uint8_t)(d->height >> 8);
    r[0x18] = d->theme; r[0x19] = d->language; r[0x1a] = d->visual;
    r[0x1b] = d->favorites; r[0x1c] = d->music; r[0x1d] = d->volume;
    mh_menu_defaults_seal(r);
    return mh_menu_defaults_valid(r);
}

/* orig: window_proc, "Restaurar padroes" (values in .rdata at 0x140f138d0) */
void mh_menu_defaults_factory(uint8_t *r) {
    MhMenuDefaults d = {0x140, 0xf0, 1, 0, 0, 0, 1, 68};
    mh_menu_defaults_build(r, &d);
}

/* orig: menu_defaults_read @0x1400242f0 */
bool mh_menu_defaults_read(const uint8_t *img, uint64_t size, uint8_t *out, uint64_t *offset) {
    if (!img || size < 0x40 || size % MH_MENU_SECTOR != 0) return false;
    int found = 0;
    uint64_t at = 0;
    for (uint64_t i = 0;; i++) {
        if (img[i] == 'P' && mh_menu_defaults_valid(img + i)) {
            uint64_t in = i % MH_MENU_SECTOR, sec = i - in;
            if (in + 0x40 > 0x818 || in < 0x18) return false;
            if (img[sec + 0xf] != 2 || (img[sec + 0x12] & 0x20)) return false;
            if (memcmp(img + sec + 0x10, img + sec + 0x14, 4) != 0) return false;
            found++;
            at = i;
        }
        if (size < i + 0x41) break;
    }
    if (found != 1) return false;
    if (out) memcpy(out, img + at, 0x40);
    if (offset) *offset = at;
    return true;
}

/* orig: menu_defaults_patch @0x140024090 */
bool mh_menu_defaults_patch(uint8_t *img, uint64_t size, const uint8_t *rec) {
    uint64_t at;
    if (!rec || !mh_menu_defaults_valid(rec) || !mh_menu_defaults_read(img, size, NULL, &at))
        return false;
    memcpy(img + at, rec, 0x40);
    mh_cd_repair_sector(img + at / MH_MENU_SECTOR * MH_MENU_SECTOR);
    return true;
}

/* orig: menu_defaults_compatible @0x140023b20 */
bool mh_menu_defaults_compatible(const uint8_t *a, uint64_t asz, const uint8_t *b, uint64_t bsz) {
    uint64_t oa, ob;
    if (asz != bsz || !mh_menu_defaults_read(a, asz, NULL, &oa) ||
        !mh_menu_defaults_read(b, bsz, NULL, &ob) || oa != ob)
        return false;
    uint64_t sec = oa / MH_MENU_SECTOR * MH_MENU_SECTOR;
    if (memcmp(a, b, sec) != 0) return false;
    if (memcmp(a + sec + MH_MENU_SECTOR, b + sec + MH_MENU_SECTOR, asz - sec - MH_MENU_SECTOR) != 0)
        return false;
    uint8_t s[MH_MENU_SECTOR], t[MH_MENU_SECTOR];
    memcpy(s, a + sec, sizeof s);
    memcpy(t, s, sizeof t);
    mh_cd_repair_sector(t);
    if (memcmp(s, t, sizeof s) != 0) return false; /* a's own EDC/ECC must be good */
    memcpy(s + oa % MH_MENU_SECTOR, b + ob, 0x40);
    mh_cd_repair_sector(s);
    return memcmp(s, b + sec, sizeof s) == 0;
}

/* orig: project_use_default_menu.constprop.0 @0x140029090 */
bool mh_menu_defaults_load(const char *path, uint8_t *rec) {
    uint8_t *d = NULL;
    uint64_t n = 0;
    bool ok = false;
    if (mh_read_entire_file(path, &d, &n) && n == MH_MENU_DEFAULTS_SIZE && mh_menu_defaults_valid(d)) {
        memcpy(rec, d, MH_MENU_DEFAULTS_SIZE);
        ok = true;
    }
    free(d);
    return ok;
}

/* orig: cmd_defaults @0x140011130 (write to a temp file next to the target, then replace) */
bool mh_menu_defaults_save(const char *path, const uint8_t *rec) {
    char tmp[MH_PATH_MAX];
    if (!mh_menu_defaults_valid(rec)) return false;
    mh_path_change_ext(path, "tmp", tmp, sizeof tmp);
    if (!mh_write_entire_file(tmp, rec, MH_MENU_DEFAULTS_SIZE)) return false;
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return false;
    }
    return true;
}
