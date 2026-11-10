/* mh/menu.h: the PicoStation boot menu image (menu.bin) and its defaults record.
 *
 * The menu is a tiny PS1 disc image kept as raw 2352 byte sectors (MH_MENU_SECTOR, or 2560
 * byte "PSXRAW" slots, MH_MENU_SLOT, whose last 208 bytes must be zero), 1 to 93 sectors:
 *   sector 16        ISO 9660 PVD ("CD001"); root directory record at PVD+0x9C
 *   root directory   at most 8 sectors; must hold SYSTEM.CNF whose BOOT= line names a file
 *                    that starts with "PS-X EXE"
 *   the default menu (resources/embedded/picostation_menu.bin, 93 sectors, Mode 2 Form 1):
 *     LBA 22 root dir, 23 SYSTEM.CNF (BOOT=cdrom:\SCES_313.37;1), 24..71 SCES_313.37 (the
 *     PSn00bSDK menu executable), 72 PICO.DAT; the defaults record sits inside the executable
 *     (sector 57, byte 1372 of the raw sector)
 *
 * Defaults record (MH_MENU_DEFAULTS_SIZE = 64 bytes, "menu-defaults.dat" holds exactly one):
 *   0x00 "PST_DEFAULTS_V1!"   0x10 u32 0x00400001   0x14 u16 width 0x140/0x170/0x200
 *   0x16 u16 height 0xF0/0x100   0x18 u8 theme 0..2 (PS One Slim, PlayStation Fat, Umbrella)
 *   0x19 u8 language 0..2 (Portugues, English, Espanol)   0x1A u8 visual 0..1 (Lista, Carrossel)
 *   0x1B u8 start in favourites 0/1   0x1C u8 Umbrella theme music 0/1   0x1D u8 volume 0..100
 *   0x1E..0x3B zero   0x3C u32 CRC-32 of bytes 0..0x3B.
 * Inside the menu image the record is stored in the user data of a Mode 2 Form 1 sector
 * (record end <= 0x818 of the raw sector, start >= 0x18); patching it rewrites the sector's
 * EDC/ECC (mh_cd_repair_sector). The record must occur exactly once in the image. */
#ifndef MH_MENU_H
#define MH_MENU_H

#include "mh/common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MH_MENU_SECTOR 0x930
#define MH_MENU_SLOT 0xa00
#define MH_MENU_MAX_SECTORS 93
#define MH_MENU_DEFAULTS_SIZE 0x40

/* ---- defaults record ---------------------------------------------------------------------- */

typedef struct MhMenuDefaults {
    uint16_t width, height;
    uint8_t theme, language, visual;
    uint8_t favorites, music, volume;
} MhMenuDefaults;

/* UI strings of the dialog, indexes match the record bytes. */
extern const char *const MH_MENU_THEMES[3];
extern const char *const MH_MENU_LANGUAGES[3];
extern const char *const MH_MENU_VISUALS[2];

/* The validation shared by menu_defaults_patch, project_use_default_menu and
 * menu_defaults_dialog: magic, version word, ranges, zero fill and CRC. */
bool mh_menu_defaults_valid(const uint8_t rec[MH_MENU_DEFAULTS_SIZE]);
/* Stores the CRC-32 of the first 0x3C bytes at 0x3C (what the dialog does after editing). */
void mh_menu_defaults_seal(uint8_t rec[MH_MENU_DEFAULTS_SIZE]);
/* Decodes a (valid) record; false if invalid. */
bool mh_menu_defaults_parse(const uint8_t rec[MH_MENU_DEFAULTS_SIZE], MhMenuDefaults *out);
/* Builds and seals a record. False if a field is out of range (volume > 100, ...). */
bool mh_menu_defaults_build(uint8_t rec[MH_MENU_DEFAULTS_SIZE], const MhMenuDefaults *d);
/* "Restaurar padroes" button: 320x240, theme 1, Portugues, Lista, favourites off, music on,
 * volume 68, sealed. */
void mh_menu_defaults_factory(uint8_t rec[MH_MENU_DEFAULTS_SIZE]);

/* orig: menu_defaults_read. Finds the record inside a menu image (size must be a multiple of
 * 0x930, at least 0x40): scans every byte offset for a valid record located in the user data
 * of a Mode 2 Form 1 sector; exactly one must exist, anything malformed aborts with false.
 * out (64 bytes) and offset may be NULL. */
bool mh_menu_defaults_read(const uint8_t *img, uint64_t size, uint8_t out[MH_MENU_DEFAULTS_SIZE],
                           uint64_t *offset);
/* orig: menu_defaults_patch. Writes rec over the record of img and repairs that sector. */
bool mh_menu_defaults_patch(uint8_t *img, uint64_t size, const uint8_t rec[MH_MENU_DEFAULTS_SIZE]);
/* orig: menu_defaults_compatible. True when the two images have the same size and differ at
 * most in the defaults record (and the EDC/ECC of its sector, which must be correct). Used to
 * accept a user supplied menu only if it is the embedded one with other defaults. */
bool mh_menu_defaults_compatible(const uint8_t *a, uint64_t asize, const uint8_t *b, uint64_t bsize);
/* orig: project_use_default_menu.constprop.0, file part. Reads "menu-defaults.dat": exactly
 * 64 bytes and valid. The caller keeps the record and the "have defaults" flag. */
bool mh_menu_defaults_load(const char *path, uint8_t rec[MH_MENU_DEFAULTS_SIZE]);
/* orig: cmd_defaults, save part. Writes rec to "<path>.tmp" then renames over path. */
bool mh_menu_defaults_save(const char *path, const uint8_t rec[MH_MENU_DEFAULTS_SIZE]);

/* ---- ISO helpers and validation ----------------------------------------------------------- */

/* orig: menu_user_sector. Locates the user data of sector `index` (stride bytes apart): sync
 * 00 FF*10 00, Mode 1 (2048 bytes at +0x10) or Mode 2 with equal subheader copies (data at
 * +0x18, 2048 bytes for Form 1, 2324 for Form 2; the original returned 0x914 = 2324). The
 * port adds the bound check index < nsectors. */
bool mh_menu_user_sector(const uint8_t *img, uint32_t stride, uint32_t nsectors, uint32_t index,
                         const uint8_t **data, uint32_t *len);
/* orig: menu_find_root_file. Looks for a file in the root directory (dir_sectors sectors from
 * LBA dir_lba): ASCII case-insensitive; with strip_version a ";n" suffix of the directory
 * entry is ignored. Needs a non-zero start LBA below nsectors and a non-zero size that fits. */
bool mh_menu_find_root_file(const uint8_t *img, uint32_t stride, uint32_t nsectors, uint32_t dir_lba,
                            uint32_t dir_sectors, const char *name, bool strip_version,
                            uint32_t *lba, uint32_t *size);
/* orig: v2_validate_menu_blob. stride is 0x930 or 0xa00. Failure texts (Portuguese, exact) go
 * to err when given. */
bool mh_menu_validate_blob(const uint8_t *img, uint64_t size, uint32_t stride, char *err,
                           size_t err_n);
/* orig: v2_validate_menu_file. Reads a menu file (0x930 stride), validates it and, when
 * must_match_embedded, requires mh_menu_defaults_compatible with the embedded menu. */
bool mh_menu_validate_file(const char *path, bool must_match_embedded, const uint8_t *embedded,
                           uint64_t embedded_size, char *err, size_t err_n);
/* Reads the embedded default menu (resources/embedded/picostation_menu.bin) and checks that it
 * validates and carries a defaults record. *img is malloc'ed. */
bool mh_menu_embedded_load(const char *path, uint8_t **img, uint64_t *size);

#ifdef __cplusplus
}
#endif
#endif
