/* mh/common.h: types and helpers shared by every module (included by mh/base.h). */
#ifndef MH_COMMON_H
#define MH_COMMON_H

#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The original used 0x400 wide chars for paths. Same count, now in UTF-8 bytes (with NUL). */
#define MH_PATH_MAX 1024

/* ---- memory (orig: p_alloc/p_calloc/p_realloc/p_free, HeapAlloc wrappers) ------------------
 * Differences from malloc: a size of 0 allocates 1 byte, so a non-NULL result is guaranteed
 * unless memory is really exhausted. mh_realloc(NULL, n) allocates. Plain free() is fine
 * for mh_free (mh_free(NULL) is a no-op). */
void *mh_alloc(size_t n);
void *mh_calloc(size_t n);
void *mh_realloc(void *p, size_t n);
void mh_free(void *p);

/* ---- 64-bit arithmetic (orig: u64_div/u64_mod/u64_mul_u32, software helpers) ------------- */
static inline uint64_t mh_u64_div(uint64_t a, uint64_t b) { return b ? a / b : 0; }
static inline uint64_t mh_u64_mod(uint64_t a, uint64_t b) { return b ? a % b : 0; }
static inline uint64_t mh_u64_mul_u32(uint64_t a, uint32_t b) { return a * (uint64_t)b; }

/* ---- progress / cancel (PORTING.md rule 5) -------------------------------------------------
 * cb may be NULL. It is called from the worker thread that runs the operation; the UI must
 * marshal to its own thread. `text` may be NULL (clear status). The original showed
 * "Cancelando no limite seguro da operacao..." once cancel was set; that text is the UI's job.
 * Set `cancel` from any thread; operations poll it at safe points (chunk boundaries). */
typedef struct MhProgress {
    void (*cb)(void *user, uint64_t done, uint64_t total, const char *text);
    void *user;
    _Atomic int cancel;
} MhProgress;

void mh_progress_init(MhProgress *p, void (*cb)(void *, uint64_t, uint64_t, const char *),
                      void *user);
void mh_progress_request_cancel(MhProgress *p);
bool mh_progress_cancelled(const MhProgress *p); /* p may be NULL: returns false */
/* Reports and returns true to continue, false if cancel was requested. p may be NULL. This is
 * the replacement for progress_cb(): report, then (orig) pump_messages, then test the flag. */
bool mh_progress_report(MhProgress *p, uint64_t done, uint64_t total, const char *text);
/* Replacement for pump_messages(): a no-op in core (the UI runs on its own thread). */
void mh_progress_yield(void);

/* ---- error text into caller buffers (rule 4) --------------------------------------------- */
#define MH_ERR_MSG_MAX 1024 /* orig: 0x200 wide chars */

/* printf into err (NULL or n == 0 is ignored). Always NUL terminates, truncates safely and
 * never cuts a UTF-8 sequence in half. Returns err for chaining. */
char *mh_err_set(char *err, size_t n, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

/* The original "result" block: ok flag, numeric (Win32) code, message. Orig layout:
 * u32 ok at +0, u32 code at +4, WCHAR msg[0x200] at +0x30. */
typedef struct MhErr {
    int ok;
    int code;
    char msg[MH_ERR_MSG_MAX];
} MhErr;

void mh_err_clear(MhErr *e);                          /* ok=1, code=0, msg="" */
void mh_err_fail(MhErr *e, int code, const char *fmt, ...) /* ok=0 */
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;
/* orig result_io_msg: msg, then " (erro Win32 <code>)" when code != 0. ok=0. */
void mh_err_fail_code(MhErr *e, const char *msg, int code);

#ifdef __cplusplus
}
#endif
#endif
