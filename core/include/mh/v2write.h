/* mh/v2write.h: PSXRAW V2 libraries, part B: the writer. Builds the whole card/image layout
 * of mh/v2format.h from a project (cases and discs), streams the disc data from the BIN files
 * (or copies it from an existing card/image), verifies it, and commits the headers last.
 *
 * Map of original functions: docs/map/v2.md (part B). Messages are EN-US translations of the original
 * Portuguese texts (docs/i18n/v2.md lists what stays untranslated). The three targets of the original are all served by mh_v2_write_handle_direct:
 *   1. a regular image file     mh_v2_write_image_direct / mh_v2_build_image ("Image > Save As")
 *   2. a block device           sd_write_project_direct (part A) opens the device and calls
 *                               mh_v2_write_handle_direct with phys_id = the disk number
 *   3. resume of a partial run  automatic: discs flagged MH_DF_KEEP_PACKAGE that belong to the
 *                               target ("blue: already written") are not streamed again
 */
#ifndef MH_V2WRITE_H
#define MH_V2WRITE_H

#include "mh/v2.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================ environment */

/* Everything the writer needs besides the project. */
typedef struct MhV2Writer {
    const MhV2Env *env;      /* env->menu / menu_size: picostation_menu.bin (required);
                                env->db: trackset identification during the analysis pass;
                                env->cheats: cheat catalog binding (NULL = the active database) */
    const uint8_t *music;    /* umbrella_music.vag (required by the writer, 1..0x22EC00 bytes) */
    uint64_t music_size;
    MhProgress *prog;        /* optional; cancel stops the write at a safe point */
    /* Timestamp stored in the "PSTRSM1" resume block (orig: QueryPerformanceCounter). The only
     * time dependent bytes of a written library. NULL = monotonic nanoseconds. */
    uint64_t (*stamp)(void *user);
    void *stamp_user;
} MhV2Writer;

/* ================================================================== progress phases */

/* Machine-readable phase of a V2 operation. The writer stores it in MhProgress.phase (with
 * step / steps) BEFORE every progress callback, so the UI reads prog->phase inside cb and never
 * parses the human readable text (which is EN-US and may change). Phases 1..MH_V2_PHASE_STEPS
 * are the steps of "Step n of N"; DONE is the end state, shown as completed.
 *   PREPARING          checks, menu and table preparation, before the first byte is written.
 *   VERIFYING_SOURCES  full read of a source Disc (analysis pass: sector checks, hashes) and
 *                      manual verification of an existing RAW Disc.
 *   WRITING_DISCS      streaming the Discs. step = 1-based index of the Disc being handled,
 *                      steps = number of Discs of the project. Disc k is flagged finished
 *                      (MH_DF_KEEP_PACKAGE, "blue") before the report of Disc k+1 or of the
 *                      next phase, so the UI can refresh its rows when step changes or the
 *                      phase leaves WRITING_DISCS.
 *   UPDATING_TABLES    fixed zone: tables, art, menu (sync then compare). From here on the
 *                      card is being committed and cancelling is no longer safe.
 *   COMMITTING_HEADER  header B then header A are published.
 *   VERIFYING_CARD     read-back checks of the card (compare against the image, header
 *                      re-read after reopening, read test).
 *   DONE               the operation finished successfully. */
typedef enum MhV2Phase {
    MH_V2_PHASE_NONE = 0,
    MH_V2_PHASE_PREPARING = 1,
    MH_V2_PHASE_VERIFYING_SOURCES = 2,
    MH_V2_PHASE_WRITING_DISCS = 3,
    MH_V2_PHASE_UPDATING_TABLES = 4,
    MH_V2_PHASE_COMMITTING_HEADER = 5,
    MH_V2_PHASE_VERIFYING_CARD = 6,
    MH_V2_PHASE_DONE = 7
} MhV2Phase;
#define MH_V2_PHASE_STEPS 6 /* "Step n of 6": PREPARING .. VERIFYING_CARD */

/* Sets the phase fields of prog (NULL is ignored). step / steps = 0 when the phase has no items. */
static inline void mh_v2_set_phase(MhProgress *prog, MhV2Phase phase, int step, int steps)
{
    if (!prog) return;
    atomic_store(&prog->phase, (int)phase);
    atomic_store(&prog->step, step);
    atomic_store(&prog->steps, steps);
}

/* ========================================================================== extent list */

/* Sorted, non overlapping [start, end) ranges of 512-byte blocks used by disc packages
 * (orig: 16 byte pairs in a stack array, at most 256, count in a u32). */
typedef struct MhV2Extent {
    uint64_t start;
    uint64_t end;
} MhV2Extent;
#define MH_V2_EXTENTS_MAX 256

/* orig: extent_insert. start must be >= 0x2800, a multiple of 256 and below end; the range
 * must not overlap a neighbour and the list must not be full. */
bool mh_v2_extent_insert(MhV2Extent *ext, uint32_t *count, uint64_t start, uint64_t end);
/* orig: extent_allocate. First fit at or after block 0x2800, 256-block aligned, of `size`
 * blocks. cap (blocks, 0 = unlimited) bounds the end. On success the range is inserted and
 * *out receives its start. */
bool mh_v2_extent_allocate(MhV2Extent *ext, uint32_t *count, uint64_t size, uint64_t cap,
                           uint64_t *out);

/* ============================================================================== estimate */

/* orig: v2_estimate_project_bytes. Bytes the library occupies (end of the last package, at
 * least 0x500000) for the target the project was loaded from; 0 on failure. */
uint64_t mh_v2_estimate_project_bytes(const MhProject *p);
/* orig: v2_estimate_project_bytes_for_target. phys_id is the disk number (-1 for an image
 * file), path the image path (NULL for a card). Preserved packages keep their LBA, the others
 * are allocated first fit. 0 when preserved extents overlap or no room can be found. The
 * original's constprop variant (v2_estimate_project_bytes_for_target.constprop.0) is the case
 * phys_id == -1. */
uint64_t mh_v2_estimate_project_bytes_for_target(const MhProject *p, int32_t phys_id,
                                                 const char *path);

/* ================================================================ package metadata, preflight */

/* orig: build_package_metadata.constprop.0. Builds the 0x6400 byte package header of disc `d`
 * into out (track map at +0, cheats at +0xA00, Disc art at +0x3C00) and stores the cheat blob
 * size in the u32 at rec+0x6C. err (char[0x100]) receives the failure text. */
bool mh_v2_build_package_metadata(const MhDisc *d, uint8_t rec[MH_V2_DISC_SIZE],
                                  uint8_t out[MH_V2_PKG_META_BYTES], char *err, size_t errn);

/* orig: serial_preflight_error. Fills res with "Write not started. Case: ...". d may be NULL. */
bool mh_v2_serial_preflight_error(MhV2Result *res, const MhCase *c, int disc_index,
                                  const MhDisc *d, const char *msg);
/* orig: preflight_serials.constprop.0 (and v2_prepare_write, which only forwards to it).
 * Every disc must exist, be prepared and have its observations accepted. */
bool mh_v2_preflight_serials(const MhProject *p, MhV2Result *res);
bool mh_v2_prepare_write(const MhProject *p, MhV2Result *res);

/* ===================================================================== stream_disc */

typedef enum {
    MH_V2_STREAM_WRITE = 0,   /* write the RAW data of d to dst at rec.raw_lba, compute CRC/SHA-1 */
    MH_V2_STREAM_ANALYZE = 1  /* read only: sector checks, track hashes, trackset match */
} MhV2StreamMode;

/* orig: stream_disc @0x1400312f0. d is a CUE/BIN disc (kind 1) read from its BIN files or a
 * V2 disc (kind 4/5) read from its card/image. rec (0x100 bytes) provides raw_lba (+0x4C) and
 * receives crc32 (+0x7C) and sha1 (+0x80). *done / total feed the progress. In ANALYZE mode
 * dst may be NULL and the disc gets its verify_issues bits (8 sector format, 0x10 card data,
 * 0x20 extra padding, 0x40 EDC, 0x80 ECC), its trackset identification and, when confirmed by
 * the exact trackset, the automatic cheat catalog. In WRITE mode a disc that was verified
 * earlier (d->verified) must reproduce the stored CRC-32 and SHA-1. */
bool mh_v2_stream_disc(const MhV2Writer *w, MhIo *dst, MhDisc *d, uint8_t rec[MH_V2_DISC_SIZE],
                       uint64_t *done, uint64_t total, MhV2StreamMode mode);

/* ================================================================== admission of discs */

/* orig: verification_issue_text. Rebuilds d->verify_text from d->verify_issues. */
void mh_v2_verification_issue_text(MhDisc *d);
/* orig: v2_check_disc_source. Full read of a disc (analysis pass); sets verified, the issue
 * bits, the CRC/SHA-1 and the text, and d->status (1 OK, 2 observation). */
bool mh_v2_check_disc_source(const MhV2Writer *w, MhDisc *d);
/* orig: v2_verify_disc_source. Manual verification of an existing RAW disc of a loaded card
 * or image against the CRC-32/SHA-1 stored in the library. msg receives the result text. */
bool mh_v2_verify_disc_source(const MhV2Writer *w, const MhDisc *d, char *msg, size_t msgn);
/* orig: v2_admit_disc_without_full_check. Cheap admission: the BIN files exist and have the
 * sizes recorded when the CUE was parsed. */
bool mh_v2_admit_disc_without_full_check(MhDisc *d);
/* orig: manager_admit. full != 0: full read (mh_v2_check_disc_source) and, when there are
 * observations, ask(ctx, text) must accept them ("Add anyway?"). */
bool mh_v2_manager_admit(const MhV2Writer *w, MhDisc *d, bool full,
                         bool (*ask)(void *ctx, const char *text), void *ctx);
/* orig: auto_attach_cheats. Binds the complete cheat catalog of the game to a disc that has a
 * serial and no profile yet; on failure the reason goes to d->cheat_note. env->cheats NULL
 * means the active database. */
void mh_v2_auto_attach_cheats(const MhV2Env *env, MhDisc *d);

/* ==================================================================== the writer itself */

/* orig: v2_write_handle_direct @0x14003a850. Writes the project into `io` (an image file or a
 * device through the blockdev interface). phys_id: disk number of a card, -1 for an image;
 * resume_path: the image path (NULL or "" for a card); capacity: bytes available in the
 * target. The library is committed at the end: header B, then header A. A failure or cancel
 * before that leaves header A invalid (the card is "uncommitted"); discs already finished are
 * flagged MH_DF_KEEP_PACKAGE (with their resume block on the target) so that the next run
 * with the same project, in the same session, only writes the pending ones.
 * res (may be NULL): ok, cases, discs, size (= bytes of the library) or the failure text. The
 * project's generation counter (reserved_c) is NOT updated; reload the library after a write. */
bool mh_v2_write_handle_direct(const MhV2Writer *w, MhProject *p, MhIo *io, int32_t phys_id,
                               const char *resume_path, uint64_t capacity, MhV2Result *res);

/* orig: v2_write_image_direct. Writes the project into a regular file: refuses a path that
 * would overwrite a source file, extends the file to the estimated size, calls the handle
 * writer, and cuts the file to the library size at the end. */
bool mh_v2_write_image_direct(const MhV2Writer *w, MhProject *p, const char *path, MhV2Result *res);
/* orig: v2_build_image (the original forwards to v2_write_image_direct). */
bool mh_v2_build_image(const MhV2Writer *w, MhProject *p, const char *path, MhV2Result *res);

/* ================================================== readiness checks and UI texts (data) */

/* orig: all_ready, text part. Returns true when every disc of every used case is usable and
 * there is at least one disc; *has_observations is set when some disc has status MH_DISC_STATUS_NOTE.
 * On false, msg holds the text the original showed in a message box. */
bool mh_v2_all_ready(const MhProject *p, bool *has_observations, char *msg, size_t msgn);

/* Texts of cmd_write / write_image_direct_to / write_failure (EN-US). */
#define MH_V2W_BLOCKED_DB \
    "WRITE BLOCKED: the built-in identification, hash and SBI database failed internal validation. Reinstall the official app."
#define MH_V2W_BLOCKED_CHEATS \
    "WRITE BLOCKED: the audited external cheat database must stay in the database/cheats folder next to the app."
#define MH_V2W_NEED_GAME "Add at least one game before writing."
#define MH_V2W_NEED_SD "Select an SD card."
#define MH_V2W_OTHER_CARD \
    "This project was loaded from another card. Select the same disk to apply the changes."
#define MH_V2W_NO_FIT "The games do not fit on the selected card."
#define MH_V2W_NOT_PREPARED "Some Discs have not been prepared for writing yet."
#define MH_V2W_SD_FAILED "Direct write to the card failed."
#define MH_V2W_IMAGE_FAILED "Could not write the V2 image."
#define MH_V2W_IMAGE_TITLE "Save PSXRAW V2 image"
#define MH_V2W_IMAGE_STATUS "Writing V2 image..."
#define MH_V2W_IMAGE_DONE "V2 image finished and loaded for editing."
#define MH_V2W_IMAGE_NOT_REOPENED "The image was written, but it could not be reopened for editing."
#define MH_V2W_SD_STATUS "Writing pending Discs and updating the library..."
#define MH_V2W_SD_DONE "Done: pending Discs written; library updated."
#define MH_V2W_FAIL_STATUS "Write interrupted. Reconnect the same card and apply again without closing the app."
#define MH_V2W_CANCEL_STATUS "Cancelled. Keep the app open and apply again to resume the pending Discs."
#define MH_V2W_CONFIRM_TITLE "Confirm changes to the card"
#define MH_V2W_MSGBOX_TITLE "PicoStation Multi House V3"

/* The two "WRITE BLOCKED" texts shared by cmd_write and write_image_direct_to, in that
 * order (NULL = both databases are fine). db_ok: embedded identification/hash/SBI database
 * validated; cheats_ok: audited cheat database loaded. */
const char *mh_v2_blocked_text(bool db_ok, bool cheats_ok);

/* Why the user cannot start a card write, in the order cmd_write tests (NULL = may go on):
 *   db_ok       embedded database validated (DAT_140e7c3b8 and DAT_140e7c410 both set)
 *   cheats_ok   cheat database loaded
 *   has_disk    a card is selected;  disk_number / disk_size: that card
 * all_ready is run by the caller before (its own text). */
const char *mh_v2_write_precheck(const MhProject *p, bool db_ok, bool cheats_ok, bool has_disk,
                                 uint32_t disk_number, uint64_t disk_size, uint64_t *estimate);
/* The confirmation text of cmd_write (CRLF line ends). drive is the device name shown as
 * "PhysicalDrive<N>" in the original; the macOS UI passes e.g. "disk4" (the word
 * "PhysicalDrive" is dropped: drive replaces it). */
void mh_v2_write_confirm_text(const MhProject *p, const char *drive, const char *model,
                              const char *serial, uint64_t estimate, char *out, size_t outn);
/* The completion text of cmd_write ("Operation completed directly on the card ..."). */
void mh_v2_write_done_text(uint16_t cases, uint16_t discs, uint64_t size, char *out, size_t outn);
/* write_failure: the box text (cancelled = false) or the status line (cancelled = true). */
void mh_v2_write_failure_text(const char *msg, bool cancelled, char *out, size_t outn);

#ifdef __cplusplus
}
#endif
#endif
