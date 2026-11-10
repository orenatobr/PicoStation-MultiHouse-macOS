/* mh/io.h: positional I/O with the original's error record, plus plain fd helpers.
 *
 * The original did all image and SD access through v1_io_read/v1_io_write on a HANDLE, with
 * a single global "first error" record (DAT_140f08ed0..) that the UI printed after a failure.
 * Here MhIo wraps a POSIX fd (pread/pwrite, so no shared file position) and the record is
 * an MhIoErr that several MhIo can share. The first error wins and stays until
 * mh_ioerr_clear(), like the original (the original never cleared it between retries). */
#ifndef MH_IO_H
#define MH_IO_H

#include "mh/common.h"
#include "mh/str.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MH_IO_CHUNK 0x40000 /* orig: reads and writes are split in 256 KiB pieces */
#define MH_IO_BLOCK 512     /* raw device block size */

/* Codes used when no errno applies, same numbers as the original Win32 codes. */
#define MH_IOERR_GENERIC 0x1f   /* system call failed without a code (ERROR_GEN_FAILURE) */
#define MH_IOERR_SHORT_READ 0x1e
#define MH_IOERR_SHORT_WRITE 0x1d
#define MH_IOERR_COMPARE 0x17   /* read-back differs */
#define MH_IOERR_ALIGN 0x57     /* offset not a multiple of 512 */
/* Text printed before the number by mh_ioerr_append ("Error code" in the original). */
#define MH_IOERR_CODE_LABEL "Error code"

typedef struct MhIoErr {
    int set;                  /* 1 when a failure was recorded */
    int code;                 /* errno, or one of MH_IOERR_* */
    const char *what;         /* static text, Portuguese, exact original (e.g. "Destination write") */
    uint64_t offset;          /* byte offset of the failing request */
    uint32_t requested;       /* bytes asked for in the failing piece */
    uint32_t transferred;     /* bytes confirmed in the failing piece */
} MhIoErr;

void mh_ioerr_clear(MhIoErr *e);
/* Appends the original report to w, starting with "\r\n":
 *   "\r\n<what>. Error code: <code>\r\nOffset: <n> bytes; LBA512: <n>\r\nRequested: <n>;
 *    confirmed transferred: <n> bytes."
 * (one line per \r\n, exactly the text built by the SD writer). Does nothing if !e->set. */
bool mh_ioerr_append(MhWbuf *w, const MhIoErr *e);

typedef struct MhIo {
    int fd;
    int is_dest;  /* orig compared the handle with the global destination: read errors are
                     labelled "Destination read" for it and "Source read" otherwise */
    MhIoErr *err; /* where failures are recorded; points to own_err unless shared */
    MhIoErr own_err;
} MhIo;

typedef enum { MH_IO_READ, MH_IO_RDWR, MH_IO_CREATE /* RDWR + create + truncate */ } MhIoMode;

/* Opens path (regular file or device node). shared_err may be NULL. Returns false and keeps
 * errno on failure. */
bool mh_io_open(MhIo *io, const char *path, MhIoMode mode, bool is_dest, MhIoErr *shared_err);
/* Takes ownership of an already open fd. */
void mh_io_attach(MhIo *io, int fd, bool is_dest, MhIoErr *shared_err);
void mh_io_close(MhIo *io); /* idempotent */
/* Current size in bytes (lseek to the end for devices). */
bool mh_io_size(MhIo *io, uint64_t *size);

/* orig: v1_io_read. Reads exactly len bytes at off, in pieces of MH_IO_CHUNK. A system error
 * or a short piece fails and records the failure. */
bool mh_io_read(MhIo *io, uint64_t off, void *buf, size_t len);
/* orig: v1_io_write. Same for writing; a short write records "Partial destination write". */
bool mh_io_write(MhIo *io, uint64_t off, const void *buf, size_t len);
/* orig: v1_io_flush (+ .part.0). F_FULLFSYNC, falling back to fsync when the device does not
 * support it. On failure records "Destination sync" with err_offset as the offset. */
bool mh_io_flush(MhIo *io, uint64_t err_offset);
/* orig: io_write_block_verified. Writes one 512 byte block, flushes, reads it back and
 * compares. off must be a multiple of 512 ("Verify alignment" otherwise). */
bool mh_io_write_block_verified(MhIo *io, uint64_t off, const void *block512);
/* orig: handle_sync_blocks_verified. Makes the device region [off, off+len) equal to src:
 * reads it in 256 KiB pieces and rewrites (verified) every 512 byte block that differs, then
 * flushes and re-reads the whole region to compare. If that final verification fails the
 * whole pass is retried, up to 4 attempts, sleeping 80, 160, 240 and 320 ms in between.
 * *lba_out (may be NULL) receives the LBA512 being processed, for error messages.
 * Progress/cancel are checked after every piece ("text" is passed to the callback); a
 * cancel returns false with no error recorded. len must be a multiple of 512. */
bool mh_io_sync_blocks_verified(MhIo *io, uint64_t off, const uint8_t *src, uint64_t len,
                                MhProgress *prog, const char *text, uint64_t *lba_out);
/* orig: handle_compare_bytes. Reads [off, off+len) and compares with src in 256 KiB pieces,
 * reporting progress after each. False on read error, difference or cancel. */
bool mh_io_compare_bytes(MhIo *io, uint64_t off, const uint8_t *src, uint64_t len,
                         MhProgress *prog, const char *text);
/* orig: read_raw_payload_at. Reads the 2560 byte slot at 512-byte block `lba512` and returns
 * its first 2352 bytes; the 208 padding bytes must all be zero or it fails. */
bool mh_io_read_raw_payload(MhIo *io, uint64_t lba512, uint8_t out[2352]);
/* orig: crc_file_region. CRC-32 of [off, off+len), read in 1 MiB pieces. */
bool mh_io_crc_region(MhIo *io, uint64_t off, uint64_t len, uint32_t *crc);

/* ---- sequential fd helpers (orig: seek64, read_exact, write_exact) ------------------------ */
bool mh_seek64(int fd, uint64_t off);                       /* SEEK_SET */
bool mh_read_exact(int fd, void *buf, size_t len);          /* false on error or EOF */
bool mh_write_exact(int fd, const void *buf, size_t len);   /* false on error or zero progress */

#ifdef __cplusplus
}
#endif
#endif
