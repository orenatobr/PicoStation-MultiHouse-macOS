/* mh/blockdev.h: SD card / USB disk discovery and raw device access (interface only).
 *
 * Portable code (sd_write_image, sd_write_project_direct, sd_quick_health, sd_verify_image)
 * uses this header. Implementations: core/src/blockdev (pure helpers, file stand-in for
 * tests) and platform/mac/mh_disk_mac.{h,m,c} (DiskArbitration, IOKit, authopen).
 * Original -> port table: docs/map/disk.md.
 *
 * Safety model (same as the original, stricter where the platform allows):
 *  - Only whole disks named "diskN" (mh_disk_valid_bsd_name) are ever turned into a path.
 *  - A device is opened for writing only through mh_blockdev_open_write, which re-queries the
 *    disk, requires it to be eligible AND identical (number, size, block size, model, serial,
 *    media registry id) to the MhDiskInfo the user picked, unmounts its volumes, opens the
 *    raw node and checks identity once more before returning.
 *  - Disks holding the running system, synthesized (APFS) disks, disk images (unless the test
 *    flag is given), internal fixed disks, disks above 130 GB (the 2 TB external NTFS drive of
 *    a developer machine is excluded by this rule, exactly as in the original) are never
 *    eligible.
 */
#ifndef MH_BLOCKDEV_H
#define MH_BLOCKDEV_H

#include "mh/common.h"
#include "mh/io.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- limits (orig: sd_enumerate scanned PhysicalDrive0..63 and returned at most 0x20) ---- */
#define MH_DISK_MAX 32
#define MH_DISK_MAX_BYTES 130000000000ULL /* orig: size - 1 < 130000000000 (decimal bytes) */
#define MH_DISK_BSD_MAX 16
#define MH_DISK_MODEL_MAX 96  /* orig: 0x60 wide chars at +0x28 */
#define MH_DISK_SERIAL_MAX 64 /* orig: 0x40 wide chars at +0xE8 */
#define MH_DISK_LABEL_MAX 64  /* orig: 0x40 wide chars at +0x168 (volume label) */

/* Flags for enumeration and open. */
#define MH_DISK_ALLOW_IMAGES 0x1u /* test mode: offer disk-image backed devices (hdiutil) */
/* Environment variable that turns MH_DISK_ALLOW_IMAGES on (value "1"). */
#define MH_DISK_TEST_ENV "MH_DISK_TEST_ALLOW_IMAGES"
uint32_t mh_disk_flags_from_env(void);

/* Bus codes: same numbers as the Windows STORAGE_BUS_TYPE values the original compared. */
enum { MH_BUS_OTHER = 0, MH_BUS_USB = 7, MH_BUS_SD = 12, MH_BUS_MMC = 13 };

/* orig: the 0x1e8 byte record filled by query_info (offsets in comments). */
typedef struct MhDiskInfo {
    uint32_t number;              /* +0x00 N of /dev/diskN (orig: PhysicalDriveN) */
    char bsd_name[MH_DISK_BSD_MAX]; /* "disk6" */
    uint32_t bus_type;            /* +0x08 MH_BUS_* from the DiskArbitration protocol */
    int removable;                /* +0x0c removable media flag */
    int is_system;                /* +0x10 holds the running system (or the check failed) */
    int has_v2;                   /* +0x14 PSXRAW header found (or recovered) */
    uint8_t v2_state;             /* +0x18 header byte 6 (shown as "PSXRAW V<n>") */
    uint8_t v2_flags;             /* +0x19 header byte 7 */
    int v2_recovered;             /* header rebuilt from the body by the recover probe */
    uint64_t size;                /* +0x20 bytes */
    uint32_t block_size;          /* bytes per device block (512 required) */
    char model[MH_DISK_MODEL_MAX]; /* +0x28 "<vendor> <product>" through mh_clean_ascii */
    char serial[MH_DISK_SERIAL_MAX]; /* +0xE8 */
    char label[MH_DISK_LABEL_MAX];   /* +0x168 label of the first mounted volume */
    int mounted;                  /* number of mounted volumes of this disk (orig: drive letter) */
    /* macOS additions, used only by the eligibility and identity rules */
    char protocol[24];            /* DiskArbitration protocol: "USB", "Secure Digital", ... */
    int is_internal;
    int is_image;                 /* disk image backed (hdiutil, "Virtual Interface") */
    int is_synthesized;           /* APFS container or other virtual disk over a physical one */
    int is_optical;
    int is_whole;
    int writable;                 /* media not write protected */
    uint64_t media_id;            /* IORegistry entry id of the IOMedia (changes on re-insert) */
} MhDiskInfo;

/* ---- pure helpers (core/src/blockdev) ---------------------------------------------------- */

/* Strict "^disk[0-9]+$" (number at most 6 digits, no leading zeros except "disk0"). On success
 * *number receives N (may be NULL). The only gate between user/UI data and a device path. */
bool mh_disk_valid_bsd_name(const char *name, uint32_t *number);
/* Builds "/dev/rdiskN" (raw) or "/dev/diskN" from a valid name; false if the name is invalid
 * or n is too small. */
bool mh_disk_device_path(const char *bsd_name, bool raw, char *out, size_t n);

/* orig: the criteria inside sd_enumerate, mapped to macOS:
 *   whole disk, not is_system, 1 <= size <= 130e9, block_size 512, size multiple of 512,
 *   not synthesized, not optical, not internal-and-fixed, disk images only with
 *   MH_DISK_ALLOW_IMAGES, and then (bus is USB/SD/MMC) or (removable and mounted) or
 *   (has_v2 and not internal). With MH_DISK_ALLOW_IMAGES an image needs no bus match.
 * On rejection *why (may be NULL) points to a static English reason, for logs only. */
bool mh_disk_eligible(const MhDiskInfo *d, uint32_t flags, const char **why);
/* orig: the checks in identity_matches / sd_refresh_identity beyond system_disks: same number,
 * size, block size, model, serial, and media_id (when both are non-zero). */
bool mh_disk_same_identity(const MhDiskInfo *a, const MhDiskInfo *b);
/* orig: refresh_sd combo text: "<bsd> | <model or Dispositivo USB/SD> | <size> [| <label>]
 * [ | PSXRAW V<n>]". (orig: "PhysicalDriveN | ... | X:").  */
void mh_disk_label_text(const MhDiskInfo *d, char *out, size_t n);
/* Text shown when the list is empty / after a scan (orig refresh_sd). */
#define MH_DISK_TXT_NONE "Nenhum dispositivo USB/SD seguro foi detectado."
#define MH_DISK_TXT_FOUND "Dispositivo detectado. Cartoes V2 existentes sao carregados automaticamente."
#define MH_DISK_TXT_GENERIC_MODEL "Dispositivo USB/SD"

/* ---- discovery (platform/mac) ------------------------------------------------------------ */

/* Optional hook for orig v2_probe_recoverable_physical / recover_v2_header_from_body: called
 * when neither header slot is valid, with a read-only MhIo on the raw disk and its size. If it
 * returns true the disk is reported with has_v2=1, v2_state=2 and v2_recovered=1. */
typedef bool (*MhV2RecoverProbe)(void *user, MhIo *io, uint64_t size);

typedef struct MhDiskOpts {
    uint32_t flags;               /* MH_DISK_ALLOW_IMAGES */
    MhV2RecoverProbe recover;     /* may be NULL */
    void *recover_user;
} MhDiskOpts;

/* orig: sd_enumerate. Fills out[] (at most max, sorted by number) with ELIGIBLE disks only
 * and returns the count. 0 with err set when the system disk could not be determined (orig:
 * system_disks failed -> nothing is offered). opts may be NULL. */
int mh_disk_enumerate(MhDiskInfo *out, int max, const MhDiskOpts *opts, char *err, size_t errn);
/* orig: query_info. Fills *out for disk `number`, including the PSXRAW header state (slot A
 * at byte 0 with state byte 1..3, else slot B at 0xA00 with state 2, each read retried after
 * 5,15,25,...,75 ms). Does not apply eligibility. False if the disk does not exist. */
bool mh_disk_query(uint32_t number, const MhDiskOpts *opts, MhDiskInfo *out);
/* orig: system_disks. Fills numbers[] with the whole disks that hold the running system
 * (physical stores of / and /System/Volumes/Data). Returns the count, 0 on failure. */
int mh_disk_system_numbers(uint32_t *numbers, int max);
/* orig: identity_matches and sd_refresh_identity (same code). Re-queries known->number into
 * *fresh and returns true only if the disk is still eligible and mh_disk_same_identity. */
bool mh_disk_identity_matches(const MhDiskInfo *known, const MhDiskOpts *opts, MhDiskInfo *fresh);

/* ---- device handle ----------------------------------------------------------------------- */

typedef struct MhBlockDev {
    MhIo io;            /* use mh_io_read/mh_io_write/mh_io_flush/... on &dev->io */
    MhDiskInfo info;    /* identity verified at open time */
    uint64_t size;      /* bytes, from the device itself */
    int writable;
    void *impl;         /* platform state: volume lock, mount block */
    uint32_t flags;     /* flags used at open, reused by recheck */
    int standin;        /* regular file used as a fake device (tests) */
} MhBlockDev;

/* Read-only open of the raw disk (no unmount, no privileges needed for /dev/rdiskN readable
 * by group operator). Verifies eligibility and identity like the write path. Returns NULL
 * with err filled (orig style Portuguese text). */
MhBlockDev *mh_blockdev_open_read(const MhDiskInfo *known, const MhDiskOpts *opts, char *err,
                                  size_t errn);
/* orig: identity_matches + lock_volumes + CreateFileW(GENERIC_READ|WRITE) + refresh. Unmounts
 * every volume (refusing busy ones, retry 40,75,..285 ms), blocks remounts, opens the raw node
 * (direct open(2) when allowed, otherwise authopen with the system authorization dialog),
 * F_NOCACHE, and re-verifies identity before returning. */
MhBlockDev *mh_blockdev_open_write(const MhDiskInfo *known, const MhDiskOpts *opts, char *err,
                                   size_t errn);
/* Identity check against the live disk; call again right before the first write if time has
 * passed since open. Same predicate as mh_disk_identity_matches. */
bool mh_blockdev_recheck(MhBlockDev *dev, char *err, size_t errn);
/* orig: refresh_disk_properties. Sleeps 120 ms, asks the system to re-read the partition
 * table, sleeps 250 ms. Call after the final write, before close. */
void mh_blockdev_refresh_properties(MhBlockDev *dev);
/* Flushes, closes the fd, re-enables mounting (orig: unlock/CloseHandle volumes). Frees dev.
 * NULL is a no-op. */
void mh_blockdev_close(MhBlockDev *dev);

/* Test stand-in: a regular file presented as a device (no privileges, never touches a disk).
 * size 0 uses the current file size. The file is created if missing when writable. */
MhBlockDev *mh_blockdev_open_file(const char *path, bool writable, uint64_t size, char *err,
                                  size_t errn);

#ifdef __cplusplus
}
#endif
#endif
