/* Private helpers of the v2 module. */
#ifndef MH_V2_INTERNAL_H
#define MH_V2_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "mh/v2.h"

/* Sleep for ms milliseconds (the original used Sleep()). */
void mh_v2_sleep_ms(unsigned ms);

/* The original's raw ReadFile retry loop: up to 8 tries of seek+read, sleeping 5, 15, ...,
 * 75 ms after a failure. A short read counts as a failure. Does not touch io->err. */
bool mh_v2_read_retry(MhIo *io, uint64_t off, void *buf, size_t len);
/* Same for writes (used by sd_write_image), schedule 5, 15, ... 75 ms. */
bool mh_v2_write_retry(MhIo *io, uint64_t off, const void *buf, size_t len);
/* FlushFileBuffers: F_FULLFSYNC then fsync; no error record. */
bool mh_v2_flush(MhIo *io);
bool mh_v2_write_retry_once(MhIo *io, uint64_t off, const void *buf, size_t len);

/* Safe result helpers. res may be NULL. */
static inline void mh_v2_msg(char *dst, size_t n, const char *text) { mh_str_copy(dst, n, text); }

#endif
