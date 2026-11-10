/* Writing a V2 library into a regular file (orig: v2_write_image_direct, v2_build_image). */
#include "write_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

static bool fail(MhV2Result *res, const char *msg)
{
    mh_v2w_msg(res, msg);
    return false;
}

/* The output file must not be one of the files the project reads from. */
static const char *source_clash(const MhProject *p, const char *path)
{
    unsigned ci, j, k;
    for (ci = 0; ci < MH_PROJECT_MAX_CASES; ci++) {
        const MhCase *c = &p->cases[ci];
        if (!c->in_use) continue;
        if (c->art_path[0] && mh_path_equal_full(path, c->art_path))
            return "A imagem substituiria uma arte de origem.";
        for (j = 0; j < c->disc_count; j++) {
            const MhDisc *d = c->discs[j];
            if (!d) continue;
            if (mh_path_equal_full(path, d->cue_path) || mh_path_equal_full(path, d->sbi_path) ||
                mh_path_equal_full(path, d->cd_art_path))
                return "A imagem substituiria um arquivo de origem.";
            for (k = 0; k < d->nfiles; k++)
                if (d->files[k].path[0] && mh_path_equal_full(path, d->files[k].path))
                    return "A imagem substituiria um BIN de origem.";
        }
    }
    return NULL;
}

/* orig: v2_write_image_direct @0x14003d0a0 */
bool mh_v2_write_image_direct(const MhV2Writer *w, MhProject *p, const char *path, MhV2Result *res)
{
    const char *clash;
    bool existing;
    unsigned ci, j;
    MhIo io;
    uint64_t cur = 0, est, target;
    bool have_size;
    bool ok;

    if (!p || !path || !path[0]) return fail(res, "Caminho da imagem V2 invalido.");
    if ((clash = source_clash(p, path)) != NULL) return fail(res, clash);
    if (!mh_v2_preflight_serials(p, res)) return false;

    /* An image that this project was loaded from, or that holds packages of the project,
     * is opened as it is (OPEN_EXISTING); any other path is created and truncated. */
    existing = p->image_loaded && p->image_path[0] && mh_path_equal_full(p->image_path, path);
    for (ci = 0; ci < MH_PROJECT_MAX_CASES; ci++) {
        const MhCase *c = &p->cases[ci];
        if (!c->in_use) continue;
        for (j = 0; j < c->disc_count; j++) {
            const MhDisc *d = c->discs[j];
            if (d && (d->flags & MH_DF_KEEP_PACKAGE) && d->v2_pkg_path[0] &&
                mh_path_equal_full(d->v2_pkg_path, path))
                existing = true;
            /* Not in the original: mh_v2w_classify treats such a disc as "in place" (its RAW slots
             * are expected in the file), so the file must not be truncated by CREATE_ALWAYS. */
            if (d && d->kind == MH_DISC_KIND_V2_IMAGE && mh_path_equal_full(d->source_path, path))
                existing = true;
        }
    }
    if (!mh_io_open(&io, path, existing ? MH_IO_RDWR : MH_IO_CREATE, true, NULL))
        return fail(res, "Nao foi possivel abrir/criar a imagem V2.");

    have_size = mh_io_size(&io, &cur);
    est = mh_v2_estimate_project_bytes_for_target(p, -1, path);
    target = MH_V2_FIXED_ZONE;
    if (!have_size) {
        if (est >= MH_V2_FIXED_ZONE) target = est;
    } else {
        if (cur >= MH_V2_FIXED_ZONE) target = cur;
        if (target < est) target = est;
    }
    if (!have_size || target > cur) {
        if (ftruncate(io.fd, (off_t)target) != 0) {
            mh_io_close(&io);
            return fail(res, "Nao foi possivel reservar o tamanho da imagem V2.");
        }
    }
    ok = mh_v2_write_handle_direct(w, p, &io, -1, path, target, res);
    mh_io_close(&io);
    return ok;
}

/* orig: v2_build_image @0x140035260 */
bool mh_v2_build_image(const MhV2Writer *w, MhProject *p, const char *path, MhV2Result *res)
{
    return mh_v2_write_image_direct(w, p, path, res);
}
