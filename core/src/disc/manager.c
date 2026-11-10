/* Parsing a CUE together with the database, and adding it to the project (the non-UI part of
 * add_cue_path). */
#include "mh/disc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* orig: manager_parse @0x140022a60 */
MhDisc *mh_manager_parse(const MhDb *db, const char *cue_path, const MhCueHooks *hooks)
{
    MhDisc *d = mh_cue_parse(cue_path);
    if (!d) return NULL;
    d->card_index = 0xffff;

    if (d->flags & MH_DF_MISSING_BIN) {
        char (*store)[MH_PATH_MAX] = mh_alloc(sizeof(char[MH_PATH_MAX]) * MH_DISC_MAX_FILES);
        const char *paths[MH_DISC_MAX_FILES];
        bool abort_fix = (store == NULL);
        for (unsigned k = 0; k < d->nfiles && !abort_fix; k++) {
            const char *src = d->files[k].path;
            if (!mh_file_exists(src)) {
                MhWbuf w;
                bool yes = false;
                mh_wbuf_init(&w);
                mh_wbuf_puts(&w, "The CUE references a BIN that was not found:\r\n\r\n");
                mh_wbuf_puts(&w, d->files[k].name);
                mh_wbuf_puts(&w, "\r\n\r\nDo you want to locate that BIN now?");
                if (hooks && hooks->ask && hooks->ask(hooks->ctx, mh_wbuf_cstr(&w)) && hooks->locate &&
                    hooks->locate(hooks->ctx, store[k]))
                    yes = true;
                mh_wbuf_free(&w);
                if (!yes) { abort_fix = true; break; }
            } else {
                mh_str_copy(store[k], MH_PATH_MAX, src);
            }
            paths[k] = store[k];
        }
        if (!abort_fix) {
            char tmpdir[MH_PATH_MAX], tmpl[MH_PATH_MAX];
            const char *td = getenv("TMPDIR");
            int fd;
            mh_str_copy(tmpdir, sizeof tmpdir, td && td[0] ? td : "/tmp");
            mh_path_join(tmpdir, "PSTXXXXXX", tmpl, sizeof tmpl);
            fd = mkstemp(tmpl);
            if (fd >= 0) {
                close(fd);
                if (mh_cue_rewrite_files(cue_path, tmpl, paths, d->nfiles)) {
                    MhDisc *nd = mh_cue_parse(tmpl);
                    remove(tmpl);
                    if (nd) {
                        mh_str_copy(nd->cue_path, sizeof nd->cue_path, cue_path);
                        nd->card_index = 0xffff;
                        mh_disc_note(nd, "BIN names and paths were fixed for this write only; original files preserved.");
                        mh_disc_free(d);
                        d = nd;
                    }
                } else {
                    remove(tmpl);
                }
            }
        }
        mh_free(store);
    }
    if ((d->flags & MH_DF_STRUCT_OK) == 0) return d;

    if (db) {
        mh_db_apply_disc(db, d);
        if (d->flags & MH_DF_TRACKS_SUSPECT) {
            if (mh_db_recover_track_map(db, d)) mh_db_apply_disc(db, d);
        }
    }
    {
        bool have_db_title = false;
        char w[0x100], stem[MH_PATH_MAX];
        if (d->db_index != 0xffffffffu && db) {
            const char *t = mh_db_record_title(db, d->db_index);
            if (t[0]) {
                mh_utf8_from_latin1(t, w, sizeof w);
                mh_normalize_title(w, d->title, sizeof d->title);
                mh_short_disc_name(d->title, d->name, sizeof d->name);
            }
            have_db_title = true;
        }
        if (d->title[0] == 0) {
            mh_path_stem(cue_path, stem, sizeof stem);
            mh_normalize_title(stem, d->title, sizeof d->title);
        }
        if (have_db_title) {
            const char *t = mh_db_record_title(db, d->db_index);
            if (t[0]) mh_db_find_sbi(db, d);
        }
    }
    if (d->title[0] == 0) return d;
    if ((d->flags & MH_DF_USER_NAMED) == 0) mh_short_disc_name(d->title, d->name, sizeof d->name);
    return d;
}

/* orig: add_cue_path.constprop.0 @0x1400012d0, without the window code */
MhAddStatus mh_project_add_cue(MhProject *p, const MhDb *db, const char *cue_path, unsigned target_slot,
                               const MhCueHooks *hooks, MhAddResult *res, char *err, size_t errn)
{
    MhDisc *d;
    MhCase *c = NULL;
    bool grouped;
    char tmp[MH_CASE_TITLE_MAX];
    if (err && errn) err[0] = 0;
    if (!mh_str_ends_i(cue_path, ".cue")) return MH_ADD_NOT_CUE;
    if (mh_project_contains_cue(p, cue_path)) return MH_ADD_DUPLICATE;
    d = mh_manager_parse(db, cue_path, hooks);
    if (!d) {
        mh_err_set(err, errn, "Could not parse a CUE.");
        return MH_ADD_PARSE_FAILED;
    }
    if (hooks && hooks->admit && !hooks->admit(hooks->ctx, d)) {
        mh_disc_free(d);
        return MH_ADD_REJECTED;
    }
    if (db) {
        mh_db_find_art(db, d, true);
        mh_db_find_art(db, d, false);
    }
    if (hooks && hooks->attach_cheats) hooks->attach_cheats(hooks->ctx, d);
    grouped = mh_case_title_for_disc(d, tmp, sizeof tmp);
    mh_project_recount(p);
    if (p->disc_count > 0xff) {
        mh_err_set(err, errn, "V2 accepts at most 256 discs in total.");
        mh_disc_free(d);
        return MH_ADD_LIMIT;
    }
    if (target_slot < 0x100 && p->cases[target_slot].in_use) {
        c = &p->cases[target_slot];
        if (c->disc_count > 4) {
            mh_err_set(err, errn, "The selected Case already has 5 discs.");
            mh_disc_free(d);
            return MH_ADD_LIMIT;
        }
    } else {
        if (grouped) c = mh_project_group_case(p, d);
        if (!c) {
            c = mh_project_add_case(p, 0);
            if (!c) {
                mh_disc_free(d);
                return MH_ADD_NO_ROOM;
            }
        }
    }
    if (c->disc_count > 4) {
        mh_err_set(err, errn, "The game already has five discs; the extra disc was not added.");
        mh_disc_free(d);
        return MH_ADD_LIMIT;
    }
    if (!mh_case_add_disc(p, c, d)) {
        if (c->disc_count == 0) mh_project_remove_case(p, c->slot);
        mh_disc_free(d);
        return MH_ADD_NO_ROOM;
    }
    if (c->serial[0] == 0 && d->serial[0]) mh_str_copy(c->serial, sizeof c->serial, d->serial);
    if (mh_str_icmp(c->title, MH_NEW_CASE_TITLE) == 0 || c->title[0] == 0) {
        mh_case_title_for_disc(d, tmp, sizeof tmp);
        mh_str_copy(c->title, sizeof c->title, tmp[0] ? tmp : d->name);
    }
    if (c->art_path[0] == 0 && d->case_art_path[0]) mh_str_copy(c->art_path, sizeof c->art_path, d->case_art_path);
    if (res) {
        res->cas = c;
        res->disc_index = c->disc_count - 1;
    }
    return MH_ADD_OK;
}
