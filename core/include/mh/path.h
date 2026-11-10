/* mh/path.h: path strings, whole-file helpers and the CUE collector used by drag and drop.
 *
 * Path strings are UTF-8. Like the original, the string functions treat BOTH '/' and '\\' as
 * separators (CUE files written on Windows contain backslashes), but anything they build uses
 * '/'. Functions that touch the file system follow POSIX rules. */
#ifndef MH_PATH_H
#define MH_PATH_H

#include "mh/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- string-only (no file system access) ------------------------------------------------- */

/* orig: path_basename. Text after the last separator ("dir/" gives ""). NULL gives "". */
void mh_path_basename(const char *path, char *out, size_t n);
/* orig: path_dirname. Text before the last separator ("/x" gives "", "a/b/" gives "a/b");
 * a path without separator (or NULL/empty) gives ".". */
void mh_path_dirname(const char *path, char *out, size_t n);
/* orig: path_join. out = a; adds '/' unless a is empty or ends with a separator; skips any
 * separators at the start of b; appends b. So ("", "/x") gives "x" and ("a", "") gives "a/".
 * out must not alias b. Everything is truncated to n-1 bytes. */
void mh_path_join(const char *a, const char *b, char *out, size_t n);
/* orig: path_stem. Basename without its last ".ext" (".hidden" gives ""). */
void mh_path_stem(const char *path, char *out, size_t n);
/* orig: path_change_ext. Replaces the extension of the last component (only a '.' after the
 * last separator counts). ext may or may not start with '.'; NULL or "" removes the
 * extension. A path that is only an extension (".cue") becomes "." + ext. */
void mh_path_change_ext(const char *path, const char *ext, char *out, size_t n);
/* Equivalent of GetFullPathNameW without touching the disk: makes the path absolute against
 * the current directory, collapses "//", "." and "..", removes a trailing '/'. Returns false
 * for NULL/empty input or if the result does not fit in n bytes. */
bool mh_path_full(const char *path, char *out, size_t n);
/* orig: path_equal_full. Both paths made absolute (mh_path_full), compared with ASCII case
 * folding (the original did the same on a case-insensitive file system, as does default APFS).
 * NULL or empty gives false. If a path is too long to normalise the raw strings are compared. */
bool mh_path_equal_full(const char *a, const char *b);
/* orig: game_directory. Directory that groups a game: dirname(full path of cue); when the
 * name of that directory normalises (mh_normalize_title) to something starting with
 * "DISC ", "DISCO " or "CD " the parent directory is returned instead. */
void mh_game_directory(const char *cue_path, char *out, size_t n);

/* ---- file system ------------------------------------------------------------------------- */

bool mh_path_exists(const char *path);  /* orig: path_exists (anything, file or directory) */
bool mh_dir_exists(const char *path);   /* orig: dir_exists (follows symlinks) */
bool mh_file_exists(const char *path);  /* orig: file_exists (exists and is not a directory) */
/* orig: ensure_dir. mkdir -p. True if the directory exists afterwards. Cap of 1023 bytes
 * like the original buffer. */
bool mh_ensure_dir(const char *path);
/* orig: file_size64. Regular files only (a directory or unreadable file gives false). */
bool mh_file_size64(const char *path, uint64_t *size);
/* orig: read_entire_file. Reads a regular file smaller than 2 GiB (0x80000000) in 1 MiB
 * pieces. *data is malloc'ed with size+2 bytes and two trailing zero bytes (so the content
 * can be used as a NUL terminated narrow string or even a UTF-16 string); release with
 * free(). size may be NULL. On failure *data is NULL and size is 0. A zero length file
 * succeeds. */
bool mh_read_entire_file(const char *path, uint8_t **data, uint64_t *size);
/* orig: write_entire_file. Creates or truncates path IN PLACE (no temp file and no rename: a
 * failure half way leaves a partial file, exactly as the original), writes in 1 MiB pieces,
 * then flushes to stable storage (F_FULLFSYNC, the FlushFileBuffers equivalent) and closes.
 * Returns false on any failure; the flush result is not checked, as in the original. */
bool mh_write_entire_file(const char *path, const void *data, uint64_t size);
/* orig: copy_file_safe (CopyFileW with bFailIfExists = FALSE): overwrites dst. Fails if src
 * and dst are the same file. A partially written dst is removed on failure. Permission bits
 * are the default for new files, not copied from src. */
bool mh_copy_file_safe(const char *src, const char *dst);

/* ---- CUE collector for drag and drop (orig: collect, import_collect, import_sort) ----------
 * The original kept this in a 0x80410 byte block. */
#define MH_COLLECT_MAX 256      /* more CUEs than this is an error */
#define MH_COLLECT_MAX_DEPTH 32 /* sub-folder nesting limit */

typedef struct MhCollect {
    uint32_t count;      /* CUE paths stored in paths[] */
    uint32_t duplicates; /* CUEs seen again (path_equal_full) and skipped */
    uint32_t ignored;    /* dropped items that were neither a CUE nor a folder, or symlinks */
    uint32_t failed;     /* non-zero when error[] holds the message (collection aborted) */
    char error[MH_PATH_MAX + 512]; /* "<message>\r\n<path>", exact original messages */
    char paths[MH_COLLECT_MAX][MH_PATH_MAX]; /* absolute CUE paths */
} MhCollect;

MhCollect *mh_collect_new(void); /* zeroed, NULL on OOM; free with mh_collect_free */
void mh_collect_free(MhCollect *c);
/* orig: import_collect. Adds one dropped item: a .cue file (case-insensitive), or a folder
 * walked recursively in name order (ASCII case-insensitive). Other files are counted in
 * `ignored` when dropped directly and skipped silently inside folders. Symlinks are never
 * followed (counted as ignored). Names starting with "._" (AppleDouble files created on
 * non-HFS volumes) are skipped; this is an addition. Returns false (and sets failed/error)
 * on the first problem or if c->failed was already set. */
bool mh_import_collect(MhCollect *c, const char *path);
/* orig: import_sort. Stable insertion sort of paths[] by mh_import_natural_compare. */
void mh_import_sort(MhCollect *c);
/* orig: import_natural_compare. Natural order: ASCII case-insensitive, digit runs compare by
 * numeric value (leading zeros ignored, "a2" < "a10"). Returns -1, 0 or 1. */
int mh_import_natural_compare(const char *a, const char *b);

#ifdef __cplusplus
}
#endif
#endif
