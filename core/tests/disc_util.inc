/* Shared helpers of the disc tests: check macro, temp dirs, a synthetic ISO 9660 builder. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mh/disc.h"

#define UNUSED __attribute__((unused))

/* The cheat module is not linked into this test (it only needs the free function). */
void mh_cheat_profile_free(MhCheatProfile *p) { (void)p; }

static int g_fail, g_checks;
#define CHECK(c)                                                                           \
    do {                                                                                   \
        g_checks++;                                                                        \
        if (!(c)) {                                                                        \
            g_fail++;                                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                   \
        }                                                                                  \
    } while (0)
#define CHECK_STR(a, b)                                                                    \
    do {                                                                                   \
        g_checks++;                                                                        \
        if (strcmp((a), (b)) != 0) {                                                       \
            g_fail++;                                                                      \
            fprintf(stderr, "FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); \
        }                                                                                  \
    } while (0)

#define FIXTURES "/Volumes/Renato/PicoStation BR"
#define EMBEDDED "resources/embedded"

static char g_tmp[MH_PATH_MAX];

UNUSED static void tmp_init(const char *tag)
{
    const char *t = getenv("TMPDIR");
    snprintf(g_tmp, sizeof g_tmp, "%s/mhdisc-%s-XXXXXX", t && t[0] ? t : "/tmp", tag);
    if (!mkdtemp(g_tmp)) { perror("mkdtemp"); exit(2); }
}

UNUSED static void rm_rf(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d) { unlink(dir); return; }
    while ((e = readdir(d))) {
        char p[MH_PATH_MAX];
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        rm_rf(p);
    }
    closedir(d);
    rmdir(dir);
}

UNUSED static void tmp_path(const char *name, char *out) { snprintf(out, MH_PATH_MAX, "%s/%s", g_tmp, name); }

UNUSED static void put_file(const char *name, const void *data, size_t n)
{
    char p[MH_PATH_MAX];
    FILE *f;
    tmp_path(name, p);
    f = fopen(p, "wb");
    if (!f) { perror(p); exit(2); }
    if (n) fwrite(data, 1, n, f);
    fclose(f);
}

UNUSED static void put_text(const char *name, const char *text) { put_file(name, text, strlen(text)); }

/* zero filled file of `sectors` * 2352 bytes (sparse) */
UNUSED static void put_zero_bin(const char *name, uint64_t bytes)
{
    char p[MH_PATH_MAX];
    FILE *f;
    tmp_path(name, p);
    f = fopen(p, "wb");
    if (!f) { perror(p); exit(2); }
    if (bytes) {
        if (ftruncate(fileno(f), (off_t)bytes) != 0) { perror("ftruncate"); exit(2); }
    }
    fclose(f);
}

UNUSED static MhDisc * parse_tmp_cue(const char *name)
{
    char p[MH_PATH_MAX];
    tmp_path(name, p);
    return mh_cue_parse(p);
}

/* ---- synthetic ISO 9660 in mode 2 form 1 raw sectors ------------------------------------- */

typedef struct IsoFile {
    const char *name;     /* ISO name, e.g. "SLUS_009.69;1" */
    const void *data;
    uint32_t size;
    const char *dir;      /* NULL = root, otherwise a sub-directory name */
} IsoFile;

typedef struct Iso {
    uint8_t *raw;         /* sectors * stride */
    uint32_t sectors;     /* image sectors in file */
    uint32_t volume;      /* declared volume size */
    uint32_t stride;      /* 2352 or 2048 */
    uint32_t udo;         /* 0x18 (mode 2 form 1), 0x10 (mode 1) or 0 */
} Iso;

UNUSED static void le32w(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
UNUSED static void be32w(uint8_t *p, uint32_t v) { p[3] = v; p[2] = v >> 8; p[1] = v >> 16; p[0] = v >> 24; }

UNUSED static uint32_t dirrec(uint8_t *p, const char *name, size_t nlen, uint32_t lba, uint32_t size, int is_dir)
{
    uint32_t len = 33 + (uint32_t)nlen;
    if (len & 1) len++;
    memset(p, 0, len);
    p[0] = (uint8_t)len;
    le32w(p + 2, lba); be32w(p + 6, lba);
    le32w(p + 10, size); be32w(p + 14, size);
    p[25] = is_dir ? 2 : 0;
    p[28] = 1; p[31] = 1;
    p[32] = (uint8_t)nlen;
    memcpy(p + 33, name, nlen);
    return len;
}

UNUSED static uint8_t * iso_user(Iso *iso, uint32_t sector) { return iso->raw + (size_t)sector * iso->stride + iso->udo; }

/* Builds an image: sector 16 PVD, 17 root directory, optional sub directory at 18, files after.
 * The root holds every file with dir == NULL plus one entry per distinct dir name. */
UNUSED static void iso_build(Iso *iso, const IsoFile *files, int nfiles, uint32_t stride, uint32_t udo,
                      uint32_t extra_sectors, int declare_extra)
{
    uint32_t next = 19, root = 17, subdir = 18, total;
    uint32_t lba[32];
    const char *subname = NULL;
    uint8_t *p;
    uint32_t pos;
    for (int i = 0; i < nfiles; i++) {
        lba[i] = next;
        next += (files[i].size + 2047) / 2048;
        if (files[i].dir) subname = files[i].dir;
    }
    total = next + extra_sectors;
    iso->stride = stride;
    iso->udo = udo;
    iso->sectors = total;
    iso->volume = declare_extra ? total + 100 : total;
    iso->raw = calloc(total, stride);
    p = iso_user(iso, 16);
    p[0] = 1; memcpy(p + 1, "CD001", 5); p[6] = 1;
    le32w(p + 80, iso->volume); be32w(p + 84, iso->volume);
    dirrec(p + 156, "\0", 1, root, 2048, 1);
    /* root directory */
    p = iso_user(iso, root);
    pos = 0;
    pos += dirrec(p + pos, "\0", 1, root, 2048, 1);
    pos += dirrec(p + pos, "\1", 1, root, 2048, 1);
    if (subname) pos += dirrec(p + pos, subname, strlen(subname), subdir, 2048, 1);
    for (int i = 0; i < nfiles; i++)
        if (!files[i].dir) pos += dirrec(p + pos, files[i].name, strlen(files[i].name), lba[i], files[i].size, 0);
    p = iso_user(iso, subdir);
    pos = 0;
    pos += dirrec(p + pos, "\0", 1, subdir, 2048, 1);
    pos += dirrec(p + pos, "\1", 1, root, 2048, 1);
    for (int i = 0; i < nfiles; i++)
        if (files[i].dir) pos += dirrec(p + pos, files[i].name, strlen(files[i].name), lba[i], files[i].size, 0);
    for (int i = 0; i < nfiles; i++) {
        uint32_t left = files[i].size, off = 0, s = lba[i];
        while (left) {
            uint32_t n = left > 2048 ? 2048 : left;
            memcpy(iso_user(iso, s), (const uint8_t *)files[i].data + off, n);
            off += n; left -= n; s++;
        }
    }
}

UNUSED static void iso_write(const Iso *iso, const char *name)
{
    put_file(name, iso->raw, (size_t)iso->sectors * iso->stride);
}

/* The same sectors as a V2 raw slot (2352 bytes padded to 2560) preceded by pre_blocks of zeros */
UNUSED static void iso_write_raw_slots(const Iso *iso, const char *name, uint32_t pre_blocks)
{
    size_t n = (size_t)iso->sectors * 0xa00 + (size_t)pre_blocks * 512;
    uint8_t *buf = calloc(1, n);
    for (uint32_t s = 0; s < iso->sectors; s++)
        memcpy(buf + (size_t)pre_blocks * 512 + (size_t)s * 0xa00, iso->raw + (size_t)s * iso->stride, 2352);
    put_file(name, buf, n);
    free(buf);
}

UNUSED static void iso_free(Iso *iso) { free(iso->raw); }

UNUSED static uint8_t * make_exe(size_t size, const char *embedded)
{
    uint8_t *e = calloc(1, size);
    memcpy(e, "PS-X EXE", 8);
    if (embedded) memcpy(e + 0x100, embedded, strlen(embedded));
    return e;
}

UNUSED static int note_has_end(const MhDisc *d, const char *s)
{
    size_t a = strlen(d->notes), b = strlen(s);
    return a >= b && strcmp(d->notes + a - b, s) == 0;
}
