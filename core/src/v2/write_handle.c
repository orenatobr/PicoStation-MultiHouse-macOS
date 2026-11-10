/* The V2 writer (orig: v2_write_handle_direct @0x14003a850). It lays out the whole library in
 * memory (fixed zone), streams the disc packages to the target, and commits the headers last. */
#include "write_internal.h"

#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define MENU_BAD "O arquivo menu\\menu.bin e invalido ou excede a area reservada."

typedef struct Item {
    MhDisc *d;
    uint8_t *rec;     /* 0x100 byte Disc record inside the zone */
    bool in_place;    /* raw data already at the target (card/image being edited) */
    bool resumed;     /* package written earlier in this session, marker still on the target */
} Item;

typedef struct Ctx {
    const MhV2Writer *w;
    MhProject *p;
    MhIo *io;
    int32_t phys;
    const char *path;         /* image path or NULL */
    uint64_t cap;             /* bytes */
    MhV2Result *res;
    uint8_t *zone;            /* the 0x500000 byte fixed zone */
    uint8_t *tbl;             /* 0x6400 byte package metadata scratch */
    uint8_t *menu;            /* private copy of the menu, patched with the user defaults */
    uint32_t menu_sectors;
    Item items[MH_V2_MAX_DISCS];
    unsigned nitems;
    MhV2Extent ext[MH_V2_EXTENTS_MAX];
    uint32_t next;            /* extent count */
    uint64_t max_end1, max_end2, to_write;
    uint32_t cases, discs;
    int cur_no;               /* orig: DAT_140f08ea8, 1-based number of the disc being written */
    char cur_name[0x21];      /* orig: DAT_140f08eac */
} Ctx;

static bool path_nonempty(const char *s) { return s && s[0]; }

/* ---- failure reporting ------------------------------------------------------------------ */

/* Appends the details of the recorded I/O error (and the disc being written) to the message,
 * like the common tail of the original (LAB_14003abc2). */
static void augment(Ctx *c)
{
    MhWbuf w;
    if (!c->res || !c->io->err->set) return;
    mh_wbuf_init(&w);
    mh_wbuf_puts(&w, c->res->msg);
    if (c->cur_no != 0) {
        mh_wbuf_puts(&w, "\r\nDisco ");
        mh_wbuf_u32(&w, (uint32_t)c->cur_no);
        mh_wbuf_puts(&w, ": ");
        mh_wbuf_puts(&w, c->cur_name);
    }
    mh_ioerr_append(&w, c->io->err);
    c->res->ok = 0;
    c->res->code = c->io->err->code;
    mh_str_copy(c->res->msg, MH_V2W_MSG_MAX, w.data ? w.data : "");
    mh_wbuf_free(&w);
}

static bool fail(Ctx *c, const char *msg)
{
    if (c->res) {
        if (msg) mh_v2w_msg(c->res, msg);
        else c->res->ok = 0;
        augment(c);
    }
    return false;
}

/* orig: result_io_msg with GetLastError(): the code is the recorded I/O error, if any. */
static bool fail_io(Ctx *c, const char *msg)
{
    if (c->res) {
        mh_v2w_io_msg(c->res, msg, c->io->err->set ? c->io->err->code : 0);
        augment(c);
    }
    return false;
}

/* ---- classification of a disc against the target ---------------------------------------- */

static void slot_cmp(int32_t phys, const MhDisc *d, bool *resumed)
{
    if (phys != -1 && d->v2_pkg_path[0] == 0) *resumed = (d->v2_slot == phys);
}
static void path_cmp(const char *path, const MhDisc *d, bool *resumed)
{
    if (d->v2_pkg_path[0] != 0) *resumed = mh_path_equal_full(d->v2_pkg_path, path);
}
static void path_nonnull(int32_t phys, const char *path, const MhDisc *d, bool *resumed)
{
    if (path[0] != 0) path_cmp(path, d, resumed);
    else slot_cmp(phys, d, resumed);
}

/* in_place: the raw slots are already on the target; resumed: the package was finished by an
 * earlier pass of this session. orig: the nest of tests at LAB_14003b1d5..LAB_14003b842. */
void mh_v2w_classify(int32_t phys, const char *path, const MhDisc *d, bool *in_place, bool *resumed)
{
    bool keep = (d->flags & MH_DF_KEEP_PACKAGE) != 0;
    *in_place = false;
    *resumed = false;
    if (d->kind == MH_DISC_KIND_V2_DEVICE) {
        if (phys == -1) {
            if (keep && path_nonempty(path)) path_cmp(path, d, resumed);
        } else if (phys == (int32_t)d->v2_drive) {
            *in_place = true;
        } else if (keep) {
            if (path) path_nonnull(phys, path, d, resumed);
            else slot_cmp(phys, d, resumed);
        }
        return;
    }
    if (d->kind == MH_DISC_KIND_V2_IMAGE && path) {
        if (path[0] == 0) {
            if (keep) slot_cmp(phys, d, resumed);
        } else if (mh_path_equal_full(d->source_path, path)) {
            *in_place = true;
        } else if (keep) {
            path_cmp(path, d, resumed);
        }
        return;
    }
    if (keep) {
        if (path) path_nonnull(phys, path, d, resumed);
        else slot_cmp(phys, d, resumed);
    }
}

/* ---- stage 1: the menu ------------------------------------------------------------------ */

static bool prepare_menu(Ctx *c)
{
    const MhV2Env *env = c->w->env;
    char err[0x100];
    if (!env || !env->menu || env->menu_size == 0) return false;
    c->menu = mh_alloc((size_t)env->menu_size);
    if (!c->menu) return false;
    memcpy(c->menu, env->menu, (size_t)env->menu_size);
    if (c->p->has_menu_defaults) {
        if (!mh_menu_defaults_valid(c->p->menu_defaults) ||
            !mh_menu_defaults_patch(c->menu, env->menu_size, c->p->menu_defaults))
            return false;
    }
    if (!mh_menu_validate_blob(c->menu, env->menu_size, MH_V2_SECTOR, err, sizeof err)) return false;
    c->menu_sectors = (uint32_t)(env->menu_size / MH_V2_SECTOR);
    return true;
}

/* ---- stage 2: validation of the project and the preserved extents ----------------------- */

/* A disc flagged KEEP_PACKAGE that belongs to this target must still carry its marker. */
static bool resume_point_ok(Ctx *c, const MhDisc *d)
{
    uint64_t lba;
    bool slot_case;
    uint8_t blk[MH_V2_BLOCK];
    if (!(d->flags & MH_DF_KEEP_PACKAGE)) return true;
    if (!path_nonempty(c->path)) {
        if (!(c->phys != -1 && d->v2_pkg_path[0] == 0 && c->phys == d->v2_slot)) return true;
        slot_case = true;
    } else {
        if (d->v2_pkg_path[0] == 0 || !mh_path_equal_full(d->v2_pkg_path, c->path)) return true;
        slot_case = false;
    }
    lba = d->v2_pkg_b + 0xff;
    /* only the card case compares the size; an image file may have grown or shrunk */
    if (slot_case && c->cap != d->v2_card_blocks) return false;
    if (lba >= c->cap / MH_V2_BLOCK) return false;
    if (!mh_io_read(c->io, lba << 9, blk, sizeof blk)) return false;
    return memcmp(blk, d->v2_marker, sizeof blk) == 0;
}

static bool validate_project(Ctx *c)
{
    unsigned ci, j;
    static const char incomplete[] = "Existe disco com estrutura incompleta.";
    c->max_end1 = MH_V2_FIXED_BLOCKS;
    for (ci = 0; ci < MH_PROJECT_MAX_CASES; ci++) {
        const MhCase *cs = &c->p->cases[ci];
        if (!cs->in_use) continue;
        if (cs->disc_count - 1u > 4u) return fail(c, "Quantidade de discos invalida.");
        c->cases++;
        if (cs->art_path[0]) {
            uint64_t sz;
            if (!mh_file_size64(cs->art_path, &sz) || sz != MH_V2_ART_SIZE)
                return fail(c, "Case Art invalida.");
        }
        for (j = 0; j < cs->disc_count; j++) {
            const MhDisc *d = cs->discs[j];
            uint64_t kept;
            if (!d || d->status == MH_DISC_STATUS_FIXABLE || d->status == MH_DISC_STATUS_ERROR ||
                d->ntracks - 1u > 0x62 || d->total_sectors == 0)
                return fail(c, incomplete);
            if (d->kind == MH_DISC_KIND_V2_DEVICE || d->kind == MH_DISC_KIND_V2_IMAGE) {
                if (d->v2_raw_blocks != (uint64_t)d->total_sectors * 5) return fail(c, incomplete);
            } else if (!(d->flags & MH_DF_STRUCT_OK) || d->nfiles == 0) {
                return fail(c, incomplete);
            }
            if (d->cd_art_path[0]) {
                uint64_t sz;
                if (!mh_file_size64(d->cd_art_path, &sz) || sz != MH_V2_ART_SIZE)
                    return fail(c, "Disc Art invalida.");
            }
            if (d->cheats && d->cheats->count != 0 &&
                mh_cheat_profile_serialized_size(d->cheats) > MH_V2_PKG_CHEAT_BYTES)
                return fail(c, "Cheats excedem a area do disco.");
            if (!resume_point_ok(c, d))
                return fail(c, "O destino nao corresponde ao ponto de retomada. Reconecte o mesmo SD ou restaure a imagem usada nesta sessao.");
            kept = mh_kept_package(d, c->phys, c->path);
            if (kept != 0) {
                uint64_t end = (uint64_t)d->total_sectors * 5 + 0x100 + kept;
                if (end > c->cap / MH_V2_BLOCK || !mh_v2_extent_insert(c->ext, &c->next, kept, end))
                    return fail(c, "Endereco preservado invalido ou sobreposto.");
                if (end > c->max_end1) c->max_end1 = end;
            }
            c->discs++;
            if (c->discs > MH_V2_MAX_DISCS) return fail(c, "A V2 aceita no maximo 256 discos.");
        }
    }
    return true;
}

/* ---- stage 3: the Case and Disc tables and the case art -------------------------------- */

static void copy_str(uint8_t *dst, size_t n, const char *src)
{
    mh_str_copy((char *)dst, n, src);
}

static bool build_tables(Ctx *c)
{
    unsigned ci, j, out_idx = 0;
    c->max_end2 = c->max_end1;
    for (ci = 0; ci < MH_PROJECT_MAX_CASES; ci++) {
        const MhCase *cs = &c->p->cases[ci];
        uint8_t *crec = c->zone + MH_V2_CASE_OFF + (size_t)ci * MH_V2_CASE_SIZE;
        if (!cs->in_use) continue;
        memcpy(crec, MH_V2_CASE_MAGIC, 4);
        crec[4] = 1;
        crec[5] = cs->disc_count;
        crec[6] = (uint8_t)ci;
        crec[7] = cs->pad5;
        copy_str(crec + 8, MH_V2_CASE_NAME_MAX, cs->title);
        copy_str(crec + 0x28, MH_V2_CASE_CODE_MAX, cs->serial);
        mh_v2_wr16(crec + 0x38, cs->art_path[0] ? (uint16_t)ci : 0xffff);
        for (j = 0; j < cs->disc_count; j++) {
            MhDisc *d = cs->discs[j];
            uint8_t *drec = c->zone + MH_V2_DISC_OFF + (size_t)out_idx * MH_V2_DISC_SIZE;
            Item *it = &c->items[out_idx];
            bool in_place, resumed;
            uint64_t lba = mh_kept_package(d, c->phys, c->path);
            mh_v2w_classify(c->phys, c->path, d, &in_place, &resumed);
            if (lba == 0) {
                uint64_t size = (uint64_t)d->total_sectors * 5 + 0x100;
                if (!mh_v2_extent_allocate(c->ext, &c->next, size, c->cap / MH_V2_BLOCK, &lba))
                    return fail(c, "Nao existe espaco alinhado suficiente para um jogo novo.");
                if (lba + size > c->max_end2) c->max_end2 = lba + size;
            }
            memcpy(drec, MH_V2_DISC_MAGIC, 4);
            drec[4] = 1;
            drec[5] = (uint8_t)out_idx;
            drec[6] = (uint8_t)ci;
            drec[7] = (uint8_t)j;
            mh_v2_wr32(drec + 8, ((d->flags >> 6) & 1) | MH_V2_DF_EXT);
            copy_str(drec + 0xc, MH_V2_SERIAL_MAX, d->serial);
            copy_str(drec + 0x1c, MH_V2_TITLE_MAX, d->name);
            mh_v2_wr64(drec + 0x3c, lba);
            mh_v2_wr64(drec + 0x44, lba);
            mh_v2_wr64(drec + 0x4c, lba + 0x100);
            mh_v2_wr64(drec + 0x54, (uint64_t)d->total_sectors * 5);
            mh_v2_wr32(drec + 0x5c, d->total_sectors);
            drec[0x60] = (uint8_t)d->ntracks;
            drec[0x61] = (uint8_t)d->nfiles;
            mh_v2_wr16(drec + 0x62, d->cd_art_path[0] ? (uint16_t)out_idx : 0xffff);
            mh_v2_wr64(drec + 0x64, lba + 5);
            mh_v2_wr64(drec + 0x70, lba + 0x19);
            mh_v2_wr32(drec + 0x78, MH_V2_SLOT);
            if (!in_place) {
                if (!resumed) {
                    c->to_write += (uint64_t)d->total_sectors * MH_V2_SLOT;
                } else {
                    uint8_t old[MH_V2_DISC_SIZE];
                    memcpy(old, d->v2_marker + MH_V2_RSM_REC_OFF, sizeof old);
                    memcpy(drec + 0x7c, old + 0x7c, 4);
                    memcpy(drec + 0x80, old + 0x80, 20);
                }
            } else {
                mh_v2_wr32(drec + 0x7c, d->crc32);
                memcpy(drec + 0x80, d->sha1, 20);
                if (resumed) {
                    uint8_t old[MH_V2_DISC_SIZE];
                    memcpy(old, d->v2_marker + MH_V2_RSM_REC_OFF, sizeof old);
                    memcpy(drec + 0x7c, old + 0x7c, 4);
                    memcpy(drec + 0x80, old + 0x80, 20);
                }
            }
            crec[0x3a + j] = (uint8_t)out_idx;
            it->d = d;
            it->rec = drec;
            it->in_place = in_place;
            it->resumed = resumed;
            out_idx++;
        }
        mh_v2_wr32(crec + 0x4c, mh_crc32(crec, 0x4c));
        if (cs->art_path[0]) {
            uint8_t *art = NULL;
            uint64_t sz = 0;
            if (!mh_read_entire_file(cs->art_path, &art, &sz) || sz != MH_V2_ART_SIZE) {
                mh_free(art);
                return fail(c, "Falha ao ler Case Art.");
            }
            memcpy(c->zone + MH_V2_ART_OFF + (size_t)ci * MH_V2_ART_SIZE, art, MH_V2_ART_SIZE);
            mh_free(art);
        }
    }
    c->nitems = out_idx;
    return true;
}

/* ---- stage 4..: writing --------------------------------------------------------------------- */

static bool progress(Ctx *c, uint64_t done, uint64_t total, const char *text)
{
    return mh_progress_report(c->w->prog, done, total, text);
}

static bool disc_progress(Ctx *c, uint64_t done, const char *text, const MhDisc *d)
{
    return mh_disc_progress(c->w->prog, done, c->to_write, text, d);
}

static uint64_t default_stamp(void *user)
{
    struct timespec ts;
    (void)user;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* Zeroes the gap between the package metadata and the RAW data. Sequential file position I/O
 * like the original (seek64 + write_exact): no I/O record on failure. */
static bool zero_gap(Ctx *c, const uint8_t *rec)
{
    uint64_t first = mh_v2_rd64(rec + 0x3c) + 0x32, last = mh_v2_rd64(rec + 0x4c);
    uint64_t left;
    uint8_t *z;
    if (last == first) return true;
    left = (last - first) * MH_V2_BLOCK;
    if (!mh_seek64(c->io->fd, first * MH_V2_BLOCK)) return false;
    z = mh_calloc(0x100000);
    if (!z) return false;
    while (left) {
        uint64_t n = left > 0x100000 ? 0x100000 : left;
        if (!mh_write_exact(c->io->fd, z, (size_t)n)) {
            mh_free(z);
            return false;
        }
        left -= n;
    }
    mh_free(z);
    return true;
}

/* Reads the disc back from the target, checks the zero padding, the CRC-32 and the SHA-1, then
 * writes the resume marker. false: the disc stays pending. */
static bool confirm_disc(Ctx *c, Item *it, uint64_t *vdone, bool *marker_failed)
{
    uint8_t *buf, digest[20], blk[MH_V2_BLOCK];
    uint32_t n = mh_v2_rd32(it->rec + 0x5c), crc = mh_crc32_begin();
    uint64_t off = mh_v2_rd64(it->rec + 0x4c) << 9;
    MhSha1 sha;
    MhWbuf text;
    MhV2Resume rm;
    MhDisc *d = it->d;
    *marker_failed = false;
    if (!mh_io_flush(c->io, 0)) return false;
    buf = mh_alloc(0x500000);
    if (!buf) return false;
    mh_wbuf_init(&text);
    mh_wbuf_puts(&text, "Confirmando gravacao: ");
    mh_wbuf_putn(&text, (const char *)it->rec + 0x1c, strnlen((const char *)it->rec + 0x1c, 0x20));
    mh_sha1_init(&sha);
    while (n) {
        uint32_t chunk = n > 0x800 ? 0x800 : n, s;
        if (!mh_io_read(c->io, off, buf, (size_t)chunk * MH_V2_SLOT)) goto bad;
        for (s = 0; s < chunk; s++) {
            const uint8_t *slot = buf + (size_t)s * MH_V2_SLOT;
            unsigned i;
            crc = mh_crc32_update(crc, slot, MH_V2_SECTOR);
            mh_sha1_update(&sha, slot, MH_V2_SECTOR);
            for (i = MH_V2_SECTOR; i < MH_V2_SLOT; i++)
                if (slot[i]) goto bad;
        }
        *vdone += (uint64_t)chunk * MH_V2_SLOT;
        n -= chunk;
        off += (uint64_t)chunk * MH_V2_SLOT;
        if (c->w->prog && !progress(c, *vdone, c->to_write, mh_wbuf_cstr(&text))) goto bad;
        mh_progress_yield();
    }
    mh_free(buf);
    mh_wbuf_free(&text);
    crc = mh_crc32_end(crc);
    mh_sha1_final(&sha, digest);
    if (crc != mh_v2_rd32(it->rec + 0x7c) || memcmp(digest, it->rec + 0x80, 20) != 0) return false;

    memset(&rm, 0, sizeof rm);
    rm.stamp = (c->w->stamp ? c->w->stamp : default_stamp)(c->w->stamp_user);
    memcpy(rm.disc_rec, it->rec, MH_V2_DISC_SIZE);
    if (d->verified != 0) {
        rm.has_verify = true;
        rm.verify_stamp = (uint64_t)d->verify_issues | ((uint64_t)d->verify_accepted << 32);
    }
    mh_v2_resume_encode(&rm, blk);
    if (!mh_io_write_block_verified(c->io, (mh_v2_rd64(it->rec + 0x4c) - 1) * MH_V2_BLOCK, blk)) {
        *marker_failed = true;
        return false;
    }
    d->v2_slot = c->phys;
    d->v2_card_blocks = c->cap;
    d->v2_pkg_b = mh_v2_rd64(it->rec + 0x3c);
    mh_str_copy(d->v2_pkg_path, sizeof d->v2_pkg_path, c->path ? c->path : "");
    memcpy(d->v2_marker, blk, sizeof blk);
    d->flags |= MH_DF_KEEP_PACKAGE;
    return true;
bad:
    mh_free(buf);
    mh_wbuf_free(&text);
    return false;
}

static bool write_discs(Ctx *c)
{
    uint64_t done = 0, vdone = 0;
    unsigned i;
    for (i = 0; i < c->nitems; i++) {
        Item *it = &c->items[i];
        MhDisc *d = it->d;
        uint8_t *rec = it->rec;
        uint64_t lba = mh_v2_rd64(rec + 0x3c), lba_out = 0;
        bool written;
        c->cur_no = (int)i + 1;
        mh_str_copy(c->cur_name, sizeof c->cur_name, (const char *)rec + 0x1c);
        if (!it->in_place && !it->resumed) {
            if (c->w->prog && !disc_progress(c, done, "Gravando: ", d))
                return fail(c, "Gravacao cancelada. Mantenha o Manager aberto para retomar.");
            d->flags &= ~(uint32_t)MH_DF_KEEP_PACKAGE;
            if (!zero_gap(c, rec)) return fail(c, "Falha ao alinhar um disco.");
            if (!mh_v2_stream_disc(c->w, c->io, d, rec, &done, c->to_write, MH_V2_STREAM_WRITE))
                return fail_io(c, "Gravacao interrompida. O disco atual ficou pendente; os concluidos podem ser retomados.");
            written = true;
        } else {
            if (c->w->prog && !disc_progress(c, done, "Ja gravado: ", d))
                return fail(c, "Gravacao cancelada. Mantenha o Manager aberto para retomar.");
            written = false;
        }
        char berr[0x100];
        if (!mh_v2_build_package_metadata(d, rec, c->tbl, berr, sizeof berr) ||
            !mh_io_sync_blocks_verified(c->io, lba << 9, c->tbl, MH_V2_PKG_META_BYTES, NULL, "", &lba_out))
            return fail_io(c, "Falha ao sincronizar os metadados do disco. Mantenha o Manager aberto para retomar.");
        mh_v2_wr32(rec + 0xfc, mh_crc_record(rec, MH_V2_DISC_SIZE));
        if (written) {
            bool marker_failed;
            if (!confirm_disc(c, it, &vdone, &marker_failed)) {
                if (marker_failed)
                    return fail_io(c, "Falha ao registrar a conclusao do disco. Ele permanece pendente.");
                return fail_io(c, "Nao foi possivel confirmar o disco gravado. Ele permanece pendente para a proxima tentativa.");
            }
        }
        if (c->w->prog && !disc_progress(c, done, "Concluido: ", d))
            return fail(c, "Gravacao cancelada. Discos concluidos serao aproveitados ao retomar nesta sessao.");
    }
    return true;
}

/* ---- the commit ----------------------------------------------------------------------------- */

static uint32_t generation_of(const MhProject *p)
{
    if (p->card_loaded == 0 && p->image_loaded == 0) return p->reserved_c == 0 ? 1 : p->reserved_c;
    return p->reserved_c == 0xffffffffu ? 1 : p->reserved_c + 1;
}

static bool commit(Ctx *c, uint64_t need)
{
    uint32_t mcrc = mh_crc32_begin(), i;
    MhSha1 msha;
    MhV2Header h;
    uint8_t cfg[MH_V2_SLOT], hdr[MH_V2_HDR_SLOT];
    uint64_t lba_out = 0, music = c->w->music_size;

    if (!mh_io_flush(c->io, 0)) return fail(c, "Falha ao confirmar os metadados do SD.");
    mh_sha1_init(&msha);
    for (i = 0; i < c->menu_sectors; i++) {
        const uint8_t *sec = c->menu + (size_t)i * MH_V2_SECTOR;
        memcpy(c->zone + MH_V2_MENU_OFF + (size_t)i * MH_V2_SLOT, sec, MH_V2_SECTOR);
        mcrc = mh_crc32_update(mcrc, sec, MH_V2_SECTOR);
        mh_sha1_update(&msha, sec, MH_V2_SECTOR);
    }
    if (!c->w->music || music < 1 || music > MH_V2_MUSIC_MAX)
        return fail(c, "Musica Umbrella incorporada invalida.");
    mh_v2_cfg_encode((uint32_t)music, mh_crc32(c->w->music, (size_t)music), cfg);
    memcpy(c->zone + MH_V2_CFG_OFF, cfg, MH_V2_CFG_SIZE);
    memcpy(c->zone + MH_V2_MUSIC_OFF, c->w->music, (size_t)music);

    memset(&h, 0, sizeof h);
    h.version = MH_V2_VERSION;
    h.flags = MH_V2_HF_BASE | MH_V2_HF_MBR;
    h.case_count = (uint16_t)c->cases;
    h.disc_count = (uint16_t)c->discs;
    h.generation = generation_of(c->p);
    h.menu_sectors = c->menu_sectors;
    h.crc_cases = mh_crc32(c->zone + MH_V2_CASE_OFF, MH_V2_CASE_TABLE);
    h.crc_discs = mh_crc32(c->zone + MH_V2_DISC_OFF, MH_V2_DISC_TABLE);
    h.crc_art = mh_crc32(c->zone + MH_V2_ART_OFF, MH_V2_ART_REGION);
    h.crc_menu = mh_crc32_end(mcrc);
    mh_sha1_final(&msha, h.menu_sha1);
    h.mbr_sectors = mh_v2_mbr_sectors(c->cap / MH_V2_BLOCK);
    mh_v2_header_encode(&h, hdr);
    memcpy(c->zone + MH_V2_HDR_B_OFF, hdr, MH_V2_HDR_SLOT);
    memcpy(c->zone + MH_V2_HDR_A_OFF, hdr, MH_V2_HDR_SLOT);

    if (c->w->prog && !progress(c, 0, MH_V2_FIXED_ZONE, "Atualizando tabelas e artes do V2"))
        return fail(c, NULL);
    if (!mh_io_sync_blocks_verified(c->io, MH_V2_BODY_OFF, c->zone + MH_V2_BODY_OFF, MH_V2_BODY_SIZE,
                                    c->w->prog, "Sincronizando tabelas, menu e artes", &lba_out)) {
        MhWbuf t;
        mh_wbuf_init(&t);
        mh_wbuf_puts(&t, "Falha persistente ao sincronizar a zona fixa V2 no bloco SD LBA ");
        mh_wbuf_u64(&t, lba_out);
        mh_wbuf_puts(&t, ". Nenhuma escrita de controle excedeu 512 bytes; troque o leitor USB se a falha se repetir.");
        fail(c, mh_wbuf_cstr(&t));
        mh_wbuf_free(&t);
        return false;
    }
    if (!mh_io_compare_bytes(c->io, MH_V2_BODY_OFF, c->zone + MH_V2_BODY_OFF, MH_V2_BODY_SIZE, c->w->prog,
                             "Verificando tabelas e artes"))
        return fail(c, "A verificacao da zona fixa V2 divergiu.");
    if (c->w->prog && !progress(c, 0, 1, "Finalizando biblioteca do SD"))
        return fail(c, "Finalizacao cancelada. Reaplique nesta sessao para concluir.");
    if (!mh_v2_publish_header_slot(c->io, MH_V2_HDR_B_OFF, c->zone + MH_V2_HDR_B_OFF))
        return fail(c, "O corpo V2 foi relido, mas o leitor USB nao confirmou a publicacao setorial do header B. O cartao permanece sem commit para impedir boot inconsistente.");
    if (!mh_v2_publish_header_slot(c->io, MH_V2_HDR_A_OFF, c->zone))
        return fail(c, "O header B foi publicado e relido, mas o header A falhou. O cartao permanece recuperavel pelo backup B; troque o leitor USB e reaplique.");
    if (!mh_io_compare_bytes(c->io, 0, c->zone, MH_V2_BODY_OFF, NULL, ""))
        return fail(c, "Readback dos headers V2 divergiu.");
    if (path_nonempty(c->path)) {
        if (ftruncate(c->io->fd, (off_t)need) != 0 || !mh_io_flush(c->io, 0))
            return fail(c, "Falha ao finalizar o tamanho da imagem V2.");
    }
    if (c->res) {
        c->res->ok = 1;
        c->res->code = 0;
        c->res->cases = (uint16_t)c->cases;
        c->res->discs = (uint16_t)c->discs;
        c->res->size = need;
        mh_str_copy(c->res->msg, MH_V2W_MSG_MAX,
                    "PSXRAW V2 concluido. Novos discos gravados e confirmados; discos existentes preservados.");
    }
    return true;
}

/* orig: v2_write_handle_direct @0x14003a850 */
bool mh_v2_write_handle_direct(const MhV2Writer *w, MhProject *p, MhIo *io, int32_t phys_id,
                               const char *resume_path, uint64_t capacity, MhV2Result *res)
{
    Ctx *c;
    bool ok = false;
    uint64_t need;
    uint8_t backup[MH_V2_HDR_SLOT], buf[MH_V2_HDR_SLOT], wip[MH_V2_BLOCK];
    bool have_backup = false;
    uint64_t cursize;

    if (res) memset(res, 0, sizeof *res);
    if (!io || io->fd < 0 || !p || !w) {
        if (res) mh_str_copy(res->msg, MH_V2W_MSG_MAX, "Destino direto V2 invalido.");
        return false;
    }
    mh_ioerr_clear(io->err);
    if (!mh_v2_preflight_serials(p, res)) return false;
    c = mh_calloc(sizeof *c);
    if (!c) {
        if (res) mh_str_copy(res->msg, MH_V2W_MSG_MAX, "Memoria insuficiente para preparar a gravacao V2.");
        return false;
    }
    c->w = w;
    c->p = p;
    c->io = io;
    c->phys = phys_id;
    c->path = resume_path;
    c->cap = capacity;
    c->res = res;
    c->zone = mh_calloc(MH_V2_FIXED_ZONE);
    c->tbl = mh_alloc(MH_V2_PKG_META_BYTES);
    if (!c->zone || !c->tbl) {
        if (res) mh_str_copy(res->msg, MH_V2W_MSG_MAX, "Memoria insuficiente para preparar a gravacao V2.");
        goto out;
    }
    if (!prepare_menu(c)) {
        fail(c, MENU_BAD);
        goto out;
    }
    if (!validate_project(c)) goto out;

    /* Package metadata of every disc must be buildable (also fills the cheat sizes). */
    if (!build_tables(c)) goto out;
    {
        unsigned i;
        char err[0x100];
        for (i = 0; i < c->nitems; i++)
            if (!mh_v2_build_package_metadata(c->items[i].d, c->items[i].rec, c->tbl, err, sizeof err)) {
                fail(c, err);
                goto out;
            }
    }
    if (w->prog && !progress(c, 0, c->to_write, "Iniciando gravacao dos discos pendentes")) {
        fail(c, "Operacao cancelada antes de gravar.");
        goto out;
    }
    need = (c->max_end1 > c->max_end2 ? c->max_end1 : c->max_end2) << 9;
    if (need < MH_V2_FIXED_ZONE) need = MH_V2_FIXED_ZONE;
    if (capacity < need) {
        fail(c, "Os jogos nao cabem no destino selecionado.");
        goto out;
    }

    /* Keep the previous header (if any) so a failed invalidation can restore it. */
    if (mh_io_size(io, &cursize) && cursize >= 0x1400) {
        if ((mh_io_read(io, 0, buf, MH_V2_HDR_SLOT) && mh_v2_header_raw_valid(buf)) ||
            (mh_io_read(io, MH_V2_HDR_B_OFF, buf, MH_V2_HDR_SLOT) && mh_v2_header_raw_valid(buf))) {
            memcpy(backup, buf, sizeof backup);
            have_backup = true;
        }
    }
    if (!mh_v2_invalidate_header_slot(io, MH_V2_HDR_A_OFF)) {
        fail(c, "O leitor USB nao confirmou nem releu a invalidacao de 512 bytes do header A. Nenhum outro metadado foi alterado; troque o leitor USB antes de tentar novamente.");
        goto out;
    }
    if (!mh_v2_invalidate_header_slot(io, MH_V2_HDR_B_OFF)) {
        const char *m = "O leitor USB falhou ao invalidar o header B. O destino ja nao possuia header recuperavel; troque o leitor antes de nova tentativa.";
        if (have_backup) {
            mh_v2_publish_header_slot(io, MH_V2_HDR_A_OFF, backup);
            m = "O leitor USB falhou ao invalidar o header B; o header A anterior foi restaurado e relido. Troque o leitor USB antes de tentar novamente.";
        }
        fail(c, m);
        goto out;
    }
    memset(wip, 0, sizeof wip);
    memcpy(wip, MH_V2_WIP_MAGIC, 7);
    if (!mh_io_write_block_verified(io, 0, wip)) {
        fail(c, "Falha ao iniciar a transacao no SD.");
        goto out;
    }
    if (!write_discs(c)) goto out;
    if (!commit(c, need)) goto out;
    ok = true;
out:
    mh_free(c->menu);
    mh_free(c->tbl);
    mh_free(c->zone);
    mh_free(c);
    return ok;
}
