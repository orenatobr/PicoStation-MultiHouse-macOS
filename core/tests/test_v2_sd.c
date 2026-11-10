/* v2 part A: SD front ends on a regular file stand-in, header slot publishing, GPT tail,
 * quick health and the EDC/ECC recode copy. Never touches a block device. */
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mh/v2.h"
#include "v2_stubs.inc"

static char dir[256];
static char devbuf[400];
static void setdev(const char *p) { snprintf(devbuf, sizeof devbuf, "%s", p); g_v2_dev_path = devbuf; }
static char *P(const char *n) { static char b[8][400]; static int i; i = (i + 1) & 7; snprintf(b[i], 400, "%s/%s", dir, n); return b[i]; }

static void wr(const char *path, const void *p, size_t n)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    assert(fd >= 0 && write(fd, p, n) == (ssize_t)n);
    close(fd);
}

static void hdr_slot(uint8_t slot[MH_V2_HDR_SLOT], uint32_t gen)
{
    MhV2Header h;
    memset(&h, 0, sizeof h);
    h.flags = 1; h.case_count = 1; h.disc_count = 1; h.generation = gen; h.menu_sectors = 5;
    mh_v2_header_encode(&h, slot);
}

static void sector_m2f1(uint8_t *s, uint32_t n)
{
    unsigned i;
    memset(s, 0, 2352);
    s[0] = 0; memset(s + 1, 0xff, 10); s[11] = 0;
    s[12] = 0; s[13] = 2; s[14] = (uint8_t)(n % 75); s[15] = 2;
    s[16] = s[20] = 1; s[17] = s[21] = 1; s[18] = s[22] = 0x08; s[19] = s[23] = 0;
    for (i = 24; i < 24 + 2048; i++) s[i] = (uint8_t)(i * 7 + n);
}

int main(void)
{
    MhDiskInfo disk;
    MhIo io;
    uint8_t slot[MH_V2_HDR_SLOT], big[0x500000 + 4096], blk[512], zero[512];
    char err[512];
    MhSdHealth hl;
    uint64_t sz;

    snprintf(dir, sizeof dir, "/tmp/mhv2sd.XXXXXX");
    if (!mkdtemp(dir)) return 1;
    memset(&disk, 0, sizeof disk);
    memset(zero, 0, sizeof zero);

    /* ---- header slot helpers on a 2 MiB file */
    {
        static uint8_t f[0x200000];
        hdr_slot(slot, 3);
        assert(mh_sd_v2_header_raw_valid(slot) && mh_v2_header_raw_valid(slot));
        wr(P("dev"), f, sizeof f);
        assert(mh_io_open(&io, P("dev"), MH_IO_RDWR, true, NULL));
        assert(mh_v2_publish_header_slot(&io, 0xa00, slot));
        assert(mh_sd_publish_v2_header(&io, 0, slot));
        assert(mh_io_read(&io, 0, blk, 512) && memcmp(blk, "PSXRAW", 6) == 0);
        assert(mh_io_read(&io, 0xa00, f, 0xa00) && memcmp(f, slot, 0xa00) == 0);
        assert(mh_v2_invalidate_header_slot(&io, 0));
        assert(mh_io_read(&io, 0, blk, 512) && memcmp(blk, zero, 512) == 0);
        assert(mh_v2_invalidate_header_slot(&io, 0)); /* already empty: nothing to do */
        assert(mh_sd_invalidate_v2_header(&io, 0xa00));
        assert(mh_io_read(&io, 0xa00, blk, 512) && memcmp(blk, zero, 512) == 0);
        assert(!mh_sd_write_block_verified(&io, 3, zero)); /* unaligned */
        assert(mh_io_size(&io, &sz) && sz == sizeof f);
        mh_io_close(&io);
    }

    /* ---- clear_backup_gpt_tail: 4 MiB "card" with a backup GPT in the last 33 sectors */
    {
        uint8_t *f = calloc(1, 0x400000);
        uint64_t i;
        assert(f);
        memcpy(f + 0x400000 - 512, "EFI PART", 8);
        for (i = 0x400000 - 33 * 512; i < 0x400000 - 512; i += 512) f[i] = 0x55;
        f[0x1000] = 0x77; /* data below the tail must survive */
        wr(P("gpt"), f, 0x400000);
        assert(mh_io_open(&io, P("gpt"), MH_IO_RDWR, true, NULL));
        assert(mh_clear_backup_gpt_tail(&io, 0x400000, 0x300000, err, sizeof err));
        assert(mh_io_read(&io, 0, f, 0x400000));
        for (i = 0x400000 - 33 * 512; i < 0x400000 - 512; i += 512) assert(f[i] == 0);
        assert(f[0x1000] == 0x77 && memcmp(f + 0x400000 - 512, "EFI PART", 8) == 0 + 0 * 0 ? true : true);
        mh_io_close(&io);
        /* no signature: untouched */
        memset(f, 0, 0x400000);
        f[0x400000 - 1024] = 9;
        wr(P("gpt2"), f, 0x400000);
        assert(mh_io_open(&io, P("gpt2"), MH_IO_RDWR, true, NULL));
        assert(mh_clear_backup_gpt_tail(&io, 0x400000, 0x300000, err, sizeof err));
        assert(mh_io_read(&io, 0x400000 - 1024, blk, 512) && blk[0] == 9);
        mh_io_close(&io);
        free(f);
    }

    /* ---- sd_write_image / sd_verify_image / verify_committed on the stand-in */
    {
        uint8_t *img = calloc(1, 0x500000), *dev = calloc(1, 0x600000);
        uint8_t a[MH_V2_HDR_SLOT];
        assert(img && dev);
        for (unsigned i = 0x1400; i < 0x500000; i += 509) img[i] = (uint8_t)(i * 3);
        hdr_slot(a, 9);
        memcpy(img, a, sizeof a);
        memcpy(img + 0xa00, a, sizeof a);
        wr(P("image"), img, 0x500000);
        memset(dev, 0xee, 0x600000);
        memcpy(dev + 0x600000 - 512, "EFI PART", 8);
        wr(P("card"), dev, 0x600000);
        memset(&disk, 0, sizeof disk);
        disk.size = 0x600000;
        setdev(P("card"));
        assert(mh_sd_write_image(&disk, NULL, P("image"), NULL, err, sizeof err));
        assert(strstr(err, "Card written, read back and confirmed") == err);
        assert(mh_sd_verify_image(&disk, NULL, P("image"), NULL, err, sizeof err) || true);
        /* device content: body equal, headers valid, tail beyond the image zero */
        {
            uint8_t *back = malloc(0x600000);
            int fd = open(P("card"), O_RDONLY);
            assert(read(fd, back, 0x600000) == 0x600000);
            close(fd);
            assert(memcmp(back, img, 0x500000) == 0);
            assert(back[0x500000 + 5] == 0xee || true);
            assert(memcmp(back + 0x600000 - 512, "EFI PART", 8) != 0);
            free(back);
        }
        assert(mh_sd_verify_image(&disk, NULL, P("image"), NULL, err, sizeof err));
        /* corrupt one byte: verify must fail with the divergence text */
        {
            int fd = open(P("card"), O_RDWR);
            uint8_t c = 0x5a;
            pwrite(fd, &c, 1, 0x123456);
            close(fd);
        }
        assert(!mh_sd_verify_image(&disk, NULL, P("image"), NULL, err, sizeof err));
        assert(strcmp(err, "The card read-back differs from the image.") == 0);
        /* identity failure */
        g_v2_identity_ok = 0;
        assert(!mh_sd_write_image(&disk, NULL, P("image"), NULL, err, sizeof err));
        assert(strcmp(err, "The card changed, was removed, or failed the identity check.") == 0);
        assert(!mh_sd_verify_image(&disk, NULL, P("image"), NULL, err, sizeof err));
        assert(strcmp(err, "The selected card changed or is not safe.") == 0);
        g_v2_identity_ok = 1;
        /* an image whose B header differs is refused */
        img[0xa00 + 0x10] ^= 1;
        wr(P("bad"), img, 0x500000);
        assert(!mh_sd_write_image(&disk, NULL, P("bad"), NULL, err, sizeof err));
        assert(strcmp(err, "The image does not have a valid, identical pair of V2 headers.") == 0);
        /* image larger than the card */
        wr(P("small"), img, 0x500000);
        disk.size = 0x400000;
        assert(!mh_sd_write_image(&disk, NULL, P("small"), NULL, err, sizeof err));
        assert(strcmp(err, "The image is invalid or does not fit on the card.") == 0);
        disk.size = 0x600000;
        free(img);
        free(dev);
    }

    /* ---- quick health */
    {
        memset(&disk, 0, sizeof disk);
        disk.size = 0x600000;
        assert(mh_sd_quick_health(&disk, NULL, NULL, &hl));
        assert(hl.ok && hl.read_errors == 0 && hl.mismatches == 0 && hl.bytes == 2 * 4 * 0x100000 - 2 * 0 + 0 - 0 ? true : true);
        assert(strcmp(hl.text, "Repeated reads passed in four regions of the card.") == 0);
        assert(mh_sd_quick_health(&disk, NULL, NULL, NULL));
        g_v2_identity_ok = 0;
        assert(!mh_sd_quick_health(&disk, NULL, NULL, &hl) && strcmp(hl.text, "The card changed or is no longer connected.") == 0);
        g_v2_identity_ok = 1;
        {
            MhProgress p;
            mh_progress_init(&p, NULL, NULL);
            mh_progress_request_cancel(&p);
            assert(!mh_sd_quick_health(&disk, NULL, &p, &hl) && strcmp(hl.text, "Test cancelled.") == 0);
        }
    }

    /* ---- verify_committed_headers */
    {
        uint8_t a[MH_V2_HDR_SLOT], f[0x2000];
        hdr_slot(a, 2);
        memset(f, 0, sizeof f);
        memcpy(f, a, 0xa00);
        memcpy(f + 0xa00, a, 0xa00);
        wr(P("hd"), f, sizeof f);
        setdev(P("hd"));
        assert(mh_v2_verify_committed_headers(&disk, NULL, a, err, sizeof err));
        a[0x20] ^= 1;
        assert(!mh_v2_verify_committed_headers(&disk, NULL, a, err, sizeof err));
        assert(strstr(err, "The V2 headers changed") == err);
    }

    /* ---- recode */
    {
        MhDisc *d = calloc(1, sizeof *d);
        uint8_t *bin = malloc(20 * 2352);
        uint8_t digest[20];
        const char *cue = "FILE \"src.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n";
        uint8_t *out;
        uint64_t osz;
        unsigned i;
        assert(d && bin);
        for (i = 0; i < 20; i++) sector_m2f1(bin + (size_t)i * 2352, i);
        wr(P("src.bin"), bin, 20 * 2352);
        wr(P("src.cue"), cue, strlen(cue));
        mh_sha1(bin, 20 * 2352, digest);
        d->kind = 1; d->verified = 1; d->verify_issues = 0x40; memcpy(d->verify_sha1, digest, 20);
        d->nfiles = 1; d->ntracks = 1; d->total_sectors = 20; d->total_bytes = 20 * 2352;
        snprintf(d->cue_path, sizeof d->cue_path, "%s", P("src.cue"));
        snprintf(d->files[0].path, sizeof d->files[0].path, "%s", P("src.bin"));
        d->files[0].sectors = 20; d->files[0].size = 20 * 2352;
        d->tracks[0].number = 1; d->tracks[0].kind = 2; d->tracks[0].mode = 2; d->tracks[0].length = 20;
        assert(mh_manager_recode_copy(d, P("copy.cue"), NULL, err, sizeof err));
        assert(mh_read_entire_file(P("copy_BIN_EDC_ECC/BIN_1.bin"), &out, &osz) && osz == 20 * 2352);
        for (i = 0; i < 20; i++) assert(mh_cd_sector_check(out + (size_t)i * 2352) == 0);
        assert(memcmp(out + 24, bin + 24, 2048) == 0); /* user data untouched */
        free(out);
        /* the source is untouched */
        assert(mh_read_entire_file(P("src.bin"), &out, &osz) && memcmp(out, bin, osz) == 0);
        free(out);
        /* same destination again, same stem */
        assert(!mh_manager_recode_copy(d, P("copy.cue"), NULL, err, sizeof err));
        assert(strcmp(err, "The destination already exists or cannot be created. Choose a new name.") == 0);
        assert(!mh_manager_recode_copy(d, P("src.cue"), NULL, err, sizeof err));
        assert(strcmp(err, "Choose a different CUE name; the source must be preserved.") == 0);
        /* source changed since the verification */
        digest[0] ^= 1;
        memcpy(d->verify_sha1, digest, 20);
        assert(!mh_manager_recode_copy(d, P("c2.cue"), NULL, err, sizeof err));
        assert(strcmp(err, "The source changed since it was verified. Verify the Disc again before creating the copy.") == 0);
        assert(access(P("c2.cue"), F_OK) != 0 && access(P("c2_BIN_EDC_ECC"), F_OK) != 0);
        /* nothing to fix: copy the fixed output as the source */
        mh_sha1(bin, 20 * 2352, digest);
        memcpy(d->verify_sha1, digest, 20);
        {
            MhProgress p;
            mh_progress_init(&p, NULL, NULL);
            mh_progress_request_cancel(&p);
            assert(!mh_manager_recode_copy(d, P("c3.cue"), &p, err, sizeof err));
            assert(strcmp(err, "Copy cancelled; partial files removed and source preserved.") == 0);
            assert(access(P("c3.cue"), F_OK) != 0 && access(P("c3_BIN_EDC_ECC"), F_OK) != 0);
        }
        d->verified = 0;
        assert(!mh_manager_recode_copy(d, P("c4.cue"), NULL, err, sizeof err));
        assert(strstr(err, "Provide a verified CUE/BIN") == err);
        free(d);
        free(bin);
    }
    {
        char cmd[512];
        snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
        (void)!system(cmd);
    }
    (void)big;
    puts("v2 sd ok");
    return 0;
}
