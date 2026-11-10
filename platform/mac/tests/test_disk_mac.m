/* Apple-specific disk layer tests. Build and run with tools/test-disk.sh.
 *
 * Always run (read only, never writes to any device):
 *   - the boot disk and every external disk >130 GB are never offered or openable
 *   - authopen plumbing (SCM_RIGHTS) against a harmless file, skipped if it would prompt
 * With MH_DISK_TEST_IMAGES=1 (set by tools/test-disk.sh --images): creates RAM-free 64 MB disk
 * images with hdiutil, attaches them with -nomount (and one formatted FAT image), enumerates
 * with MH_DISK_TEST_ALLOW_IMAGES semantics, writes and verifies blocks, and detaches. The only
 * device ever written is one whose node was returned by hdiutil for an image file created by
 * this test (path and image-path checked), matched by a strict regex. */
#import <Foundation/Foundation.h>

#include <assert.h>
#include <fcntl.h>
#include <regex.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mh/base.h"
#include "mh/blockdev.h"
#include "mh_disk_mac.h"

#define CHECK(c)                                                                       \
    do {                                                                               \
        if (!(c)) {                                                                    \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);               \
            cleanup();                                                                 \
            exit(1);                                                                   \
        }                                                                              \
    } while (0)

static char g_attached[4][32]; /* devices we attached and must detach */
static int g_nattached;
static NSString *g_tmpdir;

static NSString *run(NSArray<NSString *> *argv, int *status) {
    NSTask *t = [[NSTask alloc] init];
    t.executableURL = [NSURL fileURLWithPath:argv[0]];
    t.arguments = [argv subarrayWithRange:NSMakeRange(1, argv.count - 1)];
    NSPipe *p = [NSPipe pipe];
    t.standardOutput = p;
    t.standardError = [NSPipe pipe];
    NSError *e = nil;
    if (![t launchAndReturnError:&e]) {
        if (status) *status = -1;
        return @"";
    }
    NSData *d = [p.fileHandleForReading readDataToEndOfFile];
    [t waitUntilExit];
    if (status) *status = t.terminationStatus;
    return [[NSString alloc] initWithData:d encoding:NSUTF8StringEncoding] ?: @"";
}

static bool strict_disk_node(const char *s) {
    regex_t re;
    regcomp(&re, "^/dev/disk[0-9]+$", REG_EXTENDED | REG_NOSUB);
    bool ok = regexec(&re, s, 0, NULL, 0) == 0;
    regfree(&re);
    return ok;
}

static void detach_all(void) {
    for (int i = 0; i < g_nattached; i++) {
        if (!g_attached[i][0] || !strict_disk_node(g_attached[i])) continue;
        int st;
        run(@[ @"/usr/bin/hdiutil", @"detach", @(g_attached[i]) ], &st);
        if (st != 0) run(@[ @"/usr/bin/hdiutil", @"detach", @"-force", @(g_attached[i]) ], &st);
        g_attached[i][0] = 0;
    }
    g_nattached = 0;
}

static void cleanup(void) {
    /* a failed CHECK may leave a raw device open, which makes hdiutil detach refuse */
    for (int fd = 3; fd < 64; fd++) {
        struct stat st;
        if (fstat(fd, &st) == 0 && S_ISCHR(st.st_mode)) close(fd);
    }
    detach_all();
    if (g_tmpdir) [[NSFileManager defaultManager] removeItemAtPath:g_tmpdir error:nil];
    g_tmpdir = nil;
}

/* Creates the image, attaches it, returns the validated whole-disk node (empty on failure).
 * fs == nil: -layout NONE with no file system and -nomount. */
static bool make_image(const char *name, const char *fs, char dev[32], char mountpoint[256]) {
    NSString *base = [g_tmpdir stringByAppendingPathComponent:@(name)];
    NSString *dmg = [base stringByAppendingString:@".dmg"];
    int st;
    NSMutableArray *a = [@[ @"/usr/bin/hdiutil", @"create", @"-size", @"64m", @"-layout", @"NONE",
                            @"-type", @"UDIF" ] mutableCopy];
    if (fs) [a addObjectsFromArray:@[ @"-fs", @(fs), @"-volname", @"MHTEST" ]];
    [a addObject:base];
    run(a, &st);
    if (st != 0 || ![[NSFileManager defaultManager] fileExistsAtPath:dmg]) return false;

    NSMutableArray *b = [@[ @"/usr/bin/hdiutil", @"attach", @"-plist" ] mutableCopy];
    if (!fs) [b addObject:@"-nomount"];
    [b addObject:dmg];
    NSString *out = run(b, &st);
    if (st != 0) return false;
    NSDictionary *pl = [NSPropertyListSerialization
        propertyListWithData:[out dataUsingEncoding:NSUTF8StringEncoding]
                     options:0
                      format:NULL
                       error:NULL];
    NSArray *ents = pl[@"system-entities"];
    const char *found = NULL;
    int nfound = 0;
    mountpoint[0] = 0;
    for (NSDictionary *e in ents) {
        NSString *de = e[@"dev-entry"];
        if (de && strict_disk_node(de.UTF8String)) {
            found = de.UTF8String;
            nfound++;
            if (e[@"mount-point"]) strlcpy(mountpoint, [e[@"mount-point"] UTF8String], 256);
        }
    }
    if (nfound != 1 || !found || !strict_disk_node(found)) {
        /* detach whatever we can recognise, then fail */
        for (NSDictionary *e in ents) {
            NSString *de = e[@"dev-entry"];
            if (de && strict_disk_node(de.UTF8String)) run(@[ @"/usr/bin/hdiutil", @"detach", @"-force", de ], &st);
        }
        return false;
    }
    strlcpy(dev, found, 32);
    strlcpy(g_attached[g_nattached++], found, 32);

    /* cross-check with hdiutil info: this device must belong to OUR image file */
    NSString *info = run(@[ @"/usr/bin/hdiutil", @"info", @"-plist" ], &st);
    NSDictionary *ip = [NSPropertyListSerialization
        propertyListWithData:[info dataUsingEncoding:NSUTF8StringEncoding]
                     options:0
                      format:NULL
                       error:NULL];
    bool mine = false;
    for (NSDictionary *img in ip[@"images"]) {
        NSString *path = img[@"image-path"];
        for (NSDictionary *e in img[@"system-entities"])
            if ([e[@"dev-entry"] isEqualToString:@(dev)] && [path hasPrefix:g_tmpdir] &&
                [path isEqualToString:dmg])
                mine = true;
    }
    return mine;
}

/* APFS image with the default GPT layout: the whole disk `phys` holds the container, the
 * synthesized disk `cont` carries the mounted volume. Only nodes hdiutil returned for OUR image
 * file are accepted (cross-checked with hdiutil info like make_image). */
static bool make_apfs_image(char phys[32], char cont[32], char mountpoint[256]) {
    NSString *base = [g_tmpdir stringByAppendingPathComponent:@"apfs"];
    NSString *dmg = [base stringByAppendingString:@".dmg"];
    int st;
    run(@[ @"/usr/bin/hdiutil", @"create", @"-size", @"64m", @"-type", @"UDIF", @"-fs", @"APFS",
           @"-volname", @"MHAPFS", base ], &st);
    if (st != 0 || ![[NSFileManager defaultManager] fileExistsAtPath:dmg]) return false;
    NSString *out = run(@[ @"/usr/bin/hdiutil", @"attach", @"-plist", dmg ], &st);
    if (st != 0) return false;
    NSDictionary *pl = [NSPropertyListSerialization
        propertyListWithData:[out dataUsingEncoding:NSUTF8StringEncoding] options:0 format:NULL error:NULL];
    phys[0] = cont[0] = mountpoint[0] = 0;
    for (NSDictionary *e in pl[@"system-entities"]) {
        NSString *de = e[@"dev-entry"];
        if (!de || !strict_disk_node(de.UTF8String)) continue;
        if (strlcpy(g_attached[g_nattached], de.UTF8String, 32) < 32) g_nattached++;
        if ([e[@"content-hint"] isEqualToString:@"GUID_partition_scheme"]) strlcpy(phys, de.UTF8String, 32);
        else strlcpy(cont, de.UTF8String, 32);
    }
    for (NSDictionary *e in pl[@"system-entities"])
        if (e[@"mount-point"]) strlcpy(mountpoint, [e[@"mount-point"] UTF8String], 256);
    if (!phys[0] || !cont[0] || !mountpoint[0]) return false;
    NSString *info = run(@[ @"/usr/bin/hdiutil", @"info", @"-plist" ], &st);
    NSDictionary *ip = [NSPropertyListSerialization
        propertyListWithData:[info dataUsingEncoding:NSUTF8StringEncoding] options:0 format:NULL error:NULL];
    for (NSDictionary *img in ip[@"images"])
        for (NSDictionary *e in img[@"system-entities"])
            if ([e[@"dev-entry"] isEqualToString:@(phys)] && [img[@"image-path"] isEqualToString:dmg]) return true;
    return false;
}

static const char *bsd_of(const char *dev) { return dev + 5; }

static const MhDiskInfo *find(const MhDiskInfo *l, int n, const char *bsd) {
    for (int i = 0; i < n; i++)
        if (!strcmp(l[i].bsd_name, bsd)) return &l[i];
    return NULL;
}

/* ---- always-run, read-only ------------------------------------------------------------- */

static void test_safety_readonly(void) {
    struct statfs sf;
    CHECK(statfs("/", &sf) == 0);
    uint32_t sys[32];
    int ns = mh_disk_system_numbers(sys, 32);
    CHECK(ns >= 1);
    printf("system whole disks:");
    for (int i = 0; i < ns; i++) printf(" disk%u", sys[i]);
    printf("\n");

    MhDiskOpts o = {0};
    MhDiskInfo list[MH_DISK_MAX];
    char err[256];
    int n = mh_disk_enumerate(list, MH_DISK_MAX, &o, err, sizeof err);
    printf("eligible disks (no image mode): %d\n", n);
    for (int i = 0; i < n; i++) {
        char t[256];
        mh_disk_label_text(&list[i], t, sizeof t);
        printf("  %s\n", t);
        CHECK(!list[i].is_system && !list[i].is_synthesized && !list[i].is_image);
        CHECK(list[i].size >= 1 && list[i].size <= MH_DISK_MAX_BYTES);
        for (int k = 0; k < ns; k++) CHECK(list[i].number != sys[k]);
    }
    /* Every system disk: queried, marked, never openable for write. */
    for (int k = 0; k < ns; k++) {
        MhDiskInfo d;
        CHECK(mh_disk_query(sys[k], &o, &d));
        CHECK(d.is_system);
        CHECK(!mh_disk_eligible(&d, MH_DISK_ALLOW_IMAGES, NULL));
        MhDiskInfo forged = d;
        forged.is_system = 0; /* a lying caller */
        MhDiskInfo fresh;
        CHECK(!mh_disk_identity_matches(&forged, &o, &fresh));
        MhBlockDev *dev = mh_blockdev_open_write(&forged, &o, err, sizeof err);
        CHECK(dev == NULL && err[0]);
        dev = mh_blockdev_open_write(&d, &o, err, sizeof err);
        CHECK(dev == NULL);
    }
    /* Any external disk above the size limit (for example a 2 TB HDD): reported, not offered. */
    for (uint32_t i = 0; i < 40; i++) {
        MhDiskInfo d;
        char b[16];
        snprintf(b, sizeof b, "disk%u", i);
        if (!mh_disk_query(i, &o, &d)) continue;
        if (d.is_whole && d.size > MH_DISK_MAX_BYTES) {
            CHECK(!mh_disk_eligible(&d, MH_DISK_ALLOW_IMAGES, NULL));
            CHECK(find(list, n, b) == NULL);
            MhBlockDev *dev = mh_blockdev_open_write(&d, &o, err, sizeof err);
            CHECK(dev == NULL);
            printf("  large disk %s (%.0f GB) refused\n", b, d.size / 1e9);
        }
    }
    /* Names that are not whole disks never become paths. */
    MhDiskInfo bad;
    memset(&bad, 0, sizeof bad);
    strcpy(bad.bsd_name, "disk3s1");
    bad.number = 3;
    CHECK(mh_blockdev_open_write(&bad, &o, err, sizeof err) == NULL);
    strcpy(bad.bsd_name, "../../etc/passwd");
    CHECK(mh_blockdev_open_write(&bad, &o, err, sizeof err) == NULL);
    CHECK(mh_disk_open_raw("disk3s1", true, NULL) == -1);
    CHECK(mh_disk_open_raw("../disk3", false, NULL) == -1);
}

static void test_authopen_plumbing(void) {
    /* A file we own and can read: authopen must not need a dialog. Skip if it does. */
    NSString *f = [NSTemporaryDirectory() stringByAppendingPathComponent:@"mh_authopen_probe.txt"];
    [@"hello" writeToFile:f atomically:YES encoding:NSUTF8StringEncoding error:nil];
    int fd = mh_authopen(f.UTF8String, O_RDONLY, 4000);
    if (fd < 0) {
        printf("authopen plumbing: skipped (no descriptor, would need a dialog)\n");
    } else {
        char b[8] = {0};
        CHECK(read(fd, b, 5) == 5 && !strcmp(b, "hello"));
        close(fd);
        printf("authopen plumbing: ok\n");
    }
    [[NSFileManager defaultManager] removeItemAtPath:f error:nil];
    CHECK(mh_authopen("relative/path", O_RDONLY, 100) == -1);
    CHECK(mh_authopen("/dev/null", 99, 100) == -1);
}

/* ---- image based end to end ------------------------------------------------------------ */

static void fill(uint8_t *b, size_t n, uint32_t seed) {
    for (size_t i = 0; i < n; i++) {
        seed = seed * 1664525u + 1013904223u;
        b[i] = (uint8_t)(seed >> 24);
    }
}

static void test_images(void) {
    char dev[32], mp[256];
    char err[512];
    MhDiskOpts on = {.flags = MH_DISK_ALLOW_IMAGES};
    MhDiskOpts off = {0};
    CHECK(mh_disk_flags_from_env() == 0 || mh_disk_flags_from_env() == MH_DISK_ALLOW_IMAGES);

    CHECK(make_image("raw", NULL, dev, mp));
    CHECK(strict_disk_node(dev));
    const char *bsd = bsd_of(dev);
    printf("raw image attached as %s\n", dev);

    MhDiskInfo list[MH_DISK_MAX];
    int n = mh_disk_enumerate(list, MH_DISK_MAX, &off, err, sizeof err);
    CHECK(find(list, n, bsd) == NULL); /* not offered without the test flag */
    n = mh_disk_enumerate(list, MH_DISK_MAX, &on, err, sizeof err);
    const MhDiskInfo *me = find(list, n, bsd);
    CHECK(me != NULL);
    CHECK(me->is_image && me->is_whole && !me->is_system && !me->is_synthesized);
    CHECK(me->size == 64ULL * 1024 * 1024 && me->block_size == 512 && me->mounted == 0);
    CHECK(!me->has_v2);
    char txt[256];
    mh_disk_label_text(me, txt, sizeof txt);
    printf("  combo text: %s\n", txt);
    CHECK(strstr(txt, bsd) && strstr(txt, "64.0 MB"));
    MhDiskInfo known = *me;

    /* Refusals before any write */
    MhDiskInfo x = known;
    x.size += 512;
    MhBlockDev *d = mh_blockdev_open_write(&x, &on, err, sizeof err);
    CHECK(d == NULL && strstr(err, "identidade"));
    x = known;
    strcpy(x.serial, "FORGED");
    CHECK(mh_blockdev_open_write(&x, &on, err, sizeof err) == NULL);
    x = known;
    x.media_id ^= 0x55;
    CHECK(mh_blockdev_open_write(&x, &on, err, sizeof err) == NULL);
    CHECK(mh_blockdev_open_write(&known, &off, err, sizeof err) == NULL); /* flag required */

    /* Write */
    d = mh_blockdev_open_write(&known, &on, err, sizeof err);
    if (!d) fprintf(stderr, "open_write failed: %s\n", err);
    CHECK(d != NULL);
    CHECK(d->writable && d->size == known.size);
    CHECK(fcntl(d->io.fd, F_GETFD) & FD_CLOEXEC); /* a spawned diskutil/authopen must not inherit the raw disk */
    CHECK(mh_blockdev_recheck(d, err, sizeof err));
    uint8_t blk[512], hdr[512], rd[512];
    memset(hdr, 0, sizeof hdr);
    memcpy(hdr, "PSXRAW", 6);
    hdr[6] = 2;
    hdr[7] = 1;
    fill(blk, sizeof blk, 1);
    CHECK(mh_io_write_block_verified(&d->io, 512, blk));
    CHECK(mh_io_write_block_verified(&d->io, 0xA00, hdr)); /* slot B */
    CHECK(mh_io_write_block_verified(&d->io, known.size - 512, blk));
    CHECK(mh_io_read(&d->io, 512, rd, 512) && !memcmp(rd, blk, 512));
    size_t big = 1024 * 1024;
    uint8_t *src = malloc(big);
    fill(src, big, 7);
    uint64_t lba = 0;
    CHECK(mh_io_sync_blocks_verified(&d->io, 1024 * 1024, src, big, NULL, "t", &lba));
    CHECK(mh_io_compare_bytes(&d->io, 1024 * 1024, src, big, NULL, "t"));
    /* past the end must fail, not wrap */
    CHECK(!mh_io_write(&d->io, known.size, blk, 512));
    CHECK(!mh_io_write(&d->io, 513, blk, 512)); /* unaligned */
    mh_blockdev_refresh_properties(d);
    mh_blockdev_close(d);

    /* Read back through a fresh read handle and through query_info semantics */
    MhDiskInfo q;
    CHECK(mh_disk_query(known.number, &on, &q));
    CHECK(q.has_v2 && q.v2_state == 2 && q.v2_flags == 1);
    mh_disk_label_text(&q, txt, sizeof txt);
    CHECK(strstr(txt, "PSXRAW V2"));
    MhBlockDev *r = mh_blockdev_open_read(&q, &on, err, sizeof err);
    CHECK(r != NULL && !r->writable);
    CHECK(mh_io_read(&r->io, 0xA00, rd, 512) && !memcmp(rd, hdr, 512));
    CHECK(!mh_io_write(&r->io, 0, blk, 512)); /* read-only fd */
    mh_blockdev_close(r);
    free(src);

    /* slot A with state 3 wins/loses per the rules: state 4 is not a header */
    d = mh_blockdev_open_write(&q, &on, err, sizeof err);
    CHECK(d != NULL);
    hdr[6] = 4;
    CHECK(mh_io_write_block_verified(&d->io, 0, hdr));
    hdr[6] = 2;
    CHECK(mh_io_write_block_verified(&d->io, 0xA00, hdr));
    mh_blockdev_close(d);
    CHECK(mh_disk_query(known.number, &on, &q) && q.has_v2 && q.v2_state == 2);
    d = mh_blockdev_open_write(&q, &on, err, sizeof err);
    CHECK(d != NULL);
    memset(hdr, 0, sizeof hdr);
    CHECK(mh_io_write_block_verified(&d->io, 0xA00, hdr));
    mh_blockdev_close(d);
    CHECK(mh_disk_query(known.number, &on, &q) && !q.has_v2);

    detach_all();
    /* A detached disk is gone: identity must fail */
    CHECK(!mh_disk_query(known.number, &on, &q) ||
          !mh_disk_identity_matches(&known, &on, &q));

    /* Mounted FAT image: unmount path and busy refusal */
    CHECK(make_image("fat", "MS-DOS", dev, mp));
    bsd = bsd_of(dev);
    printf("FAT image %s mounted at %s\n", dev, mp);
    n = mh_disk_enumerate(list, MH_DISK_MAX, &on, err, sizeof err);
    me = find(list, n, bsd);
    CHECK(me && me->mounted == 1 && !strcmp(me->label, "MHTEST"));
    mh_disk_label_text(me, txt, sizeof txt);
    CHECK(strstr(txt, "MHTEST"));
    known = *me;

    pid_t pid = fork();
    if (pid == 0) { /* keeps the volume busy: cwd inside it */
        if (chdir(mp) == 0) sleep(8);
        _exit(0);
    }
    usleep(300 * 1000);
    d = mh_blockdev_open_write(&known, &on, err, sizeof err);
    printf("  busy volume -> %s\n", d ? "OPENED (unexpected)" : err);
    CHECK(d == NULL && strstr(err, "esta em uso"));
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    usleep(300 * 1000);

    d = mh_blockdev_open_write(&known, &on, err, sizeof err);
    if (!d) fprintf(stderr, "open_write (mounted) failed: %s\n", err);
    CHECK(d != NULL);
    struct statfs sf;
    CHECK(statfs(mp, &sf) != 0 || strcmp(sf.f_mntfromname, dev) != 0); /* unmounted */
    CHECK(mh_io_write_block_verified(&d->io, 512, blk));
    mh_blockdev_close(d);
    detach_all();
}


/* A volume mounted through a synthesized APFS container is a mounted volume of the physical
 * disk: it must be counted, unmounted before the raw open and its mount must not be missed. */
static void test_apfs_image(void) {
    char phys[32], cont[32], mp[256], err[512];
    MhDiskOpts on = {.flags = MH_DISK_ALLOW_IMAGES};
    CHECK(make_apfs_image(phys, cont, mp));
    printf("APFS image: store %s, container %s mounted at %s\n", phys, cont, mp);
    MhDiskInfo list[MH_DISK_MAX];
    int n = mh_disk_enumerate(list, MH_DISK_MAX, &on, err, sizeof err);
    const MhDiskInfo *me = find(list, n, bsd_of(phys));
    CHECK(me && me->is_image && !me->is_synthesized);
    CHECK(find(list, n, bsd_of(cont)) == NULL); /* the container itself is never offered */
    CHECK(me->mounted == 1 && !strcmp(me->label, "MHAPFS"));
    MhDiskInfo known = *me;
    MhBlockDev *d = mh_blockdev_open_write(&known, &on, err, sizeof err);
    if (!d) fprintf(stderr, "open_write (APFS) failed: %s\n", err);
    CHECK(d != NULL);
    struct statfs sf;
    CHECK(statfs(mp, &sf) != 0 || strncmp(sf.f_mntfromname, cont, strlen(cont)) != 0); /* unmounted */
    uint8_t blk[512];
    fill(blk, sizeof blk, 3);
    CHECK(mh_io_write_block_verified(&d->io, 512, blk));
    mh_blockdev_close(d);
    detach_all();
}

int main(void) {
    @autoreleasepool {
        setvbuf(stdout, NULL, _IOLBF, 0);
        char tmpl[] = "/private/tmp/mh_disk_test_XXXXXX";
        CHECK(mkdtemp(tmpl) != NULL);
        g_tmpdir = @(tmpl);
        atexit(cleanup);
        test_safety_readonly();
        test_authopen_plumbing();
        const char *img = getenv("MH_DISK_TEST_IMAGES");
        if (img && !strcmp(img, "1")) {
            test_images();
            test_apfs_image();
        }
        else
            printf("image tests skipped (set MH_DISK_TEST_IMAGES=1 or use tools/test-disk.sh --images)\n");
        printf("disk-mac: ok\n");
        cleanup();
    }
    return 0;
}
