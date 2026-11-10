/* blockdev: portable parts of the disk layer (rules, text, test stand-in). The platform half
 * (enumeration, unmount, privileged open) lives in platform/mac/mh_disk_mac.*. */
#include "mh/blockdev.h"
#include "mh/str.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

uint32_t mh_disk_flags_from_env(void) {
    const char *v = getenv(MH_DISK_TEST_ENV);
    return (v && v[0] == '1' && v[1] == '\0') ? MH_DISK_ALLOW_IMAGES : 0;
}

bool mh_disk_valid_bsd_name(const char *name, uint32_t *number) {
    if (!name || strncmp(name, "disk", 4) != 0) return false;
    const char *p = name + 4;
    size_t n = strlen(p);
    if (n < 1 || n > 6) return false;
    if (n > 1 && p[0] == '0') return false;
    uint32_t v = 0;
    for (size_t i = 0; i < n; i++) {
        if (p[i] < '0' || p[i] > '9') return false;
        v = v * 10 + (uint32_t)(p[i] - '0');
    }
    if (number) *number = v;
    return true;
}

bool mh_disk_device_path(const char *bsd_name, bool raw, char *out, size_t n) {
    if (!out || n == 0) return false;
    out[0] = 0;
    if (!mh_disk_valid_bsd_name(bsd_name, NULL)) return false;
    int w = snprintf(out, n, "/dev/%s%s", raw ? "r" : "", bsd_name);
    if (w < 0 || (size_t)w >= n) {
        out[0] = 0;
        return false;
    }
    return true;
}

bool mh_disk_eligible(const MhDiskInfo *d, uint32_t flags, const char **why) {
    const char *dummy;
    if (!why) why = &dummy;
    *why = "";
    if (!d) { *why = "no disk"; return false; }
    bool img_ok = (flags & MH_DISK_ALLOW_IMAGES) != 0;
    if (!mh_disk_valid_bsd_name(d->bsd_name, NULL)) { *why = "invalid BSD name"; return false; }
    if (!d->is_whole) { *why = "not a whole disk"; return false; }
    if (d->is_system) { *why = "system disk"; return false; }
    if (d->size < 1 || d->size > MH_DISK_MAX_BYTES) { *why = "size out of range"; return false; }
    if (d->block_size != MH_IO_BLOCK || d->size % MH_IO_BLOCK) { *why = "block size not 512"; return false; }
    if (d->is_synthesized) { *why = "synthesized disk"; return false; }
    if (d->is_optical) { *why = "optical media"; return false; }
    if (d->is_image) {
        if (!img_ok) { *why = "disk image"; return false; }
        return true; /* test mode: no bus rule for images */
    }
    if (d->is_internal && !d->removable) { *why = "internal fixed disk"; return false; }
    if (d->bus_type == MH_BUS_USB || d->bus_type == MH_BUS_SD || d->bus_type == MH_BUS_MMC)
        return true;
    if (d->removable && d->mounted > 0) return true;
    if (d->has_v2 && !d->is_internal) return true;
    *why = "not a USB/SD/removable device";
    return false;
}

bool mh_disk_same_identity(const MhDiskInfo *a, const MhDiskInfo *b) {
    if (!a || !b) return false;
    if (a->number != b->number || strcmp(a->bsd_name, b->bsd_name) != 0) return false;
    if (a->size != b->size || a->block_size != b->block_size) return false;
    if (strcmp(a->model, b->model) != 0 || strcmp(a->serial, b->serial) != 0) return false;
    if (a->media_id && b->media_id && a->media_id != b->media_id) return false;
    return true;
}

/* orig: refresh_sd.part.0 @0x14002d600 */
void mh_disk_label_text(const MhDiskInfo *d, char *out, size_t n) {
    if (!out || n == 0) return;
    out[0] = 0;
    if (!d) return;
    MhWbuf w;
    mh_wbuf_init(&w);
    mh_wbuf_puts(&w, d->bsd_name);
    mh_wbuf_puts(&w, " | ");
    mh_wbuf_puts(&w, d->model[0] ? d->model : MH_DISK_TXT_GENERIC_MODEL);
    mh_wbuf_puts(&w, " | ");
    mh_wbuf_size(&w, d->size);
    if (d->label[0]) {
        mh_wbuf_puts(&w, " | ");
        mh_wbuf_puts(&w, d->label);
    }
    if (d->has_v2) {
        mh_wbuf_puts(&w, " | PSXRAW V");
        mh_wbuf_u32(&w, (uint32_t)(int32_t)(int8_t)d->v2_state);
    }
    mh_str_copy(out, n, mh_wbuf_cstr(&w));
    mh_wbuf_free(&w);
}

/* ---- handle dispatch ---------------------------------------------------------------------- */

bool mh_blockdev_recheck(MhBlockDev *dev, char *err, size_t errn) {
    if (!dev) return false;
    if (dev->ops && dev->ops->recheck) return dev->ops->recheck(dev, err, errn);
    return true;
}

void mh_blockdev_refresh_properties(MhBlockDev *dev) {
    if (dev && dev->ops && dev->ops->refresh) dev->ops->refresh(dev);
}

void mh_blockdev_close(MhBlockDev *dev) {
    if (!dev) return;
    if (dev->writable && dev->io.fd >= 0) mh_io_flush(&dev->io, 0);
    mh_io_close(&dev->io);
    if (dev->ops && dev->ops->release) dev->ops->release(dev);
    free(dev);
}

MhBlockDev *mh_blockdev_open_file(const char *path, bool writable, uint64_t size, char *err,
                                  size_t errn) {
    int fd = open(path, writable ? (O_RDWR | O_CREAT) : O_RDONLY, 0644);
    if (fd < 0) {
        mh_err_set(err, errn, "Nao foi possivel abrir o arquivo: %s", strerror(errno));
        return NULL;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        mh_err_set(err, errn, "O alvo de teste nao e um arquivo regular.");
        return NULL;
    }
    if (size == 0) size = (uint64_t)st.st_size;
    if (writable && (uint64_t)st.st_size < size && ftruncate(fd, (off_t)size) != 0) {
        close(fd);
        mh_err_set(err, errn, "Nao foi possivel dimensionar o arquivo: %s", strerror(errno));
        return NULL;
    }
    MhBlockDev *dev = calloc(1, sizeof *dev);
    if (!dev) {
        close(fd);
        return NULL;
    }
    mh_io_attach(&dev->io, fd, writable, NULL);
    dev->size = size;
    dev->writable = writable;
    dev->standin = 1;
    dev->info.size = size;
    dev->info.block_size = MH_IO_BLOCK;
    dev->info.is_whole = 1;
    mh_str_copy(dev->info.bsd_name, sizeof dev->info.bsd_name, "file");
    return dev;
}
