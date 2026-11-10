/* base module: MhIo, whole-file helpers and file system predicates (temp files only). */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mh/base.h"

static int g_checks;
#define CHECK(c)                                                                       \
    do {                                                                               \
        g_checks++;                                                                    \
        if (!(c)) {                                                                    \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c);      \
            abort();                                                                   \
        }                                                                              \
    } while (0)
#define CHECK_STR(a, b)                                                                \
    do {                                                                               \
        g_checks++;                                                                    \
        if (strcmp((a), (b)) != 0) {                                                   \
            fprintf(stderr, "%s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); \
            abort();                                                                   \
        }                                                                              \
    } while (0)

static char g_dir[] = "/tmp/mh_base_io_XXXXXX";
static char *P(const char *name)
{
    static char buf[4][1100];
    static int k;
    k = (k + 1) & 3;
    snprintf(buf[k], sizeof buf[k], "%s/%s", g_dir, name);
    return buf[k];
}

static void fill(uint8_t *p, size_t n, uint32_t seed)
{
    size_t i;
    for (i = 0; i < n; i++) {
        seed = seed * 1664525u + 1013904223u;
        p[i] = (uint8_t)(seed >> 24);
    }
}

static void test_files(void)
{
    uint8_t *d, *r;
    uint64_t n = 99, sz;
    char big[2000];
    CHECK(mh_ensure_dir(P("a/b/c")));
    CHECK(mh_dir_exists(P("a")) && mh_dir_exists(P("a/b/c")) && mh_ensure_dir(P("a/b/c")));
    CHECK(mh_ensure_dir(P("a/b2/")));
    CHECK(mh_dir_exists(P("a/b2")));
    CHECK(!mh_ensure_dir("") && !mh_ensure_dir(NULL));
    CHECK(mh_ensure_dir("/tmp"));

    CHECK(mh_write_entire_file(P("empty.bin"), "", 0));
    CHECK(mh_file_size64(P("empty.bin"), &sz) && sz == 0);
    CHECK(mh_read_entire_file(P("empty.bin"), &r, &n) && n == 0 && r && r[0] == 0 && r[1] == 0);
    free(r);

    d = malloc(3 * 1048576 + 17);
    fill(d, 3 * 1048576 + 17, 1);
    CHECK(mh_write_entire_file(P("a/data.bin"), d, 3 * 1048576 + 17));
    CHECK(mh_file_size64(P("a/data.bin"), &sz) && sz == 3 * 1048576 + 17);
    CHECK(mh_read_entire_file(P("a/data.bin"), &r, &n) && n == sz && !memcmp(r, d, n));
    CHECK(r[n] == 0 && r[n + 1] == 0); /* two terminating zero bytes */
    free(r);
    free(d);

    CHECK(mh_write_entire_file(P("t.txt"), "hello", 5));
    CHECK(mh_write_entire_file(P("t.txt"), "hi", 2)); /* truncates */
    CHECK(mh_read_entire_file(P("t.txt"), &r, &n) && n == 2 && !strcmp((char *)r, "hi"));
    free(r);

    CHECK(!mh_read_entire_file(P("missing"), &r, &n) && r == NULL && n == 0);
    CHECK(!mh_read_entire_file(P("a"), &r, &n) && r == NULL); /* a directory */
    CHECK(!mh_file_size64(P("a"), &sz) && !mh_file_size64(P("missing"), &sz));
    CHECK(!mh_write_entire_file(P("no/such/dir/f"), "x", 1));

    CHECK(mh_path_exists(P("t.txt")) && mh_path_exists(P("a")) && !mh_path_exists(P("zz")));
    CHECK(mh_file_exists(P("t.txt")) && !mh_file_exists(P("a")) && !mh_file_exists(P("zz")));
    CHECK(mh_dir_exists(P("a")) && !mh_dir_exists(P("t.txt")) && !mh_dir_exists(P("zz")));
    CHECK(!mh_path_exists(NULL) && !mh_dir_exists(NULL) && !mh_file_exists(NULL));
    CHECK(symlink(P("t.txt"), P("lnk")) == 0 && mh_file_exists(P("lnk")) && mh_path_exists(P("lnk")));
    CHECK(!mh_ensure_dir(P("t.txt")) && !mh_ensure_dir(P("t.txt/sub")));

    CHECK(mh_copy_file_safe(P("t.txt"), P("copy.txt")));
    CHECK(mh_read_entire_file(P("copy.txt"), &r, &n) && n == 2 && !memcmp(r, "hi", 2));
    free(r);
    CHECK(mh_write_entire_file(P("copy.txt"), "OLD CONTENT LONGER", 18));
    CHECK(mh_copy_file_safe(P("t.txt"), P("copy.txt"))); /* overwrites and shortens */
    CHECK(mh_file_size64(P("copy.txt"), &sz) && sz == 2);
    CHECK(!mh_copy_file_safe(P("t.txt"), P("t.txt"))); /* same file */
    CHECK(!mh_copy_file_safe(P("t.txt"), P("lnk"))); /* same file through a link */
    CHECK(mh_file_size64(P("t.txt"), &sz) && sz == 2);
    CHECK(!mh_copy_file_safe(P("missing"), P("x.txt")) && !mh_path_exists(P("x.txt")));
    CHECK(!mh_copy_file_safe(P("t.txt"), P("no/dir/x")));

    d = malloc(5 * 1048576 + 1);
    fill(d, 5 * 1048576 + 1, 9);
    CHECK(mh_write_entire_file(P("big.bin"), d, 5 * 1048576 + 1));
    CHECK(mh_copy_file_safe(P("big.bin"), P("big2.bin")));
    CHECK(mh_read_entire_file(P("big2.bin"), &r, &n) && n == 5 * 1048576 + 1 && !memcmp(r, d, n));
    free(r);
    free(d);

    memset(big, 'x', sizeof big - 1);
    big[sizeof big - 1] = 0;
    CHECK(!mh_ensure_dir(big) || 1); /* must not overflow, whatever the result */
}

static void test_fd_helpers(void)
{
    int fd = open(P("fd.bin"), O_RDWR | O_CREAT | O_TRUNC, 0644);
    uint8_t a[100], b[100];
    CHECK(fd >= 0);
    fill(a, sizeof a, 5);
    CHECK(mh_write_exact(fd, a, sizeof a) && mh_write_exact(fd, a, 0));
    CHECK(mh_seek64(fd, 10));
    CHECK(mh_read_exact(fd, b, 50) && !memcmp(b, a + 10, 50));
    CHECK(!mh_read_exact(fd, b, 100)); /* hits EOF after 40 more bytes */
    CHECK(mh_seek64(fd, 1000) && !mh_read_exact(fd, b, 1));
    CHECK(mh_read_exact(fd, b, 0));
    CHECK(!mh_seek64(fd, UINT64_C(1) << 63));
    close(fd);
    CHECK(!mh_write_exact(-1, a, 1) && !mh_read_exact(-1, a, 1));
}

static int g_prog_calls;
static uint64_t g_prog_last_done;
static void on_prog(void *user, uint64_t done, uint64_t total, const char *text)
{
    (void)user;
    g_prog_calls++;
    g_prog_last_done = done;
    CHECK(total > 0 && done <= total && text && !strcmp(text, "txt"));
}

typedef struct { MhProgress *p; int cancel_at; } CancelCtx;
static void cancel_cb(void *user, uint64_t done, uint64_t total, const char *text)
{
    (void)done; (void)total; (void)text;
    CancelCtx *c = user;
    if (++g_prog_calls == c->cancel_at) mh_progress_request_cancel(c->p);
}

static void test_io(void)
{
    enum { SZ = 600 * 1024 }; /* more than two 256 KiB pieces */
    uint8_t *a = malloc(SZ), *b = malloc(SZ), *r = malloc(SZ);
    MhIo io, io2;
    MhIoErr shared;
    MhWbuf w;
    fill(a, SZ, 11);
    fill(b, SZ, 12);

    CHECK(!mh_io_open(&io, P("nonexistent"), MH_IO_READ, false, NULL) && io.fd == -1);
    CHECK(mh_io_open(&io, P("img.bin"), MH_IO_CREATE, true, NULL));
    CHECK(mh_io_write(&io, 0, a, SZ));
    CHECK(mh_io_flush(&io, 0));
    {
        uint64_t sz;
        CHECK(mh_io_size(&io, &sz) && sz == SZ);
    }
    CHECK(mh_io_read(&io, 0, r, SZ) && !memcmp(r, a, SZ));
    CHECK(mh_io_read(&io, 12345, r, 100000) && !memcmp(r, a + 12345, 100000));
    CHECK(mh_io_read(&io, 5, r, 0) && mh_io_write(&io, 5, r, 0));
    CHECK(!io.err->set);

    /* short read: 300 bytes available after SZ-300; the second 256 KiB piece comes up short */
    CHECK(!mh_io_read(&io, SZ - 300, r, 1000));
    CHECK(io.err->set && io.err->code == MH_IOERR_SHORT_READ && io.err->offset == SZ - 300);
    CHECK_STR(io.err->what, "Leitura do destino");
    CHECK(io.err->requested == 1000 && io.err->transferred == 300);
    /* first error wins */
    CHECK(!mh_io_read(&io, SZ + 10, r, 10));
    CHECK(io.err->offset == SZ - 300 && io.err->transferred == 300);
    CHECK(mh_ioerr_append((mh_wbuf_init(&w), &w), io.err));
    CHECK_STR(mh_wbuf_cstr(&w),
              "\r\nLeitura do destino. Erro Windows: 30\r\nOffset: 614100 bytes; LBA512: 1199\r\n"
              "Solicitado: 1000; transferido confirmado: 300 bytes.");
    mh_wbuf_free(&w);
    mh_ioerr_clear(io.err);
    CHECK(!io.err->set);
    {
        MhWbuf w2;
        mh_wbuf_init(&w2);
        CHECK(mh_ioerr_append(&w2, io.err) && w2.len == 0);
    }

    /* the second piece of a long read is the one that fails: offset points at it */
    CHECK(!mh_io_read(&io, 0, r, SZ + 4096));
    CHECK(io.err->offset == 2 * MH_IO_CHUNK && io.err->requested == SZ + 4096 - 2 * MH_IO_CHUNK && io.err->transferred == SZ - 2 * MH_IO_CHUNK);
    mh_ioerr_clear(io.err);

    /* read-only descriptor: label "origem", write fails with errno */
    memset(&shared, 0, sizeof shared);
    CHECK(mh_io_open(&io2, P("img.bin"), MH_IO_READ, false, &shared));
    CHECK(io2.err == &shared);
    CHECK(!mh_io_read(&io2, SZ, r, 1));
    CHECK_STR(shared.what, "Leitura da origem");
    mh_ioerr_clear(&shared);
    CHECK(!mh_io_write(&io2, 0, a, 10));
    CHECK_STR(shared.what, "Escrita no destino");
    CHECK(shared.code == EBADF && shared.offset == 0 && shared.requested == 10 && shared.transferred == 0);
    mh_ioerr_clear(&shared);
    CHECK(!mh_io_read(&io2, UINT64_C(1) << 63, r, 1));
    CHECK_STR(shared.what, "Posicionamento de leitura");
    mh_ioerr_clear(&shared);
    CHECK(!mh_io_write(&io2, UINT64_C(1) << 63, a, 1));
    CHECK_STR(shared.what, "Posicionamento de escrita");
    mh_io_close(&io2);
    mh_io_close(&io2);
    mh_ioerr_clear(&shared);

    /* verified single block */
    CHECK(!mh_io_write_block_verified(&io, 100, a));
    CHECK_STR(io.err->what, "Alinhamento do controle");
    CHECK(io.err->code == MH_IOERR_ALIGN && io.err->offset == 100 && io.err->requested == 512 && io.err->transferred == 0);
    mh_ioerr_clear(io.err);
    CHECK(mh_io_write_block_verified(&io, 4096, b) && !io.err->set);
    CHECK(mh_io_read(&io, 4096, r, 512) && !memcmp(r, b, 512));
    CHECK(mh_io_write(&io, 0, a, SZ));

    /* sync: make [8192, 8192+512*1000) equal to b[..] touching only differing blocks */
    {
        uint8_t *want = malloc(512 * 1000);
        uint64_t lba = 0;
        MhProgress pr;
        uint8_t keep[512];
        memcpy(want, a + 8192, 512 * 1000);
        memcpy(want + 512 * 3, b, 512);          /* block 3 differs */
        memcpy(want + 512 * 600, b + 512, 512);  /* block 600 differs (second piece) */
        memcpy(keep, a + 8192 + 512, 512);
        mh_progress_init(&pr, on_prog, &pr);
        g_prog_calls = 0;
        CHECK(mh_io_sync_blocks_verified(&io, 8192, want, 512 * 1000, &pr, "txt", &lba));
        CHECK(g_prog_calls == 2 && g_prog_last_done == 512 * 1000);
        CHECK(lba == (8192 + 512 * 600) / 512); /* last block rewritten */
        CHECK(mh_io_read(&io, 8192, r, 512 * 1000) && !memcmp(r, want, 512 * 1000));
        CHECK(!memcmp(r + 512, keep, 512));
        CHECK(mh_io_read(&io, 0, r, 8192) && !memcmp(r, a, 8192));                       /* outside untouched */
        CHECK(mh_io_read(&io, 8192 + 512 * 1000, r, 4096) && !memcmp(r, a + 8192 + 512 * 1000, 4096));
        CHECK(!io.err->set);
        /* NULL progress, already equal: nothing to write */
        CHECK(mh_io_sync_blocks_verified(&io, 8192, want, 512 * 1000, NULL, NULL, NULL));
        CHECK(mh_io_sync_blocks_verified(&io, 8192, want, 0, NULL, NULL, NULL));
        CHECK(!mh_io_sync_blocks_verified(&io, 8192, NULL, 512, NULL, NULL, &lba));

        /* cancel from the progress callback after the first piece */
        {
            CancelCtx cc;
            memcpy(want + 512 * 3, a + 8192 + 512 * 3, 512); /* make block 3 differ from the device again */
            cc.p = &pr;
            cc.cancel_at = 1;
            mh_progress_init(&pr, cancel_cb, &cc);
            g_prog_calls = 0;
            CHECK(!mh_io_sync_blocks_verified(&io, 8192, want, 512 * 1000, &pr, "x", NULL));
            CHECK(g_prog_calls == 1 && mh_progress_cancelled(&pr) && !io.err->set);
            /* the piece that was in flight when cancel was seen has been completed */
            CHECK(mh_io_read(&io, 8192 + 512 * 3, r, 512) && !memcmp(r, a + 8192 + 512 * 3, 512));
        }
        free(want);
    }

    /* a region past the end of the image cannot be verified: read fails and lba_out points there */
    {
        uint64_t lba = 0;
        uint8_t blk[1024] = {0};
        CHECK(!mh_io_sync_blocks_verified(&io, SZ + 4096, blk, 1024, NULL, NULL, &lba));
        CHECK(lba == (SZ + 4096) / 512 && io.err->set && io.err->code == MH_IOERR_SHORT_READ);
        mh_ioerr_clear(io.err);
    }

    /* compare */
    CHECK(mh_io_write(&io, 0, a, SZ));
    {
        MhProgress pr;
        mh_progress_init(&pr, on_prog, &pr);
        g_prog_calls = 0;
        CHECK(mh_io_compare_bytes(&io, 0, a, SZ, &pr, "txt"));
        CHECK(g_prog_calls == 3 && g_prog_last_done == SZ);
        CHECK(mh_io_compare_bytes(&io, 77, a + 77, 5000, NULL, NULL));
        CHECK(!mh_io_compare_bytes(&io, 0, b, SZ, NULL, NULL));
        a[SZ - 1] ^= 1;
        CHECK(!mh_io_compare_bytes(&io, 0, a, SZ, NULL, NULL));
        a[SZ - 1] ^= 1;
        CHECK(mh_io_compare_bytes(&io, 0, a, 0, NULL, NULL));
        {
            CancelCtx cc;
            cc.p = &pr;
            cc.cancel_at = 2;
            mh_progress_init(&pr, cancel_cb, &cc);
            g_prog_calls = 0;
            CHECK(!mh_io_compare_bytes(&io, 0, a, SZ, &pr, "t") && g_prog_calls == 2);
        }
    }

    /* crc of a region */
    {
        uint32_t crc = 0;
        CHECK(mh_io_crc_region(&io, 1000, 400000, &crc) && crc == mh_crc32(a + 1000, 400000));
        CHECK(mh_io_crc_region(&io, 0, 0, &crc) && crc == 0);
        crc = 7;
        CHECK(!mh_io_crc_region(&io, SZ - 10, 100, &crc) && crc == 7);
        mh_ioerr_clear(io.err);
    }

    /* raw payload slot: 2352 bytes plus 208 zero bytes, addressed in 512-byte blocks */
    {
        uint8_t slot[MH_SECTOR_SLOT], out[2352];
        fill(slot, MH_SECTOR_RAW, 3);
        memset(slot + MH_SECTOR_RAW, 0, 208);
        CHECK(mh_io_write(&io, 7 * 512, slot, sizeof slot));
        CHECK(mh_io_read_raw_payload(&io, 7, out) && !memcmp(out, slot, 2352));
        slot[MH_SECTOR_SLOT - 1] = 1;
        CHECK(mh_io_write(&io, 7 * 512, slot, sizeof slot));
        CHECK(!mh_io_read_raw_payload(&io, 7, out));
        CHECK(!mh_io_read_raw_payload(&io, UINT64_MAX / 100, out));
        mh_ioerr_clear(io.err);
    }
    mh_io_close(&io);

    /* attach/shared error record across two handles */
    {
        int fd = open(P("img.bin"), O_RDONLY);
        MhIo x;
        mh_io_attach(&x, fd, false, &shared);
        CHECK(x.err == &shared && !mh_io_read(&x, SZ, r, 1) && shared.set);
        mh_io_close(&x);
    }
    free(a);
    free(b);
    free(r);
}

int main(void)
{
    if (!mkdtemp(g_dir)) { perror("mkdtemp"); return 1; }
    test_files();
    test_fd_helpers();
    test_io();
    {
        char cmd[1100];
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", g_dir);
        CHECK(system(cmd) == 0);
    }
    printf("test_base_io: %d checks passed\n", g_checks);
    return 0;
}
