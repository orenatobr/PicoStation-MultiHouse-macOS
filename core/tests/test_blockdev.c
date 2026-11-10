/* Portable tests for mh/blockdev.h: name validation, eligibility, identity, text, stand-in. */
#include "mh/base.h"
#include "mh/blockdev.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static MhDiskInfo sd(void) {
    MhDiskInfo d;
    memset(&d, 0, sizeof d);
    d.number = 4;
    strcpy(d.bsd_name, "disk4");
    d.bus_type = MH_BUS_SD;
    d.removable = 1;
    d.is_whole = 1;
    d.size = 32ULL * 1000 * 1000 * 1000 / 512 * 512;
    d.block_size = 512;
    strcpy(d.model, "Generic SD");
    strcpy(d.serial, "ABC123");
    d.media_id = 77;
    return d;
}

static void test_names(void) {
    uint32_t n = 99;
    assert(mh_disk_valid_bsd_name("disk0", &n) && n == 0);
    assert(mh_disk_valid_bsd_name("disk12", &n) && n == 12);
    const char *bad[] = {"", "disk", "disk01", "disk1s1", "rdisk1", "/dev/disk1", "disk-1", "disk1 ",
                         "disk1234567", "Disk1", "disk1\n", "disk1/../x", "disk 1", "../disk1", NULL};
    for (int i = 0; bad[i]; i++) assert(!mh_disk_valid_bsd_name(bad[i], NULL));
    assert(!mh_disk_valid_bsd_name(NULL, NULL));
    char p[32];
    assert(mh_disk_device_path("disk6", true, p, sizeof p) && !strcmp(p, "/dev/rdisk6"));
    assert(mh_disk_device_path("disk6", false, p, sizeof p) && !strcmp(p, "/dev/disk6"));
    assert(!mh_disk_device_path("disk6s1", true, p, sizeof p) && p[0] == 0);
    assert(!mh_disk_device_path("disk6", true, p, 8));
}

static void test_eligible(void) {
    const char *why;
    MhDiskInfo d = sd();
    assert(mh_disk_eligible(&d, 0, &why));

    /* the 2 TB external NTFS HDD of the developer machine: USB, fixed, too big */
    MhDiskInfo hdd = sd();
    hdd.number = 6;
    strcpy(hdd.bsd_name, "disk6");
    hdd.bus_type = MH_BUS_USB;
    hdd.removable = 0;
    hdd.size = 2000365289472ULL;
    assert(!mh_disk_eligible(&hdd, 0, &why));
    assert(!mh_disk_eligible(&hdd, MH_DISK_ALLOW_IMAGES, &why));
    /* boundary of the original rule: size - 1 < 130e9 */
    hdd.size = 130000000000ULL / 512 * 512;
    assert(mh_disk_eligible(&hdd, 0, &why));
    hdd.size = 130000000512ULL;
    assert(!mh_disk_eligible(&hdd, 0, &why));
    hdd.size = 0;
    assert(!mh_disk_eligible(&hdd, 0, &why));

    MhDiskInfo x = sd();
    x.is_system = 1;
    assert(!mh_disk_eligible(&x, 0, &why));
    x = sd();
    x.is_whole = 0;
    assert(!mh_disk_eligible(&x, 0, &why));
    x = sd();
    x.is_synthesized = 1;
    assert(!mh_disk_eligible(&x, MH_DISK_ALLOW_IMAGES, &why));
    x = sd();
    x.is_optical = 1;
    assert(!mh_disk_eligible(&x, 0, &why));
    x = sd();
    x.block_size = 4096;
    assert(!mh_disk_eligible(&x, 0, &why));
    x = sd();
    x.size += 1;
    assert(!mh_disk_eligible(&x, 0, &why));
    x = sd();
    strcpy(x.bsd_name, "disk4s1");
    assert(!mh_disk_eligible(&x, 0, &why));
    x = sd();
    x.is_internal = 1;
    x.removable = 0;
    assert(!mh_disk_eligible(&x, 0, &why));
    /* internal SD slot: removable media is fine */
    x = sd();
    x.is_internal = 1;
    assert(mh_disk_eligible(&x, 0, &why));
    /* SATA/NVMe never, unless removable with a volume or an existing V2 card that is external */
    x = sd();
    x.bus_type = MH_BUS_OTHER;
    x.removable = 0;
    assert(!mh_disk_eligible(&x, 0, &why));
    x.removable = 1;
    x.mounted = 1;
    assert(mh_disk_eligible(&x, 0, &why));
    x.removable = 0;
    x.mounted = 0;
    x.has_v2 = 1;
    assert(mh_disk_eligible(&x, 0, &why));
    x.is_internal = 1;
    assert(!mh_disk_eligible(&x, 0, &why));
    /* images: only in test mode */
    x = sd();
    x.bus_type = MH_BUS_OTHER;
    x.removable = 0;
    x.is_image = 1;
    assert(!mh_disk_eligible(&x, 0, &why));
    assert(mh_disk_eligible(&x, MH_DISK_ALLOW_IMAGES, &why));
    x.is_system = 1;
    assert(!mh_disk_eligible(&x, MH_DISK_ALLOW_IMAGES, &why));
}

static void test_identity(void) {
    MhDiskInfo a = sd(), b = sd();
    assert(mh_disk_same_identity(&a, &b));
    b.size += 512;
    assert(!mh_disk_same_identity(&a, &b));
    b = sd();
    b.number = 5;
    strcpy(b.bsd_name, "disk5");
    assert(!mh_disk_same_identity(&a, &b));
    b = sd();
    strcpy(b.model, "Other");
    assert(!mh_disk_same_identity(&a, &b));
    b = sd();
    strcpy(b.serial, "ZZZ");
    assert(!mh_disk_same_identity(&a, &b));
    b = sd();
    b.media_id = 78;
    assert(!mh_disk_same_identity(&a, &b));
    b = sd();
    b.media_id = 0;
    assert(mh_disk_same_identity(&a, &b));
    assert(!mh_disk_same_identity(NULL, &b));
}

static void test_label(void) {
    char t[256];
    MhDiskInfo d = sd();
    mh_disk_label_text(&d, t, sizeof t);
    assert(!strcmp(t, "disk4 | Generic SD | 29.8 GB"));
    strcpy(d.label, "PICO");
    d.has_v2 = 1;
    d.v2_state = 2;
    mh_disk_label_text(&d, t, sizeof t);
    assert(!strcmp(t, "disk4 | Generic SD | 29.8 GB | PICO | PSXRAW V2"));
    d.model[0] = 0;
    d.label[0] = 0;
    d.has_v2 = 0;
    mh_disk_label_text(&d, t, sizeof t);
    assert(!strcmp(t, "disk4 | Dispositivo USB/SD | 29.8 GB"));
    mh_disk_label_text(&d, t, 8);
    assert(strlen(t) < 8);
}

static void test_standin(void) {
    char path[] = "/tmp/mh_blockdev_test_XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    char err[256] = "";
    MhBlockDev *dev = mh_blockdev_open_file(path, true, 4096, err, sizeof err);
    assert(dev && dev->size == 4096 && dev->standin);
    uint8_t blk[512], rd[512];
    memset(blk, 0xA5, sizeof blk);
    assert(mh_io_write_block_verified(&dev->io, 1024, blk));
    assert(mh_io_read(&dev->io, 1024, rd, 512) && !memcmp(blk, rd, 512));
    assert(mh_blockdev_recheck(dev, err, sizeof err));
    mh_blockdev_refresh_properties(dev);
    mh_blockdev_close(dev);
    mh_blockdev_close(NULL);
    unlink(path);
    assert(mh_blockdev_open_file("/nonexistent/dir/x", false, 0, err, sizeof err) == NULL && err[0]);
}

int main(void) {
    test_names();
    test_eligible();
    test_identity();
    test_label();
    test_standin();
    puts("blockdev: ok");
    return 0;
}
