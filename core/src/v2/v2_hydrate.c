/* v2_hydrate.c: orig hydrate_loaded_project @0x14001c640. */
#include <stdlib.h>

#include "v2_internal.h"

#define NO_DB_MSG "Banco incorporado indisponivel; o catalogo RAW existente foi preservado sem alteracoes."

static void note_db_unavailable(MhDisc *d)
{
    if (strstr(d->notes, NO_DB_MSG)) return;
    mh_disc_note(d, NO_DB_MSG);
}

void mh_v2_hydrate_project(MhProject *p, const MhV2Env *env)
{
    const MhDb *db = env ? env->db : NULL;
    const MhCheatDb *cdb = env ? env->cheats : NULL;
    bool cdb_ok = cdb && cdb->loaded;
    bool dirty = false;
    unsigned ci, k;

    for (ci = 0; ci < MH_PROJECT_MAX_CASES; ci++) {
        MhCase *c = &p->cases[ci];
        if (!c->in_use || c->disc_count == 0) continue;
        for (k = 0; k < c->disc_count; k++) {
            MhDisc *d = c->discs[k];
            MhCheatProfile *saved, *fresh = NULL;
            bool had_case, had_cd;
            char msg[256];
            if (!d || d->serial[0] == 0) continue;
            d->db_index = 0xffffffffu;
            saved = d->cheats;
            had_case = d->case_art_path[0] != 0;
            had_cd = d->cd_art_path[0] != 0;
            if (db && !mh_db_apply_disc(db, d)) mh_db_apply_saved_single_track(db, d);
            msg[0] = 0;

            if (d->cheats != NULL) {
                if (d->cheats->state == MH_CHEAT_STATE_REFS_ONLY && cdb_ok) {
                    d->cheats->state = MH_CHEAT_STATE_NONE;
                    mh_str_copy(d->cheat_note, sizeof d->cheat_note,
                                "Catalogo preparado para firmware compacto. Aplique as alteracoes; escolhas preservadas.");
                    dirty = true;
                }
                if (d->cheats->state == MH_CHEAT_STATE_FULL_SNAPSHOT && cdb_ok) {
                    if (mh_cheat_profile_build_exact_catalog(cdb, d->cheats->title, d->serial, &fresh, msg, sizeof msg) &&
                        mh_cheat_profile_restore_user_state(fresh, d->cheats, msg, sizeof msg)) {
                        dirty = true;
                        mh_cheat_profile_free(d->cheats);
                        d->cheats = fresh;
                        fresh = NULL;
                        mh_str_copy(d->cheat_note, sizeof d->cheat_note,
                                    "Catalogo original completo restaurado. Escolhas anteriores preservadas; novos grupos "
                                    "desativados. Aplique as alteracoes.");
                    }
                    mh_cheat_profile_free(fresh);
                    fresh = NULL;
                }
                if (d->cheats != NULL) {
                    if (!cdb_ok) note_db_unavailable(d);
                    goto art;
                }
            }
            /* no profile (or the snapshot could not be rebuilt): bind the catalog of the game */
            msg[0] = 0;
            if (!cdb_ok) {
                mh_str_copy(msg, sizeof msg, "O banco de cheats incorporado nao passou na verificacao.");
            } else {
                uint32_t title;
                if (!mh_cheat_db_find_title_for_disc(cdb, d->serial, &title)) {
                    mh_str_copy(msg, sizeof msg,
                                "Este serial nao possui vinculo automatico no GameShark V5. Abra Cheats e escolha o jogo "
                                "manualmente.");
                } else if (!mh_cheat_profile_build_exact_catalog(cdb, 0xffffffffu, d->serial, &fresh, msg, sizeof msg)) {
                    mh_cheat_profile_free(fresh);
                } else {
                    d->cheats = fresh;
                    if (d->cheat_regenerated == 0) {
                        mh_str_copy(d->cheat_note, sizeof d->cheat_note,
                                    "Catalogo de cheats associado pelo serial. Cheats inicialmente desativados; aplique as "
                                    "alteracoes para disponibilizar a lista no menu.");
                    } else {
                        d->cheat_regenerated = 0;
                        mh_str_copy(d->cheat_note, sizeof d->cheat_note,
                                    "Catalogo validado regenerado pelo serial; pronto para aplicar. Escolhas anteriores "
                                    "desativadas. Confira a lista de cheats.");
                    }
                    dirty = true;
                    msg[0] = 0;
                    goto art;
                }
            }
            if (d->cheats == NULL && msg[0]) mh_str_copy(d->cheat_note, sizeof d->cheat_note, msg);
        art:
            if (db) {
                if (d->case_art_path[0] == 0) mh_db_find_art(db, d, true);
                if (d->cd_art_path[0] == 0) mh_db_find_art(db, d, false);
            }
            if (saved == NULL && d->cheats != NULL) dirty = true;
            if (!had_case && d->case_art_path[0]) dirty = true;
            if (!had_cd && d->cd_art_path[0]) dirty = true;
            if (c->art_path[0] == 0 && d->case_art_path[0]) {
                dirty = true;
                mh_str_copy(c->art_path, sizeof c->art_path, d->case_art_path);
            }
        }
    }
    p->cache_tag = 0;
    if (dirty) p->dirty = 1;
}
