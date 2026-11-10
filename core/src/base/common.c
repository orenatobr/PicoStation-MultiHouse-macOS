/* Common helpers: allocation, progress/cancel, error text. */
#include "internal.h"

#include <stdio.h>
#include <stdlib.h>

/* orig: p_alloc @0x140025200 */
void *mh_alloc(size_t n) { return malloc(n ? n : 1); }
/* orig: p_calloc @0x140025230 */
void *mh_calloc(size_t n) { return calloc(n ? n : 1, 1); }
/* orig: p_realloc @0x1400252a0 */
void *mh_realloc(void *p, size_t n) { return p ? realloc(p, n ? n : 1) : malloc(n ? n : 1); }
/* orig: p_free @0x140025260 */
void mh_free(void *p) { free(p); }

void mh_progress_init(MhProgress *p, void (*cb)(void *, uint64_t, uint64_t, const char *),
                      void *user)
{
    p->cb = cb;
    p->user = user;
    atomic_init(&p->cancel, 0);
    atomic_init(&p->phase, 0);
    atomic_init(&p->step, 0);
    atomic_init(&p->steps, 0);
}

void mh_progress_request_cancel(MhProgress *p)
{
    if (p) atomic_store(&p->cancel, 1);
}

bool mh_progress_cancelled(const MhProgress *p)
{
    return p && atomic_load((_Atomic int *)&p->cancel) != 0;
}

/* orig: progress_cb @0x140026b40 minus the window code: report, then test the cancel flag. */
bool mh_progress_report(MhProgress *p, uint64_t done, uint64_t total, const char *text)
{
    if (!p) return true;
    if (p->cb) p->cb(p->user, done, total, text);
    mh_progress_yield();
    return !mh_progress_cancelled(p);
}

/* orig: pump_messages @0x14002b100 */
void mh_progress_yield(void) {}

static void vset(char *err, size_t n, const char *fmt, va_list ap)
{
    int r;
    size_t len;
    if (!err || n == 0) return;
    r = vsnprintf(err, n, fmt, ap);
    if (r < 0) {
        err[0] = 0;
        return;
    }
    len = (size_t)r >= n ? n - 1 : (size_t)r;
    if ((size_t)r >= n) len = mh_utf8_whole_prefix(err, len);
    err[len] = 0;
}

char *mh_err_set(char *err, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vset(err, n, fmt, ap);
    va_end(ap);
    return err;
}

void mh_err_clear(MhErr *e)
{
    e->ok = 1;
    e->code = 0;
    e->msg[0] = 0;
}

void mh_err_fail(MhErr *e, int code, const char *fmt, ...)
{
    va_list ap;
    e->ok = 0;
    e->code = code;
    va_start(ap, fmt);
    vset(e->msg, sizeof e->msg, fmt, ap);
    va_end(ap);
}

/* orig: result_io_msg.part.0 @0x14002dcb0 */
void mh_err_fail_code(MhErr *e, const char *msg, int code)
{
    if (code != 0)
        mh_err_fail(e, code, "%s (error %u)", msg ? msg : "", (unsigned)code);
    else
        mh_err_fail(e, 0, "%s", msg ? msg : "");
}
