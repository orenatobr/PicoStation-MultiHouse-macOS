/* Private helpers of the V2 writer (part B). */
#ifndef MH_V2_WRITE_INTERNAL_H
#define MH_V2_WRITE_INTERNAL_H

#include "mh/v2write.h"

#include <string.h>

/* The original copied messages with w_copy(dst, 0x200, ...): at most 511 characters. */
#define MH_V2W_MSG_MAX 0x200

/* orig: result_msg.part.0.constprop.0: ok = 0, message. The rest of the block is untouched
 * (the original did not clear it either, callers zero it first). */
static inline void mh_v2w_msg(MhV2Result *res, const char *msg)
{
    if (!res) return;
    res->ok = 0;
    mh_str_copy(res->msg, MH_V2W_MSG_MAX, msg);
}

/* orig: result_io_msg.part.0: ok = 0, code, text + " (erro Win32 N)" when code != 0. */
static inline void mh_v2w_io_msg(MhV2Result *res, const char *msg, int code)
{
    MhWbuf w;
    if (!res) return;
    res->ok = 0;
    res->code = code;
    mh_wbuf_init(&w);
    mh_wbuf_puts(&w, msg);
    if (code != 0) {
        mh_wbuf_puts(&w, " (erro Win32 ");
        mh_wbuf_u32(&w, (uint32_t)code);
        mh_wbuf_putc(&w, ')');
    }
    mh_str_copy(res->msg, MH_V2W_MSG_MAX, w.data ? w.data : msg);
    mh_wbuf_free(&w);
}

/* Win32 style error numbers for the "(Win32 N)" texts. */
int mh_v2w_win32_code(int err);

/* The disc counts of a case, clamped like the original loops (it trusted the byte). */
#define MH_V2W_CASE_USED(c) ((c)->in_use != 0)

#endif
