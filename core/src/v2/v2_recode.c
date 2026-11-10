/* v2_recode.c: orig manager_recode_copy @0x140022e80 (EDC/ECC recode of a CUE/BIN copy). */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "v2_internal.h"

#define CHUNK_SECTORS 0x80u

static void cleanup(const char *dst_cue, const char *dir, char (*bins)[MH_PATH_MAX], unsigned created)
{
    unsigned i;
    for (i = 0; i < created; i++) unlink(bins[i]);
    rmdir(dir);
    unlink(dst_cue);
}

bool mh_manager_recode_copy(const MhDisc *d, const char *dst_cue, MhProgress *prog, char *err, size_t errn)
{
    char full[MH_PATH_MAX], dir[MH_PATH_MAX], stem[MH_PATH_MAX], folder[MH_PATH_MAX];
    char (*bins)[MH_PATH_MAX] = NULL;
    const char **paths = NULL;
    uint8_t *buf = NULL;
    unsigned created = 0, changed = 0, nfiles, i, track = 0;
    uint64_t done = 0;
    MhSha1 sha;
    uint8_t digest[20];
    int fd;
    bool ok = false;
    const char *msg;

    if (err && errn) err[0] = 0;
    if (!d || d->kind != 1 || !d->verified || (d->verify_issues & 0xc0) == 0 || (d->verify_issues & 8) != 0 ||
        d->nfiles == 0 || d->ntracks == 0) {
        mh_str_copy(err, errn,
                    "Forneca um CUE/BIN verificado com estrutura valida. Recalculo nao recupera dados ausentes ou "
                    "corrompidos.");
        return false;
    }
    nfiles = d->nfiles;
    mh_sha1_init(&sha);
    if (!mh_path_full(dst_cue, full, sizeof full) || mh_path_equal_full(full, d->cue_path)) {
        mh_str_copy(err, errn, "Escolha outro nome de CUE; a origem deve ser preservada.");
        return false;
    }
    fd = open(full, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) {
        mh_str_copy(err, errn, "O destino ja existe ou nao pode ser criado. Escolha um novo nome.");
        return false;
    }
    close(fd);
    mh_path_dirname(full, dir, sizeof dir);
    mh_path_stem(full, stem, sizeof stem);
    mh_str_cat(stem, sizeof stem, "_BIN_EDC_ECC");
    mh_path_join(dir, stem, folder, sizeof folder);
    if (mkdir(folder, 0777) != 0) {
        mh_str_copy(err, errn, "A pasta da copia ja existe ou nao pode ser criada. Escolha um novo nome.");
        unlink(full);
        return false;
    }
    bins = calloc(nfiles, MH_PATH_MAX);
    paths = calloc(nfiles, sizeof *paths);
    buf = malloc((size_t)CHUNK_SECTORS * MH_V2_SECTOR);
    if (!bins || !paths || !buf) goto fail;

    for (i = 0; i < nfiles; i++) {
        const MhDiscFile *f = &d->files[i];
        uint64_t fsize;
        MhIo src, dst;
        char name[32];
        uint32_t remaining = f->sectors, off = 0;
        if (!mh_file_size64(f->path, &fsize) || (uint64_t)f->sectors * MH_V2_SECTOR != fsize) goto fail;
        snprintf(name, sizeof name, "BIN_%u.bin", i + 1);
        mh_path_join(folder, name, bins[i], MH_PATH_MAX);
        paths[i] = bins[i];
        {
            int sfd = open(f->path, O_RDONLY);
            int dfd = open(bins[i], O_WRONLY | O_CREAT | O_EXCL, 0666);
            if (dfd >= 0) created = i + 1;
            if (sfd < 0 || dfd < 0) {
                if (sfd >= 0) close(sfd);
                if (dfd >= 0) close(dfd);
                goto fail;
            }
            mh_io_attach(&src, sfd, false, NULL);
            mh_io_attach(&dst, dfd, true, NULL);
        }
        while (remaining != 0) {
            uint32_t n = remaining > CHUNK_SECTORS ? CHUNK_SECTORS : remaining, s;
            size_t bytes = (size_t)n * MH_V2_SECTOR;
            if (!mh_read_exact(src.fd, buf, bytes)) goto io_fail;
            mh_sha1_update(&sha, buf, bytes);
            for (s = 0; s < n; s++) {
                uint32_t abs = f->start + off + s;
                uint8_t *sec = buf + (size_t)s * MH_V2_SECTOR;
                while (track + 1 < d->ntracks && (uint32_t)(d->tracks[track + 1].start - d->tracks[track + 1].pregap) <= abs)
                    track++;
                if (d->tracks[track].kind == 2 && d->tracks[track].start <= abs) {
                    int r = mh_cd_sector_recode(sec);
                    if ((r & 1) || sec[0xf] != d->tracks[track].mode) goto io_fail;
                    if (r != 0) changed++;
                }
            }
            if (!mh_write_exact(dst.fd, buf, bytes)) goto io_fail;
            remaining -= n;
            off += n;
            done += bytes;
            if (!mh_progress_report(prog, done, d->total_bytes, "Criando copia EDC/ECC; origem preservada")) {
                mh_str_copy(err, errn, "Copia cancelada; arquivos parciais removidos e origem preservada.");
                mh_io_close(&src);
                mh_io_close(&dst);
                goto fail;
            }
            mh_progress_yield();
        }
        if (fsync(dst.fd) != 0) goto io_fail;
        mh_io_close(&src);
        mh_io_close(&dst);
        continue;
    io_fail:
        mh_io_close(&src);
        mh_io_close(&dst);
        goto fail;
    }
    mh_sha1_final(&sha, digest);
    if (memcmp(digest, d->verify_sha1, 20) != 0) {
        mh_str_copy(err, errn,
                    "A origem mudou desde a verificacao. Verifique o disco novamente antes de criar a copia.");
        goto fail;
    }
    if (changed == 0) {
        mh_str_copy(err, errn, "Nenhum codigo EDC/ECC precisou ser alterado.");
        goto fail;
    }
    if (!mh_cue_rewrite_files(d->cue_path, full, paths, nfiles)) goto fail;
    ok = true;
    goto out;
fail:
    cleanup(full, folder, bins ? bins : (char(*)[MH_PATH_MAX])NULL, bins ? created : 0);
    msg = "Nao foi possivel concluir a copia. Origem preservada; arquivos parciais removidos.";
    if (err && errn && err[0] == 0) mh_str_copy(err, errn, msg);
out:
    free(bins);
    free((void *)paths);
    free(buf);
    return ok;
}
