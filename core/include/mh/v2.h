/* mh/v2.h: PSXRAW V2 libraries, part A: validation, loading (image or card), header
 * publishing and the SD front ends. Part B (the writer: v2_write_handle_direct and friends)
 * lives in the writer's own header. The binary layout is in mh/v2format.h.
 *
 * Map of original functions: docs/map/v2.md. Messages are the original texts, byte for byte.
 * Everything that touches a physical card goes through mh/blockdev.h (MhBlockDev / MhIo). */
#ifndef MH_V2_H
#define MH_V2_H

#include "mh/base.h"
#include "mh/blockdev.h"
#include "mh/cheat.h"
#include "mh/disc.h"
#include "mh/menu.h"
#include "mh/v2format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================== results */

/* The original's 0x430 byte result block (ok u32, code u32, cases u16, discs u16, u64 size,
 * u32 crc, u8 sha1[20], WCHAR msg[0x200] at +0x30). All the V2 entry points fill one when
 * given. A failure sets ok = 0 and msg; code is an errno-like value (0 if none). */
typedef struct MhV2Result {
    int ok;
    int code;
    uint16_t cases;    /* +8 */
    uint16_t discs;    /* +10 */
    uint64_t size;     /* +0x10 image size (validate/load) or bytes written (writer) */
    uint32_t crc32;    /* +0x18 whole-file CRC-32 (validate only) */
    uint8_t sha1[20];  /* +0x1c whole-file SHA-1 (validate only) */
    char msg[MH_ERR_MSG_MAX];
} MhV2Result;

/* orig: result_msg.part.0: ok = 0 and the text; the other members keep their values. res may
 * be NULL. */
void mh_v2_result_fail(MhV2Result *res, const char *msg);
/* orig: result_io_msg.part.0: ok = 0, code, and "<msg> (error <code>)" when code != 0. */
void mh_v2_result_fail_code(MhV2Result *res, const char *msg, int code);

/* ======================================================================= environment */

/* What the loader needs besides the card: the embedded menu (a recovered header carries no
 * menu data, and a recovered card is shown with the default one), the disc database and the
 * cheat database used by hydrate. All members are optional (NULL = unavailable). */
typedef struct MhV2Env {
    const uint8_t *menu;       /* resources/embedded/picostation_menu.bin */
    uint64_t menu_size;        /* multiple of 0x930, 1..93 sectors */
    const MhDb *db;            /* disc database (db_apply_disc, art lookup) */
    const MhCheatDb *cheats;   /* embedded cheat database, NULL = "database unavailable" */
} MhV2Env;

/* ================================================================== header slot helpers */

/* orig: sd_v2_header_raw_valid @0x14002ec70. Looser than mh_v2_header_raw_valid: plain CRC,
 * no count / MBR checks. Used by the SD front ends. */
bool mh_sd_v2_header_raw_valid(const uint8_t *h);

/* orig: v2_invalidate_header_slot @0x140036a50. Reads 512 bytes at `off`; if they start with
 * "PSXRAW" writes 512 zero bytes (verified); anything else is left alone and counts as
 * success. Errors are recorded in io->err. */
bool mh_v2_invalidate_header_slot(MhIo *io, uint64_t off);
/* orig: v2_publish_header_slot @0x140038830. Writes blocks 1..4 of the 0xA00 slot, then block
 * 0 (the one carrying "PSXRAW"), each written, flushed and read back; finally re-reads the
 * whole slot and compares. hdr must be the 0xA00 slot. */
bool mh_v2_publish_header_slot(MhIo *io, uint64_t off, const uint8_t *hdr);
/* orig: sd_invalidate_v2_header @0x14002e380 and sd_publish_v2_header @0x14002e470: the same
 * operations with their own retry schedule (8 tries, 5,15,...,75 ms for reads and
 * 30,65,...,275 ms for writes) and no MhIoErr record. */
bool mh_sd_invalidate_v2_header(MhIo *io, uint64_t off);
bool mh_sd_publish_v2_header(MhIo *io, uint64_t off, const uint8_t *hdr);
/* orig: sd_write_block_verified @0x14002ef50. */
bool mh_sd_write_block_verified(MhIo *io, uint64_t off, const void *block512);

/* orig: clear_backup_gpt_tail @0x140010d30. dev_size: size of the card in bytes, used: bytes
 * covered by the image/library. If the last block of the card starts with "EFI PART" every
 * non zero 512-byte block of the last 1 MiB beyond `used` is zeroed (verified). Texts:
 * "Could not check the last sector of the card.", "Could not read the backup GPT area.",
 * "Could not remove a backup GPT sector with a 512-byte read-back." */
bool mh_clear_backup_gpt_tail(MhIo *io, uint64_t dev_size, uint64_t used, char *err, size_t errn);

/* orig: compare_region @0x140012620. Compares [off, off+len) of two devices/files in 256 KiB
 * pieces (8 read tries each). `done_base` / `total` feed the progress ("Comparando SD byte a
 * byte"). */
bool mh_v2_compare_region(MhIo *a, MhIo *b, uint64_t off, uint64_t len, MhProgress *prog,
                          uint64_t done_base, uint64_t total, char *err, size_t errn);

/* =============================================================================== validate */

/* orig: v2_validate_image @0x1400389e0. Full structural validation of an image file: both
 * headers, the Case and Disc tables (links, CRCs, layouts), every track map, cheat blob, art
 * ids, the RAW data of every disc (CRC-32/SHA-1 and zero padding), the menu, the global
 * config and its music, then the CRC-32/SHA-1 of the whole file. On success res holds the
 * counts, the size, the CRC and the SHA-1. Progress phase text: "Validando dados RAW V2". */
bool mh_v2_validate_image(const char *path, MhProgress *prog, MhV2Result *res);

/* ==================================================================================== load */

typedef enum {
    MH_V2_SRC_CARD = 4,  /* a physical card (disc kind MH_DISC_KIND_V2_DEVICE) */
    MH_V2_SRC_IMAGE = 5  /* an image file (MH_DISC_KIND_V2_IMAGE) */
} MhV2SourceKind;

/* orig: recover_v2_header_from_body @0x14002bf00. Rebuilds a header from the Case/Disc
 * tables and the config sector when both header slots are gone. Refuses a card whose first
 * block says "PSTWIP1" (interrupted write), a source smaller than 5 MiB, tables with any
 * inconsistency. hdr receives the 0x930 byte header. env->menu supplies the menu size. */
bool mh_v2_recover_header_from_body(MhIo *io, uint64_t size, const MhV2Env *env, uint8_t hdr[MH_V2_HDR_SIZE]);
/* orig: v2_probe_recoverable_physical @0x140038770. Matches MhV2RecoverProbe (blockdev.h):
 * user is a const MhV2Env *. */
bool mh_v2_recover_probe(void *user, MhIo *io, uint64_t size);

/* orig: v2_load_source @0x140036c10. Loads a V2 library from `io` (already open, read only)
 * into *project, which is replaced on success and left untouched on failure. The source is
 * either an image file (image_path is stored in the project and in every disc) or a card
 * (disk_number is stored). cache_dir receives menu.bin, CASE-<n>.art, DISC-<n>.art and
 * DISC-<n>.sbi (the original used %TEMP%\PicoStationV2Cache-PD<N>); it is created if needed.
 * res (may be NULL) gets ok, counts, size and the informative message.
 * On a card with damaged multi-track maps the project loads with discs in the "corrigivel"
 * state, which blocks applying until the original CUEs are supplied. */
bool mh_v2_load_io(MhIo *io, MhV2SourceKind kind, uint32_t disk_number, uint64_t size,
                   const char *image_path, const char *cache_dir, const MhV2Env *env,
                   MhProject *project, MhV2Result *res);
/* orig: v2_load_image @0x140036ae0 (opens the file, size from the file). */
bool mh_v2_load_image(const char *path, const char *cache_dir, const MhV2Env *env,
                      MhProject *project, MhV2Result *res);
/* orig: v2_load_physical @0x140036b70. Opens the card read-only through mh_blockdev_open_read. */
bool mh_v2_load_physical(const MhDiskInfo *disk, const MhDiskOpts *opts, const char *cache_dir,
                         const MhV2Env *env, MhProject *project, MhV2Result *res);

/* orig: hydrate_loaded_project @0x14001c640. Runs after a successful load: for every disc
 * with a serial, redoes the database match, completes or rebuilds the cheat catalog from the
 * embedded database, looks for default case/disc art, and marks the project dirty when
 * anything was added. */
void mh_v2_hydrate_project(MhProject *project, const MhV2Env *env);

/* ================================================================================ resume */

/* orig: confirm_session_marker @0x140012d60. True when the 512-byte marker block of the disc
 * package (at v2_pkg_b + 0xFF) still equals the copy in d->v2_marker. */
bool mh_v2_confirm_session_marker(MhIo *io, const MhDisc *d, uint64_t card_bytes);
/* orig: v2_confirm_resume_target @0x140035680. Before writing: every disc flagged
 * MH_DF_KEEP_PACKAGE that belongs to this target (disk number or image path) must still
 * show its marker. disk_number is -1 for an image file. path may be NULL / "" for a card.
 * Failure text: "The target does not match the resume point. Reconnect the same card used
 * in this session." */
bool mh_v2_confirm_resume_target(const MhProject *project, MhIo *io, int32_t disk_number,
                                 const char *path, uint64_t card_bytes, MhV2Result *res);

/* ==================================================================== SD front ends */

/* orig: verify_committed_headers_after_reopen @0x14003da90. Waits 250 ms, reopens the card
 * read-only, reads headers A and B (8 tries), requires both to pass
 * mh_sd_v2_header_raw_valid, to be identical to each other and to `expected` (0xA00 bytes). */
bool mh_v2_verify_committed_headers(const MhDiskInfo *disk, const MhDiskOpts *opts,
                                    const uint8_t *expected, char *err, size_t errn);

/* orig: sd_verify_image @0x14002ed50. Compares an image file byte by byte with the start of
 * the card (read-only). */
bool mh_sd_verify_image(const MhDiskInfo *disk, const MhDiskOpts *opts, const char *image,
                        MhProgress *prog, char *err, size_t errn);

/* orig: sd_write_image @0x14002f0d0. Copies a valid V2 image to the card: header A then B
 * are invalidated first, the body (from 0x1400) is copied and read back, the backup GPT tail
 * is cleared, then B and A are published. Reopens and verifies the headers at the end. */
bool mh_sd_write_image(const MhDiskInfo *disk, const MhDiskOpts *opts, const char *image,
                       MhProgress *prog, char *err, size_t errn);

/* orig: sd_quick_health 0x220 byte result (ok u32, bytes u64 at +8, speed at +0x10 in 1/100
 * MiB/s, read_errors at +0x14, mismatches at +0x18, text at +0x1C). */
typedef struct MhSdHealth {
    int ok;
    uint64_t bytes;       /* bytes read (both passes) */
    uint32_t speed_x100;  /* MiB/s * 100 */
    uint32_t read_errors; /* regions with a persistent read failure */
    uint32_t mismatches;  /* regions whose two reads differ */
    char text[256];
} MhSdHealth;
/* orig: sd_quick_health @0x14002e590. Reads four regions (start, 1/4, 1/2 and the last MiB)
 * twice each and compares the two reads. out may be NULL (then the return value says whether
 * anything was read). */
bool mh_sd_quick_health(const MhDiskInfo *disk, const MhDiskOpts *opts, MhProgress *prog,
                        MhSdHealth *out);

/* orig: sd_write_project_direct @0x14002fae0. Writes a project to a card with the writer of
 * v2write.h: identity check, size check, unmount + open, mh_v2_prepare_write, resume target
 * check, backup GPT tail, mh_v2_write_handle_direct, header A check, close, reopen and verify
 * the headers. verify_full only selects the final text ("... fully verified." or
 * "... quick mode ..."). err gets the message of success or failure. */
typedef struct MhV2Writer MhV2Writer;
bool mh_sd_write_project_direct(const MhDiskInfo *disk, const MhDiskOpts *opts, MhProject *project,
                                bool verify_full, const MhV2Writer *w, char *err, size_t errn,
                                MhV2Result *res);

/* ============================================================================== recode */

/* orig: manager_recode_copy @0x140022e80. Creates a copy of a verified CUE/BIN disc in
 * "<dst stem>_BIN_EDC_ECC/BIN_<n>.bin" with the EDC/ECC of every data sector recomputed, and
 * a CUE at dst_cue that points at the copies. The source is never modified. Returns false
 * with err set (and nothing left on disk) when the copy fails, is cancelled, the source
 * changed since verification, or no sector needed a change ("No EDC/ECC code needed to
 * change."). Needs d->verified, d->verify_sha1 and the verify_issues bits 0x40/0x80. */
bool mh_manager_recode_copy(const MhDisc *d, const char *dst_cue, MhProgress *prog, char *err,
                            size_t errn);

#ifdef __cplusplus
}
#endif
#endif
