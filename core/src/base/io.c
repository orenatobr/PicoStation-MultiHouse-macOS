/* Positional I/O with the original's error record (orig: v1_io_* and friends). */
#include "internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

void mh_ioerr_clear(MhIoErr *e) { memset(e, 0, sizeof *e); }

/* First error wins (orig: only stored when the global record was empty). */
static void rec(MhIo *io, const char *what, int code, uint64_t off, uint32_t req, uint32_t xfer)
{
    MhIoErr *e = io->err;
    if (e->set) return;
    e->set = 1;
    e->code = code;
    e->what = what;
    e->offset = off;
    e->requested = req;
    e->transferred = xfer;
}
static int syscode(void) { return errno ? errno : MH_IOERR_GENERIC; }

bool mh_ioerr_append(MhWbuf *w, const MhIoErr *e)
{
    if (!e->set) return true;
    return mh_wbuf_puts(w, "\r\n") && mh_wbuf_puts(w, e->what) &&
           mh_wbuf_puts(w, ". " MH_IOERR_CODE_LABEL ": ") && mh_wbuf_u32(w, (uint32_t)e->code) &&
           mh_wbuf_puts(w, "\r\nOffset: ") && mh_wbuf_u64(w, e->offset) &&
           mh_wbuf_puts(w, " bytes; LBA512: ") && mh_wbuf_u64(w, e->offset / MH_IO_BLOCK) &&
           mh_wbuf_puts(w, "\r\nSolicitado: ") && mh_wbuf_u32(w, e->requested) &&
           mh_wbuf_puts(w, "; transferido confirmado: ") && mh_wbuf_u32(w, e->transferred) &&
           mh_wbuf_puts(w, " bytes.");
}

void mh_io_attach(MhIo *io, int fd, bool is_dest, MhIoErr *shared_err)
{
    memset(io, 0, sizeof *io);
    io->fd = fd;
    io->is_dest = is_dest;
    io->err = shared_err ? shared_err : &io->own_err;
}

bool mh_io_open(MhIo *io, const char *path, MhIoMode mode, bool is_dest, MhIoErr *shared_err)
{
    int flags = mode == MH_IO_READ ? O_RDONLY : mode == MH_IO_RDWR ? O_RDWR : (O_RDWR | O_CREAT | O_TRUNC);
    int fd = open(path, flags | O_CLOEXEC, 0666);
    if (fd < 0) {
        memset(io, 0, sizeof *io);
        io->fd = -1;
        io->err = shared_err ? shared_err : &io->own_err;
        return false;
    }
    mh_io_attach(io, fd, is_dest, shared_err);
    return true;
}

void mh_io_close(MhIo *io)
{
    if (io->fd >= 0) close(io->fd);
    io->fd = -1;
}

bool mh_io_size(MhIo *io, uint64_t *size)
{
    struct stat st;
    off_t end;
    if (fstat(io->fd, &st) != 0) return false;
    if (S_ISREG(st.st_mode)) {
        *size = (uint64_t)st.st_size;
        return true;
    }
    end = lseek(io->fd, 0, SEEK_END);
    if (end < 0) return false;
    *size = (uint64_t)end;
    return true;
}

static bool range_ok(uint64_t off, size_t len) { return off <= INT64_MAX && len <= (uint64_t)INT64_MAX - off; }

/* orig: v1_io_read @0x140034da0 */
bool mh_io_read(MhIo *io, uint64_t off, void *buf, size_t len)
{
    uint8_t *p = buf;
    size_t done = 0;
    const char *label = io->is_dest ? "Leitura do destino" : "Leitura da origem";
    if (!range_ok(off, len)) {
        rec(io, "Posicionamento de leitura", EINVAL, off, (uint32_t)len, 0);
        return false;
    }
    while (done < len) {
        size_t chunk = len - done > MH_IO_CHUNK ? MH_IO_CHUNK : len - done;
        ssize_t n;
        do n = pread(io->fd, p + done, chunk, (off_t)(off + done)); while (n < 0 && errno == EINTR);
        if (n < 0) {
            rec(io, label, syscode(), off + done, (uint32_t)chunk, 0);
            return false;
        }
        if ((size_t)n != chunk) {
            rec(io, label, MH_IOERR_SHORT_READ, off + done, (uint32_t)chunk, (uint32_t)n);
            return false;
        }
        done += chunk;
    }
    return true;
}

/* orig: v1_io_write @0x140034f60 */
bool mh_io_write(MhIo *io, uint64_t off, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    size_t done = 0;
    if (!range_ok(off, len)) {
        rec(io, "Posicionamento de escrita", EINVAL, off, (uint32_t)len, 0);
        return false;
    }
    while (done < len) {
        size_t chunk = len - done > MH_IO_CHUNK ? MH_IO_CHUNK : len - done;
        ssize_t n;
        do n = pwrite(io->fd, p + done, chunk, (off_t)(off + done)); while (n < 0 && errno == EINTR);
        if (n < 0) {
            rec(io, "Escrita no destino", syscode(), off + done, (uint32_t)chunk, 0);
            return false;
        }
        if ((size_t)n != chunk) {
            rec(io, "Escrita parcial no destino", MH_IOERR_SHORT_WRITE, off + done, (uint32_t)chunk, (uint32_t)n);
            return false;
        }
        done += chunk;
    }
    return true;
}

/* orig: v1_io_flush.part.0 @0x140034d40 (the FlushFileBuffers call was inlined at call sites) */
bool mh_io_flush(MhIo *io, uint64_t err_offset)
{
    if (fcntl(io->fd, F_FULLFSYNC) == 0) return true;
    if (fsync(io->fd) == 0) return true;
    rec(io, "Sincronizacao do destino", syscode(), err_offset, 0, 0);
    return false;
}

/* orig: io_write_block_verified @0x14001d3f0 */
bool mh_io_write_block_verified(MhIo *io, uint64_t off, const void *block)
{
    uint8_t back[MH_IO_BLOCK];
    if (off % MH_IO_BLOCK != 0) {
        rec(io, "Alinhamento do controle", MH_IOERR_ALIGN, off, MH_IO_BLOCK, 0);
        return false;
    }
    if (!mh_io_write(io, off, block, MH_IO_BLOCK)) return false;
    if (!mh_io_flush(io, off)) return false;
    if (!mh_io_read(io, off, back, MH_IO_BLOCK)) return false;
    if (memcmp(block, back, MH_IO_BLOCK) != 0) {
        rec(io, "Comparacao do controle", MH_IOERR_COMPARE, off, MH_IO_BLOCK, MH_IO_BLOCK);
        return false;
    }
    return true;
}

/* Second half of handle_sync_blocks_verified: re-read everything and compare. */
static bool verify_region(MhIo *io, uint64_t off, const uint8_t *src, uint64_t len, uint8_t *buf)
{
    uint64_t done = 0;
    while (done < len) {
        size_t chunk = len - done > MH_IO_CHUNK ? MH_IO_CHUNK : (size_t)(len - done);
        if (!mh_io_read(io, off + done, buf, chunk)) return false;
        if (memcmp(buf, src + done, chunk) != 0) return false;
        done += chunk;
    }
    return true;
}

/* orig: handle_sync_blocks_verified @0x14001c360 */
bool mh_io_sync_blocks_verified(MhIo *io, uint64_t off, const uint8_t *src, uint64_t len,
                                MhProgress *prog, const char *text, uint64_t *lba_out)
{
    uint8_t *buf;
    unsigned wait_ms;
    if (lba_out) *lba_out = off / MH_IO_BLOCK;
    if (!src) return false;
    buf = malloc(MH_IO_CHUNK);
    if (!buf) return false;
    for (wait_ms = 80; wait_ms != 400; wait_ms += 80) {
        uint64_t done = 0;
        while (done < len) {
            size_t chunk = len - done > MH_IO_CHUNK ? MH_IO_CHUNK : (size_t)(len - done), k;
            if (!mh_io_read(io, off + done, buf, chunk)) {
                if (lba_out) *lba_out = (off + done) / MH_IO_BLOCK;
                goto fail_final;
            }
            for (k = 0; k + MH_IO_BLOCK <= chunk; k += MH_IO_BLOCK) {
                if (memcmp(buf + k, src + done + k, MH_IO_BLOCK) == 0) continue;
                if (lba_out) *lba_out = (off + done + k) / MH_IO_BLOCK;
                if (!mh_io_write_block_verified(io, off + done + k, src + done + k)) goto fail_final;
            }
            done += chunk;
            if (!mh_progress_report(prog, done, len, text)) goto fail_final;
        }
        if (fcntl(io->fd, F_FULLFSYNC) == 0 || fsync(io->fd) == 0) {
            if (verify_region(io, off, src, len, buf)) {
                free(buf);
                return true;
            }
        }
        {
            struct timespec ts = {0, (long)wait_ms * 1000000L};
            nanosleep(&ts, NULL);
        }
    }
fail_final:
    free(buf);
    return false;
}

/* orig: handle_compare_bytes @0x14001c1f0 */
bool mh_io_compare_bytes(MhIo *io, uint64_t off, const uint8_t *src, uint64_t len,
                         MhProgress *prog, const char *text)
{
    uint8_t *buf = malloc(MH_IO_CHUNK);
    uint64_t done = 0;
    if (!buf) return false;
    while (done < len) {
        size_t chunk = len - done > MH_IO_CHUNK ? MH_IO_CHUNK : (size_t)(len - done);
        if (!mh_io_read(io, off + done, buf, chunk) || memcmp(buf, src + done, chunk) != 0) {
            free(buf);
            return false;
        }
        done += chunk;
        if (!mh_progress_report(prog, done, len, text)) {
            free(buf);
            return false;
        }
    }
    free(buf);
    return true;
}

/* orig: read_raw_payload_at @0x14002b940 */
bool mh_io_read_raw_payload(MhIo *io, uint64_t lba512, uint8_t out[2352])
{
    uint8_t slot[MH_SECTOR_SLOT];
    size_t i;
    if (lba512 > UINT64_MAX / MH_IO_BLOCK || !mh_io_read(io, lba512 * MH_IO_BLOCK, slot, sizeof slot))
        return false;
    memcpy(out, slot, MH_SECTOR_RAW);
    for (i = MH_SECTOR_RAW; i < MH_SECTOR_SLOT; i++)
        if (slot[i]) return false;
    return true;
}

/* orig: crc_file_region @0x140012ef0 */
bool mh_io_crc_region(MhIo *io, uint64_t off, uint64_t len, uint32_t *crc)
{
    uint8_t *buf = malloc(0x100000);
    uint32_t c = mh_crc32_begin();
    uint64_t done = 0;
    if (!buf) return false;
    while (done < len) {
        size_t chunk = len - done > 0x100000 ? 0x100000 : (size_t)(len - done);
        if (!mh_io_read(io, off + done, buf, chunk)) {
            free(buf);
            return false;
        }
        c = mh_crc32_update(c, buf, chunk);
        done += chunk;
    }
    free(buf);
    *crc = mh_crc32_end(c);
    return true;
}

/* orig: seek64 @0x14002fff0 */
bool mh_seek64(int fd, uint64_t off) { return off <= INT64_MAX && lseek(fd, (off_t)off, SEEK_SET) >= 0; }

/* orig: read_exact @0x14002b8a0 */
bool mh_read_exact(int fd, void *buf, size_t len)
{
    uint8_t *p = buf;
    size_t done = 0;
    while (done < len) {
        ssize_t n = read(fd, p + done, len - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        done += (size_t)n;
    }
    return true;
}

/* orig: write_exact @0x14003f720 */
bool mh_write_exact(int fd, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    size_t done = 0;
    while (done < len) {
        ssize_t n = write(fd, p + done, len - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        done += (size_t)n;
    }
    return true;
}
