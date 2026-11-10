/* platform/mac/mh_disk_mac.h: macOS disk layer internals (see mh/blockdev.h for the public API).
 *
 * mh_disk_mac.m : enumeration, identity, unmount (DiskArbitration + IOKit)
 * mh_disk_mac.c : privileged raw device open (direct open(2) or authopen over SCM_RIGHTS)
 * Original -> port table: docs/map/disk.md. */
#ifndef MH_DISK_MAC_H
#define MH_DISK_MAC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mh/blockdev.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MH_AUTHOPEN_PATH "/usr/libexec/authopen"

/* Runs `authopen -stdoutpipe -o <oflags> <path>` and receives the descriptor it passes back
 * over its stdout socket (SCM_RIGHTS). This shows the standard macOS authorization dialog
 * when the caller lacks access. Waits at most timeout_ms (the dialog is interactive).
 * Returns the fd (>= 0) or -1 with errno set (EACCES when the user denied or timed out).
 * path is used verbatim: callers must have validated it (mh_disk_device_path). */
int mh_authopen(const char *path, int oflags, int timeout_ms);

/* Opens /dev/rdiskN (N from a validated BSD name). write=false: plain open(2) O_RDONLY.
 * write=true: open(2) O_RDWR first (works for disk images owned by the user), and only if that
 * fails with EACCES/EPERM falls back to mh_authopen. Sets F_NOCACHE, checks that the fd is a
 * character device. Returns the fd or -1 (errno). *used_auth (may be NULL) tells which path. */
int mh_disk_open_raw(const char *bsd_name, bool write, int *used_auth);

/* ---- helpers ---- */

/* Size of the device from the fd itself (DKIOCGETBLOCKCOUNT * DKIOCGETBLOCKSIZE). */
bool mh_disk_fd_geometry(int fd, uint64_t *size, uint32_t *block_size);

#ifdef __cplusplus
}
#endif
#endif
