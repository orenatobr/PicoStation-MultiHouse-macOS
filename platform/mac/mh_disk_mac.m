/* mh_disk_mac.m: macOS disk discovery, identity and volume locking (DiskArbitration + IOKit).
 * Ports sd_enumerate, system_disks, query_info, identity_matches, lock_volumes,
 * refresh_disk_properties, sd_refresh_identity. See docs/map/disk.md. */
#import <Foundation/Foundation.h>
#import <DiskArbitration/DiskArbitration.h>
#import <IOKit/IOKitLib.h>
#import <IOKit/storage/IOMedia.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <unistd.h>

#include "mh/base.h"
#include "mh/blockdev.h"
#include "mh_disk_mac.h"

/* ---- small CF helpers ---------------------------------------------------------------------- */

static bool cf_str(CFDictionaryRef d, CFStringRef key, char *out, size_t n) {
    if (out && n) out[0] = 0;
    CFTypeRef v = d ? CFDictionaryGetValue(d, key) : NULL;
    if (!v || CFGetTypeID(v) != CFStringGetTypeID()) return false;
    return CFStringGetCString((CFStringRef)v, out, (CFIndex)n, kCFStringEncodingUTF8);
}

static bool cf_u64(CFDictionaryRef d, CFStringRef key, uint64_t *out) {
    CFTypeRef v = d ? CFDictionaryGetValue(d, key) : NULL;
    if (!v || CFGetTypeID(v) != CFNumberGetTypeID()) return false;
    long long x = 0;
    if (!CFNumberGetValue((CFNumberRef)v, kCFNumberLongLongType, &x) || x < 0) return false;
    *out = (uint64_t)x;
    return true;
}

/* tri-state: -1 absent */
static int cf_bool(CFDictionaryRef d, CFStringRef key) {
    CFTypeRef v = d ? CFDictionaryGetValue(d, key) : NULL;
    if (!v || CFGetTypeID(v) != CFBooleanGetTypeID()) return -1;
    return CFBooleanGetValue((CFBooleanRef)v) ? 1 : 0;
}

static bool ioreg_bsd_whole(io_registry_entry_t e, uint32_t *number) {
    bool ok = false;
    CFTypeRef whole = IORegistryEntryCreateCFProperty(e, CFSTR(kIOMediaWholeKey), kCFAllocatorDefault, 0);
    CFTypeRef bsd = IORegistryEntryCreateCFProperty(e, CFSTR("BSD Name"), kCFAllocatorDefault, 0);
    if (whole && CFGetTypeID(whole) == CFBooleanGetTypeID() && CFBooleanGetValue(whole) && bsd &&
        CFGetTypeID(bsd) == CFStringGetTypeID()) {
        char name[32];
        if (CFStringGetCString(bsd, name, sizeof name, kCFStringEncodingUTF8) &&
            mh_disk_valid_bsd_name(name, number))
            ok = true;
    }
    if (whole) CFRelease(whole);
    if (bsd) CFRelease(bsd);
    return ok;
}

/* Whole IOMedia that are `media` itself or any ancestor in the IOService plane. Physical
 * stores of an APFS volume come out here, as does the synthesized container. */
static int whole_chain(io_registry_entry_t media, uint32_t *nums, int max, bool *self_whole) {
    int n = 0;
    uint32_t v;
    if (self_whole) *self_whole = false;
    if (ioreg_bsd_whole(media, &v)) {
        if (self_whole) *self_whole = true;
        if (n < max) nums[n++] = v;
    }
    io_iterator_t it = IO_OBJECT_NULL;
    if (IORegistryEntryCreateIterator(media, kIOServicePlane,
                                      kIORegistryIterateRecursively | kIORegistryIterateParents,
                                      &it) == KERN_SUCCESS) {
        io_registry_entry_t e;
        while ((e = IOIteratorNext(it)) != IO_OBJECT_NULL) {
            if (IOObjectConformsTo(e, kIOMediaClass) && ioreg_bsd_whole(e, &v)) {
                bool dup = false;
                for (int i = 0; i < n; i++) dup |= nums[i] == v;
                if (!dup && n < max) nums[n++] = v;
            }
            IOObjectRelease(e);
        }
        IOObjectRelease(it);
    }
    return n;
}

static io_service_t media_for_bsd(const char *bsd) {
    CFMutableDictionaryRef m = IOBSDNameMatching(kIOMainPortDefault, 0, bsd);
    if (!m) return IO_OBJECT_NULL;
    return IOServiceGetMatchingService(kIOMainPortDefault, m); /* consumes m */
}

/* ---- system_disks (orig @0x140032250) -------------------------------------------------------
 * Windows asked for the physical drives behind the Windows directory volume. Here: the volumes
 * mounted at / and /System/Volumes/Data, and every whole disk in their IORegistry ancestry. */

int mh_disk_system_numbers(uint32_t *numbers, int max) {
    if (!numbers || max <= 0) return 0;
    static const char *const mounts[] = {"/", "/System/Volumes/Data"};
    int n = 0;
    bool root_ok = false;
    for (int i = 0; i < 2; i++) {
        struct statfs sf;
        if (statfs(mounts[i], &sf) != 0 || strncmp(sf.f_mntfromname, "/dev/disk", 9) != 0) continue;
        io_service_t media = media_for_bsd(sf.f_mntfromname + 5);
        if (media == IO_OBJECT_NULL) continue;
        uint32_t tmp[16];
        int k = whole_chain(media, tmp, 16, NULL);
        IOObjectRelease(media);
        if (k == 0) continue;
        if (i == 0) root_ok = true;
        for (int j = 0; j < k; j++) {
            bool dup = false;
            for (int q = 0; q < n; q++) dup |= numbers[q] == tmp[j];
            if (!dup && n < max) numbers[n++] = tmp[j];
        }
    }
    return root_ok ? n : 0; /* orig: failure means nothing is offered */
}

static bool in_list(const uint32_t *l, int n, uint32_t v) {
    for (int i = 0; i < n; i++)
        if (l[i] == v) return true;
    return false;
}

/* ---- mounts of one disk -------------------------------------------------------------------- */

static int disk_mounts(const char *bsd, char names[][32], int max) {
    struct statfs *fs = NULL;
    int cnt = getmntinfo(&fs, MNT_NOWAIT);
    size_t bl = strlen(bsd);
    int n = 0;
    for (int i = 0; i < cnt; i++) {
        const char *f = fs[i].f_mntfromname;
        if (strncmp(f, "/dev/", 5) != 0 || strncmp(f + 5, bsd, bl) != 0) continue;
        char c = f[5 + bl];
        if (c != 0 && c != 's') continue; /* disk4 vs disk40 */
        if (c == 's') {
            const char *q = f + 5 + bl + 1;
            if (*q < '0' || *q > '9') continue;
        }
        if (n < max) mh_str_copy(names[n], 32, f + 5);
        n++;
    }
    return n;
}

/* ---- serial from the IORegistry ------------------------------------------------------------ */

static void ioreg_serial(io_registry_entry_t media, char *out, size_t n) {
    static NSString *const keys[] = {@"USB Serial Number", @"kUSBSerialNumberString",
                                     @"Serial Number", @"SerialNumber"};
    out[0] = 0;
    for (int i = 0; i < 4 && !out[0]; i++) {
        CFTypeRef v = IORegistryEntrySearchCFProperty(
            media, kIOServicePlane, (__bridge CFStringRef)keys[i], kCFAllocatorDefault,
            kIORegistryIterateRecursively | kIORegistryIterateParents);
        if (!v) continue;
        char raw[128] = "";
        if (CFGetTypeID(v) == CFStringGetTypeID())
            CFStringGetCString(v, raw, sizeof raw, kCFStringEncodingUTF8);
        CFRelease(v);
        mh_clean_ascii(raw, out, n);
    }
}

/* ---- query_info (orig @0x14002b160) --------------------------------------------------------- */

/* Slot A at 0 (state 1..3) or slot B at 0xA00 (state 2). Retries 5,15,...,75 ms like the
 * original, except for errors that cannot heal (permissions). */
static bool read_header_slot(int fd, uint64_t off, bool slot_a, MhDiskInfo *out) {
    void *buf = NULL;
    if (posix_memalign(&buf, 4096, MH_IO_BLOCK) != 0) return false;
    bool found = false;
    for (unsigned delay = 5; delay != 85; delay += 10) {
        ssize_t r = pread(fd, buf, MH_IO_BLOCK, (off_t)off);
        if (r == MH_IO_BLOCK) {
            const uint8_t *b = buf;
            if (memcmp(b, "PSXRAW", 6) == 0 && (slot_a ? (uint8_t)(b[6] - 1u) < 3 : b[6] == 2)) {
                out->has_v2 = 1;
                out->v2_state = b[6];
                out->v2_flags = b[7];
                found = true;
            }
            break; /* a good read that does not match goes on to the next slot */
        }
        if (r < 0 && (errno == EACCES || errno == EPERM || errno == ENXIO || errno == ENOENT)) break;
        usleep(delay * 1000);
    }
    free(buf);
    return found;
}

static void probe_v2(const char *bsd, const MhDiskOpts *opts, MhDiskInfo *out) {
    int fd = mh_disk_open_raw(bsd, false, NULL);
    if (fd < 0) return;
    if (!read_header_slot(fd, 0, true, out) && !read_header_slot(fd, 0xA00, false, out)) {
        if (opts && opts->recover) {
            MhIo io;
            mh_io_attach(&io, fd, false, NULL);
            fd = -1; /* owned by io now */
            if (opts->recover(opts->recover_user, &io, out->size)) {
                out->has_v2 = 1;
                out->v2_state = 2;
                out->v2_flags = 0;
                out->v2_recovered = 1;
            }
            mh_io_close(&io);
        }
    }
    if (fd >= 0) close(fd);
}

static bool query_one(DASessionRef session, uint32_t number, const MhDiskOpts *opts,
                      const uint32_t *sys, int nsys, MhDiskInfo *out) {
    char bsd[MH_DISK_BSD_MAX];
    snprintf(bsd, sizeof bsd, "disk%u", number);
    DADiskRef disk = DADiskCreateFromBSDName(kCFAllocatorDefault, session, bsd);
    if (!disk) return false;
    CFDictionaryRef d = DADiskCopyDescription(disk);
    if (!d) {
        CFRelease(disk);
        return false;
    }
    memset(out, 0, sizeof *out);
    out->number = number;
    mh_str_copy(out->bsd_name, sizeof out->bsd_name, bsd);

    out->is_whole = cf_bool(d, kDADiskDescriptionMediaWholeKey) == 1;
    uint64_t v = 0;
    out->size = cf_u64(d, kDADiskDescriptionMediaSizeKey, &v) ? v : 0;
    out->block_size = cf_u64(d, kDADiskDescriptionMediaBlockSizeKey, &v) ? (uint32_t)v : 0;
    out->removable = cf_bool(d, kDADiskDescriptionMediaRemovableKey) == 1;
    int w = cf_bool(d, kDADiskDescriptionMediaWritableKey);
    out->writable = w != 0;
    int internal = cf_bool(d, kDADiskDescriptionDeviceInternalKey);
    out->is_internal = internal != 0; /* unknown counts as internal: the safe side */

    char proto[24] = "", vendor[128] = "", model[128] = "", mname[128] = "", kind[64] = "";
    cf_str(d, kDADiskDescriptionDeviceProtocolKey, proto, sizeof proto);
    cf_str(d, kDADiskDescriptionDeviceVendorKey, vendor, sizeof vendor);
    cf_str(d, kDADiskDescriptionDeviceModelKey, model, sizeof model);
    cf_str(d, kDADiskDescriptionMediaNameKey, mname, sizeof mname);
    cf_str(d, kDADiskDescriptionMediaKindKey, kind, sizeof kind);
    mh_str_copy(out->protocol, sizeof out->protocol, proto);
    if (!strcmp(proto, "USB")) out->bus_type = MH_BUS_USB;
    else if (!strcmp(proto, "Secure Digital")) out->bus_type = MH_BUS_SD;
    else if (!strcmp(proto, "MMC") || !strcmp(proto, "Multi Media Card")) out->bus_type = MH_BUS_MMC;
    out->is_image = !strcmp(proto, "Virtual Interface") || !strcmp(proto, "Disk Image") ||
                    !strcmp(model, "Disk Image") || !strcmp(mname, "Disk Image");
    out->is_optical = strstr(kind, "CD") || strstr(kind, "DVD") || strstr(kind, "BD");

    /* orig: clean_ascii(vendor,0x30), clean_ascii(product,0x40), joined with one space */
    char cv[0x30], cp[0x40];
    mh_clean_ascii(vendor, cv, sizeof cv);
    mh_clean_ascii(model, cp, sizeof cp);
    mh_str_copy(out->model, sizeof out->model, cv);
    if (out->model[0] && cp[0]) mh_str_cat(out->model, sizeof out->model, " ");
    mh_str_cat(out->model, sizeof out->model, cp);

    io_service_t media = DADiskCopyIOMedia(disk);
    if (media != IO_OBJECT_NULL) {
        uint64_t id = 0;
        if (IORegistryEntryGetRegistryEntryID(media, &id) == KERN_SUCCESS) out->media_id = id;
        ioreg_serial(media, out->serial, sizeof out->serial);
        uint32_t chain[16];
        bool self_whole;
        int k = whole_chain(media, chain, 16, &self_whole);
        out->is_synthesized = k > (self_whole ? 1 : 0);
        for (int i = 0; i < k; i++)
            if (in_list(sys, nsys, chain[i])) out->is_system = 1;
        IOObjectRelease(media);
    } else {
        out->is_system = 1; /* cannot trace it: never offer */
    }
    if (in_list(sys, nsys, number)) out->is_system = 1;
    if (nsys <= 0) out->is_system = 1; /* orig: system_disks failure rejects everything */

    char names[16][32];
    int mc = disk_mounts(bsd, names, 16);
    out->mounted = mc;
    for (int i = 0; i < mc && i < 16 && !out->label[0]; i++) {
        DADiskRef part = DADiskCreateFromBSDName(kCFAllocatorDefault, session, names[i]);
        if (!part) continue;
        CFDictionaryRef pd = DADiskCopyDescription(part);
        char vn[256] = "";
        cf_str(pd, kDADiskDescriptionVolumeNameKey, vn, sizeof vn);
        mh_clean_ascii(vn, out->label, sizeof out->label);
        if (pd) CFRelease(pd);
        CFRelease(part);
    }
    CFRelease(d);
    CFRelease(disk);

    /* The header probe reads the raw device, so only for disks that could be offered. */
    if (out->size && out->is_whole) {
        MhDiskInfo t = *out;
        t.has_v2 = 1;
        uint32_t flags = opts ? opts->flags : 0;
        if (mh_disk_eligible(&t, flags, NULL)) probe_v2(bsd, opts, out);
    }
    return true;
}

bool mh_disk_query(uint32_t number, const MhDiskOpts *opts, MhDiskInfo *out) {
    if (!out) return false;
    uint32_t sys[32];
    int nsys = mh_disk_system_numbers(sys, 32);
    DASessionRef s = DASessionCreate(kCFAllocatorDefault);
    if (!s) return false;
    bool ok = query_one(s, number, opts, sys, nsys, out);
    CFRelease(s);
    return ok;
}

/* ---- sd_enumerate (orig @0x14002dfd0) ------------------------------------------------------- */

static int cmp_info(const void *a, const void *b) {
    uint32_t x = ((const MhDiskInfo *)a)->number, y = ((const MhDiskInfo *)b)->number;
    return x < y ? -1 : x > y;
}

int mh_disk_enumerate(MhDiskInfo *out, int max, const MhDiskOpts *opts, char *err, size_t errn) {
    if (err && errn) err[0] = 0;
    if (!out || max <= 0) return 0;
    uint32_t sys[32];
    int nsys = mh_disk_system_numbers(sys, 32);
    if (nsys <= 0) {
        mh_err_set(err, errn, "Nao foi possivel identificar o disco do sistema com seguranca.");
        return 0;
    }
    DASessionRef s = DASessionCreate(kCFAllocatorDefault);
    if (!s) return 0;

    uint32_t numbers[256];
    int nn = 0;
    io_iterator_t it = IO_OBJECT_NULL;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching(kIOMediaClass), &it) ==
        KERN_SUCCESS) {
        io_registry_entry_t e;
        while ((e = IOIteratorNext(it)) != IO_OBJECT_NULL) {
            uint32_t num;
            if (ioreg_bsd_whole(e, &num) && nn < 256) numbers[nn++] = num;
            IOObjectRelease(e);
        }
        IOObjectRelease(it);
    }
    uint32_t flags = opts ? opts->flags : 0;
    int count = 0;
    for (int i = 0; i < nn; i++) {
        MhDiskInfo d;
        if (!query_one(s, numbers[i], opts, sys, nsys, &d)) continue;
        if (!mh_disk_eligible(&d, flags, NULL)) continue;
        if (count < MH_DISK_MAX && count < max) out[count++] = d;
    }
    CFRelease(s);
    qsort(out, (size_t)count, sizeof *out, cmp_info);
    return count;
}

/* ---- identity_matches / sd_refresh_identity (orig @0x14001cc20 / 0x14002ec60) --------------- */

bool mh_disk_identity_matches(const MhDiskInfo *known, const MhDiskOpts *opts, MhDiskInfo *fresh) {
    if (!known || !fresh) return false;
    uint32_t n;
    if (!mh_disk_valid_bsd_name(known->bsd_name, &n) || n != known->number) return false;
    uint32_t flags = opts ? opts->flags : 0;
    if (!mh_disk_eligible(known, flags, NULL)) return false;
    if (!mh_disk_query(known->number, opts, fresh)) return false;
    /* Our own unmount drops "mounted" and a rewritten header drops has_v2: give the fresh
     * record the credit the user-visible record had, everything else is judged live. */
    MhDiskInfo t = *fresh;
    if (known->mounted > 0 && t.mounted == 0) t.mounted = known->mounted;
    if (known->has_v2) t.has_v2 = 1;
    if (!mh_disk_eligible(&t, flags, NULL)) return false;
    return mh_disk_same_identity(known, fresh);
}

/* ---- lock_volumes (orig @0x14001dd10) -------------------------------------------------------- */

typedef struct {
    dispatch_semaphore_t sem;
    DAReturn status;
    bool done;
} UnmountCtx;

static void unmount_cb(DADiskRef disk, DADissenterRef dissenter, void *ctx) {
    UnmountCtx *c = ctx;
    c->status = dissenter ? DADissenterGetStatus(dissenter) : kDAReturnSuccess;
    c->done = true;
    dispatch_semaphore_signal(c->sem);
}

/* Unmounts every volume of `bsd` (kDADiskUnmountOptionWhole, never forced). Text and retry
 * schedule are the original's: lock attempts at 40,75,110,...,285 ms, "em uso" when still
 * busy, a separate text for any other failure. */
static bool unmount_whole(const MhDiskInfo *info, char *err, size_t errn) {
    char names[16][32];
    int found = disk_mounts(info->bsd_name, names, 16);
    if (found == 0) return true; /* already unmounted (even better than the original) */
    if (found > 16) {
        mh_err_set(err, errn, "O SD possui volumes demais para serem bloqueados com seguranca.");
        return false;
    }
    DASessionRef s = DASessionCreate(kCFAllocatorDefault);
    if (!s) {
        mh_err_set(err, errn, "Nao foi possivel desmontar um volume do SD com seguranca.");
        return false;
    }
    dispatch_queue_t q = dispatch_queue_create("mh.disk.unmount", DISPATCH_QUEUE_SERIAL);
    DASessionSetDispatchQueue(s, q);
    DADiskRef disk = DADiskCreateFromBSDName(kCFAllocatorDefault, s, info->bsd_name);
    bool ok = false;
    if (disk) {
        bool busy = false;
        for (unsigned delay = 40; delay != 320; delay += 35) {
            UnmountCtx c = {dispatch_semaphore_create(0), kDAReturnSuccess, false};
            DADiskUnmount(disk, kDADiskUnmountOptionWhole, unmount_cb, &c);
            long to = dispatch_semaphore_wait(c.sem, dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_SEC));
            if (to != 0) { busy = false; break; } /* no answer: treat as failure */
            if (c.status == kDAReturnSuccess) { ok = true; break; }
            /* DiskArbitration reports a busy mount as unix_err(EBUSY) = 0xc010 */
            busy = c.status == kDAReturnBusy || c.status == kDAReturnExclusiveAccess ||
                   c.status == (0xc000u | EBUSY);
            if (!busy) break;
            usleep(delay * 1000);
        }
        if (!ok) {
            if (busy)
                mh_err_set(err, errn, "Um volume do SD esta em uso e nao pode ser bloqueado. Feche o Explorador e outros programas que usam o cartao.");
            else
                mh_err_set(err, errn, "Nao foi possivel desmontar um volume do SD com seguranca.");
        }
    } else {
        mh_err_set(err, errn, "Nao foi possivel desmontar um volume do SD com seguranca.");
    }
    if (disk) CFRelease(disk);
    DASessionSetDispatchQueue(s, NULL);
    CFRelease(s);
    if (ok && disk_mounts(info->bsd_name, names, 16) != 0) {
        mh_err_set(err, errn, "Nao foi possivel desmontar um volume do SD com seguranca.");
        ok = false;
    }
    return ok;
}

/* ---- block device handle -------------------------------------------------------------------- */

typedef struct {
    MhDiskInfo known;
    MhDiskOpts opts;
} Impl;

static bool dev_recheck(MhBlockDev *dev, char *err, size_t errn) {
    Impl *im = dev->impl;
    MhDiskInfo fresh;
    if (!im || !mh_disk_identity_matches(&im->known, &im->opts, &fresh)) {
        mh_err_set(err, errn, "O SD mudou, foi removido ou falhou na verificacao de identidade.");
        return false;
    }
    uint64_t sz = 0;
    uint32_t bs = 0;
    if (!mh_disk_fd_geometry(dev->io.fd, &sz, &bs) || sz != im->known.size || bs != im->known.block_size) {
        mh_err_set(err, errn, "O SD mudou, foi removido ou falhou na verificacao de identidade.");
        return false;
    }
    char names[16][32];
    if (dev->writable && disk_mounts(im->known.bsd_name, names, 16) != 0) {
        mh_err_set(err, errn, "Um volume do SD foi montado durante a operacao; a gravacao foi cancelada por seguranca.");
        return false;
    }
    return true;
}

/* orig: refresh_disk_properties @0x14002cd20 (IOCTL_DISK_UPDATE_PROPERTIES). macOS re-reads
 * the partition map and remounts when the last writer closes the raw node, so here only the
 * two sleeps and a flush; mh_blockdev_close does the closing. */
static void dev_refresh(MhBlockDev *dev) {
    usleep(120 * 1000);
    if (dev->writable) mh_io_flush(&dev->io, 0);
    usleep(250 * 1000);
}

static void dev_release(MhBlockDev *dev) {
    free(dev->impl);
    dev->impl = NULL;
}

static const MhBlockDevOps kOps = {dev_recheck, dev_refresh, dev_release};

static MhBlockDev *open_dev(const MhDiskInfo *known, const MhDiskOpts *opts, bool write, char *err,
                            size_t errn) {
    if (err && errn) err[0] = 0;
    uint32_t num;
    uint32_t flags = opts ? opts->flags : 0;
    if (!known || !mh_disk_valid_bsd_name(known->bsd_name, &num) || num != known->number ||
        !mh_disk_eligible(known, flags, NULL)) {
        mh_err_set(err, errn, "O SD selecionado mudou ou nao e seguro.");
        return NULL;
    }
    MhDiskInfo fresh;
    if (!mh_disk_identity_matches(known, opts, &fresh)) {
        mh_err_set(err, errn, "O SD mudou, foi removido ou falhou na verificacao de identidade.");
        return NULL;
    }
    if (write && !unmount_whole(&fresh, err, errn)) return NULL;

    int fd = mh_disk_open_raw(known->bsd_name, write, NULL);
    if (fd < 0) {
        mh_err_set(err, errn, write ? "Execute como administrador e feche programas usando o SD."
                                    : "Nao foi possivel abrir o SD para leitura.");
        return NULL;
    }
    MhBlockDev *dev = calloc(1, sizeof *dev);
    Impl *im = calloc(1, sizeof *im);
    if (!dev || !im) {
        free(dev);
        free(im);
        close(fd);
        mh_err_set(err, errn, "Memoria insuficiente.");
        return NULL;
    }
    im->known = *known;
    if (opts) im->opts = *opts;
    im->opts.recover = NULL; /* identity checks never need the V2 recovery probe */
    mh_io_attach(&dev->io, fd, write, NULL);
    dev->info = fresh;
    dev->size = fresh.size;
    dev->writable = write;
    dev->flags = flags;
    dev->ops = &kOps;
    dev->impl = im;
    if (!dev_recheck(dev, err, errn)) {
        mh_io_close(&dev->io);
        dev_release(dev);
        free(dev);
        return NULL;
    }
    return dev;
}

MhBlockDev *mh_blockdev_open_read(const MhDiskInfo *known, const MhDiskOpts *opts, char *err,
                                  size_t errn) {
    return open_dev(known, opts, false, err, errn);
}

MhBlockDev *mh_blockdev_open_write(const MhDiskInfo *known, const MhDiskOpts *opts, char *err,
                                   size_t errn) {
    return open_dev(known, opts, true, err, errn);
}
