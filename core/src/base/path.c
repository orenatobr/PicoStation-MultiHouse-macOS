/* Path strings, whole-file helpers, CUE collector. */
#include "internal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FILE_CHUNK 0x100000 /* orig: 1 MiB pieces in read/write_entire_file */

/* orig: path_basename @0x140025480 */
void mh_path_basename(const char *path, char *out, size_t n)
{
    const char *base = path ? path : "";
    const char *p;
    for (p = base; *p; p++)
        if (mh_is_sep(*p)) base = p + 1;
    mh_str_copy(out, n, base);
}

/* orig: path_dirname @0x1400258b0 */
void mh_path_dirname(const char *path, char *out, size_t n)
{
    size_t len, i;
    if (!out || n == 0) return;
    len = mh_strlen(path);
    for (i = len; i > 0; i--)
        if (mh_is_sep(path[i - 1])) {
            size_t k = i - 1 < n - 1 ? i - 1 : n - 1;
            k = mh_utf8_whole_prefix(path, k);
            memcpy(out, path, k);
            out[k] = 0;
            return;
        }
    mh_str_copy(out, n, ".");
}

/* orig: path_join @0x140025bc0 */
void mh_path_join(const char *a, const char *b, char *out, size_t n)
{
    size_t len;
    if (!out || n == 0) return;
    mh_str_copy(out, n, a);
    len = strlen(out);
    if (len > 0 && !mh_is_sep(out[len - 1]) && len + 1 < n) {
        out[len] = '/';
        out[len + 1] = 0;
    }
    if (b)
        while (mh_is_sep(*b)) b++;
    mh_str_cat(out, n, b);
}

/* orig: path_stem @0x140025df0 */
void mh_path_stem(const char *path, char *out, size_t n)
{
    char *dot;
    if (!out || n == 0) return;
    mh_path_basename(path, out, n);
    dot = strrchr(out, '.');
    if (dot) *dot = 0;
}

/* orig: path_change_ext @0x140025550 */
void mh_path_change_ext(const char *path, const char *ext, char *out, size_t n)
{
    size_t len;
    if (!out || n == 0) return;
    mh_str_copy(out, n, path);
    for (len = strlen(out); len > 0; len--) {
        if (out[len - 1] == '.') {
            out[len - 1] = 0;
            break;
        }
        if (mh_is_sep(out[len - 1])) break;
    }
    if (!ext || !*ext) return;
    if (*ext != '.') mh_str_cat(out, n, ".");
    mh_str_cat(out, n, ext);
}

/* Equivalent of GetFullPathNameW (orig: used by path_equal_full, collect, game_directory). */
bool mh_path_full(const char *path, char *out, size_t n)
{
    char src[4096], tmp[4096];
    size_t o = 0, i;
    if (!path || !*path || !out || n == 0) return false;
    if (path[0] == '/') {
        if (strlen(path) >= sizeof src) return false;
        strcpy(src, path);
    } else {
        size_t cl;
        if (!getcwd(src, sizeof src)) return false;
        cl = strlen(src);
        if (cl + 1 + strlen(path) >= sizeof src) return false;
        src[cl] = '/';
        strcpy(src + cl + 1, path);
    }
    for (i = 0; src[i];) {
        size_t s = i, e;
        while (src[i] && src[i] != '/') i++;
        e = i;
        while (src[i] == '/') i++;
        if (e == s || (e - s == 1 && src[s] == '.')) continue;
        if (e - s == 2 && src[s] == '.' && src[s + 1] == '.') {
            while (o > 0 && tmp[o - 1] != '/') o--;
            if (o > 0) o--; /* drop the '/' that preceded the removed component */
            continue;
        }
        tmp[o++] = '/';
        memcpy(tmp + o, src + s, e - s);
        o += e - s;
    }
    if (o == 0) tmp[o++] = '/';
    if (o + 1 > n) return false;
    memcpy(out, tmp, o);
    out[o] = 0;
    return true;
}

/* orig: path_equal_full @0x1400259a0 */
bool mh_path_equal_full(const char *a, const char *b)
{
    char fa[4096], fb[4096];
    if (!a || !b || !*a || !*b) return false;
    if (!mh_path_full(a, fa, sizeof fa) || !mh_path_full(b, fb, sizeof fb))
        return mh_str_icmp(a, b) == 0;
    return mh_str_icmp(fa, fb) == 0;
}

/* orig: game_directory @0x14001b2e0 */
void mh_game_directory(const char *cue_path, char *out, size_t n)
{
    char full[MH_PATH_MAX], base[MH_PATH_MAX], norm[256], up[MH_PATH_MAX];
    int r;
    if (!out || n == 0) return;
    if (!mh_path_full(cue_path, full, sizeof full)) mh_str_copy(full, sizeof full, cue_path);
    mh_path_dirname(full, out, n);
    mh_path_basename(out, base, sizeof base);
    mh_normalize_title(base, norm, sizeof norm);
    r = mh_str_find_i(norm, "DISC ");
    if (r < 0) r = mh_str_find_i(norm, "DISCO ");
    if (r < 0) r = mh_str_find_i(norm, "CD ");
    if (r == 0) {
        mh_path_dirname(out, up, sizeof up);
        mh_str_copy(out, n, up);
    }
}

/* orig: path_exists @0x140025ba0 */
bool mh_path_exists(const char *path)
{
    struct stat st;
    return path && lstat(path, &st) == 0;
}

/* orig: dir_exists @0x140017bb0 */
bool mh_dir_exists(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* orig: file_exists @0x14001b010 */
bool mh_file_exists(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0 && !S_ISDIR(st.st_mode);
}

/* orig: ensure_dir @0x140019780. Deviation: the original also returned true when a plain
 * file occupied the name (ERROR_ALREADY_EXISTS); here the result is true only for a
 * directory. */
bool mh_ensure_dir(const char *path)
{
    char buf[1024];
    size_t i;
    if (!path || !*path) return false;
    if (mh_dir_exists(path)) return true;
    mh_str_copy(buf, sizeof buf, path); /* orig: 1023 character buffer */
    for (i = 1; buf[i]; i++) {
        if (buf[i] != '/') continue;
        buf[i] = 0;
        if (!mh_dir_exists(buf)) (void)mkdir(buf, 0777);
        buf[i] = '/';
    }
    if (mh_dir_exists(buf)) return true;
    return mkdir(buf, 0777) == 0 || (errno == EEXIST && mh_dir_exists(buf));
}

/* orig: file_size64 @0x14001b040 */
bool mh_file_size64(const char *path, uint64_t *size)
{
    struct stat st;
    off_t end;
    int fd;
    bool ok = false;
    if (!path) return false;
    fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    if (fstat(fd, &st) == 0 && !S_ISDIR(st.st_mode)) {
        if (S_ISREG(st.st_mode)) {
            if (size) *size = (uint64_t)st.st_size;
            ok = true;
        } else if ((end = lseek(fd, 0, SEEK_END)) >= 0) {
            if (size) *size = (uint64_t)end;
            ok = true;
        }
    }
    close(fd);
    return ok;
}

/* orig: read_entire_file @0x14002b700 */
bool mh_read_entire_file(const char *path, uint8_t **data, uint64_t *size)
{
    struct stat st;
    uint8_t *buf;
    uint64_t done = 0;
    int fd;
    *data = NULL;
    if (size) *size = 0;
    if (!path) return false;
    fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || (uint64_t)st.st_size >= 0x80000000ull) {
        close(fd);
        return false;
    }
    buf = mh_alloc((size_t)st.st_size + 2);
    if (!buf) {
        close(fd);
        return false;
    }
    while (done < (uint64_t)st.st_size) {
        size_t want = (uint64_t)st.st_size - done > FILE_CHUNK ? FILE_CHUNK : (size_t)((uint64_t)st.st_size - done);
        ssize_t r = read(fd, buf + done, want);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) {
            free(buf);
            close(fd);
            return false;
        }
        done += (uint64_t)r;
    }
    buf[done] = 0;
    buf[done + 1] = 0;
    close(fd);
    *data = buf;
    if (size) *size = done;
    return true;
}

static void flush_fd(int fd)
{
    if (fcntl(fd, F_FULLFSYNC) != 0) (void)fsync(fd);
}

/* orig: write_entire_file @0x14003f620 */
bool mh_write_entire_file(const char *path, const void *data, uint64_t size)
{
    const uint8_t *p = data;
    uint64_t done = 0;
    int fd;
    if (!path) return false;
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return false;
    while (done < size) {
        size_t want = size - done > FILE_CHUNK ? FILE_CHUNK : (size_t)(size - done);
        ssize_t r = write(fd, p + done, want);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) {
            close(fd);
            return false;
        }
        done += (uint64_t)r;
    }
    flush_fd(fd);
    close(fd);
    return true;
}

/* orig: copy_file_safe @0x140012e10 (CopyFileW, overwrite allowed) */
bool mh_copy_file_safe(const char *src, const char *dst)
{
    struct stat ss, ds;
    uint8_t *buf;
    int in, out;
    bool ok = true;
    if (!src || !dst) return false;
    in = open(src, O_RDONLY);
    if (in < 0) return false;
    if (fstat(in, &ss) != 0 || S_ISDIR(ss.st_mode) ||
        (stat(dst, &ds) == 0 && ds.st_dev == ss.st_dev && ds.st_ino == ss.st_ino)) {
        close(in);
        return false;
    }
    out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    buf = malloc(FILE_CHUNK);
    if (out < 0 || !buf) {
        if (out >= 0) close(out);
        free(buf);
        close(in);
        return false;
    }
    for (;;) {
        ssize_t r = read(in, buf, FILE_CHUNK), off = 0;
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) { ok = false; break; }
        if (r == 0) break;
        while (off < r) {
            ssize_t w = write(out, buf + off, (size_t)(r - off));
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0) { ok = false; break; }
            off += w;
        }
        if (!ok) break;
    }
    free(buf);
    close(in);
    if (close(out) != 0) ok = false;
    if (!ok) unlink(dst);
    return ok;
}

/* ---- collector ----------------------------------------------------------------------------- */

MhCollect *mh_collect_new(void) { return calloc(1, sizeof(MhCollect)); }
void mh_collect_free(MhCollect *c) { free(c); }

/* Original error format: message, "\r\n", offending path, capped like the 0x200 char buffer. */
static bool col_fail(MhCollect *c, const char *msg, const char *path)
{
    c->failed = 1;
    mh_str_copy(c->error, sizeof c->error, msg);
    mh_str_cat(c->error, sizeof c->error, "\r\n");
    mh_str_cat(c->error, sizeof c->error, path);
    return false;
}

static int name_cmp(const void *x, const void *y)
{
    int r = mh_str_icmp(*(char *const *)x, *(char *const *)y);
    return r ? r : strcmp(*(char *const *)x, *(char *const *)y);
}

/* orig: collect @0x1400120a0 */
static bool collect(MhCollect *c, const char *path, int depth)
{
    struct stat st;
    DIR *d;
    char **names = NULL, child[MH_PATH_MAX];
    size_t nn = 0, cap = 0, i;
    bool ok = true;

    if (lstat(path, &st) != 0) return col_fail(c, "Nao foi possivel acessar:", path);
    if (S_ISLNK(st.st_mode)) { /* orig: reparse points are skipped */
        c->ignored++;
        return true;
    }
    if (!S_ISDIR(st.st_mode)) {
        uint32_t k;
        if (!mh_str_ends_i(path, ".cue")) {
            c->ignored++;
            return true;
        }
        for (k = 0; k < c->count; k++)
            if (mh_path_equal_full(path, c->paths[k])) {
                c->duplicates++;
                return true;
            }
        if (c->count >= MH_COLLECT_MAX)
            return col_fail(c, "O lote excede 256 CUEs. Divida a importacao em lotes menores.", path);
        if (!mh_path_full(path, c->paths[c->count], MH_PATH_MAX))
            return col_fail(c, "Caminho invalido:", path);
        c->count++;
        return true;
    }
    if (depth == MH_COLLECT_MAX_DEPTH) return col_fail(c, "Limite de subpastas excedido:", path);
    if (strlen(path) + 3 >= MH_PATH_MAX) return col_fail(c, "Caminho muito longo:", path);

    d = opendir(path);
    if (!d) {
        if (errno == ENOENT) return true; /* orig: ERROR_FILE_NOT_FOUND counts as empty */
        return col_fail(c, "Nao foi possivel listar a pasta:", path);
    }
    for (;;) {
        struct dirent *e;
        errno = 0;
        e = readdir(d);
        if (!e) break;
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (e->d_name[0] == '.' && e->d_name[1] == '_') continue; /* AppleDouble, not in orig */
        if (nn == cap) {
            char **t = realloc(names, (cap ? cap * 2 : 64) * sizeof *names);
            if (!t) { ok = col_fail(c, "Nao foi possivel listar a pasta:", path); break; }
            names = t;
            cap = cap ? cap * 2 : 64;
        }
        names[nn] = strdup(e->d_name);
        if (!names[nn]) { ok = col_fail(c, "Nao foi possivel listar a pasta:", path); break; }
        nn++;
    }
    if (ok && errno != 0)
        ok = col_fail(c, "A pasta foi desconectada ou mudou durante a leitura:", path);
    closedir(d);
    if (ok) {
        qsort(names, nn, sizeof *names, name_cmp);
        for (i = 0; ok && i < nn; i++) {
            struct stat cs;
            if (strlen(path) + 2 + strlen(names[i]) >= MH_PATH_MAX) {
                ok = col_fail(c, "Caminho muito longo:", path);
                break;
            }
            mh_path_join(path, names[i], child, sizeof child);
            if (!(stat(child, &cs) == 0 && S_ISDIR(cs.st_mode)) && !mh_str_ends_i(child, ".cue"))
                continue;
            ok = collect(c, child, depth + 1);
        }
    }
    for (i = 0; i < nn; i++) free(names[i]);
    free(names);
    return ok;
}

/* orig: import_collect @0x14001cd00 */
bool mh_import_collect(MhCollect *c, const char *path)
{
    if (!c || !path || c->failed) return false;
    return collect(c, path, 0);
}

static bool is_digit(unsigned char c) { return c >= '0' && c <= '9'; }

/* orig: import_natural_compare @0x14001cff0 */
int mh_import_natural_compare(const char *a, const char *b)
{
    const unsigned char *x = (const unsigned char *)(a ? a : "");
    const unsigned char *y = (const unsigned char *)(b ? b : "");
    for (;;) {
        if (!*x) return *y ? -1 : 0;
        if (!*y) return 1;
        if (is_digit(*x) && is_digit(*y)) {
            const unsigned char *xs = x, *ys = y;
            size_t lx = 0, ly = 0;
            int d;
            while (*xs == '0') xs++;
            while (*ys == '0') ys++;
            while (is_digit(xs[lx])) lx++;
            while (is_digit(ys[ly])) ly++;
            if (lx != ly) return lx < ly ? -1 : 1;
            d = memcmp(xs, ys, lx);
            if (d) return d < 0 ? -1 : 1;
            while (is_digit(*x)) x++;
            while (is_digit(*y)) y++;
            continue;
        } else {
            int p = *x, q = *y; /* orig folds a-z to upper case, digits compare raw */
            if (p >= 'a' && p <= 'z') p -= 32;
            if (q >= 'a' && q <= 'z') q -= 32;
            if (p != q) return p < q ? -1 : 1;
            x++;
            y++;
        }
    }
}

/* orig: import_sort @0x14001d270 (stable insertion sort of 0x800 byte entries) */
void mh_import_sort(MhCollect *c)
{
    uint32_t i, j;
    char *tmp;
    if (!c || c->count < 2) return;
    tmp = malloc(MH_PATH_MAX);
    if (!tmp) return;
    for (i = 1; i < c->count; i++) {
        memcpy(tmp, c->paths[i], MH_PATH_MAX);
        for (j = i; j > 0 && mh_import_natural_compare(tmp, c->paths[j - 1]) < 0; j--)
            memcpy(c->paths[j], c->paths[j - 1], MH_PATH_MAX);
        memcpy(c->paths[j], tmp, MH_PATH_MAX);
    }
    free(tmp);
}
