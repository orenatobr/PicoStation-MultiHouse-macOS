/* v2_util.c: result block, retry loops. */
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#include "v2_internal.h"

/* orig: result_msg.part.0.constprop.0 @0x14002dd40: only ok and the text change */
void mh_v2_result_fail(MhV2Result *res, const char *msg)
{
    if (!res) return;
    res->ok = 0;
    mh_str_copy(res->msg, sizeof res->msg, msg);
}

/* orig: result_io_msg.part.0 @0x14002dcb0 */
void mh_v2_result_fail_code(MhV2Result *res, const char *msg, int code)
{
    if (!res) return;
    res->ok = 0;
    res->code = code;
    if (code != 0)
        mh_err_set(res->msg, sizeof res->msg, "%s (erro Win32 %u)", msg, (unsigned)code);
    else
        mh_str_copy(res->msg, sizeof res->msg, msg);
}

void mh_v2_sleep_ms(unsigned ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static bool pread_full(int fd, uint64_t off, void *buf, size_t len)
{
    size_t done = 0;
    while (done < len) {
        ssize_t n = pread(fd, (uint8_t *)buf + done, len - done, (off_t)(off + done));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        done += (size_t)n;
    }
    return true;
}

static bool pwrite_full(int fd, uint64_t off, const void *buf, size_t len)
{
    size_t done = 0;
    while (done < len) {
        ssize_t n = pwrite(fd, (const uint8_t *)buf + done, len - done, (off_t)(off + done));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        done += (size_t)n;
    }
    return true;
}

bool mh_v2_read_retry(MhIo *io, uint64_t off, void *buf, size_t len)
{
    unsigned d = 5;
    for (;;) {
        unsigned next;
        if (pread_full(io->fd, off, buf, len)) return true;
        next = d + 10;
        mh_v2_sleep_ms(d);
        d = next;
        if (d == 0x55) return false;
    }
}

bool mh_v2_write_retry(MhIo *io, uint64_t off, const void *buf, size_t len)
{
    unsigned d = 5;
    for (;;) {
        unsigned next;
        if (pwrite_full(io->fd, off, buf, len)) return true;
        next = d + 10;
        mh_v2_sleep_ms(d);
        d = next;
        if (d == 0x55) return false;
    }
}

bool mh_v2_flush(MhIo *io)
{
    if (fcntl(io->fd, F_FULLFSYNC) == 0) return true;
    return fsync(io->fd) == 0;
}

/* One try of a write (no retry schedule), used by the block writer which has its own. */
bool mh_v2_write_retry_once(MhIo *io, uint64_t off, const void *buf, size_t len)
{
    return pwrite_full(io->fd, off, buf, len);
}
