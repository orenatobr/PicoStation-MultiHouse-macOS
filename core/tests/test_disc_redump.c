/* Slow disc tests on the real CUE/BIN library (skipped when absent): Redump verification of
 * full discs and track map recovery through the trackset SHA-1 table. */
#include "disc_util.inc"

static void load_db(MhDb *db, bool *ok)
{
    *ok = access(EMBEDDED "/disc_index.bin", R_OK) == 0 && mh_db_load_dir(db, EMBEDDED, "/nonexistent-dir");
}

static void test_redump(const MhDb *db)
{
    static const char *const cues[] = {"Alundra (USA) (Rev 1).cue", "Breath of Fire III (USA).cue"};
    for (size_t i = 0; i < sizeof cues / sizeof cues[0]; i++) {
        char p[MH_PATH_MAX];
        MhDisc *d;
        MhProgress pr;
        snprintf(p, sizeof p, "%s/%s", FIXTURES, cues[i]);
        if (access(p, R_OK) != 0) { fprintf(stderr, "skip %s\n", cues[i]); continue; }
        d = mh_manager_parse(db, p, NULL);
        mh_progress_init(&pr, NULL, NULL);
        CHECK(d->crc32 == 0 && (d->flags & MH_DF_TRACKSET_OK) == 0);
        CHECK(mh_db_verify_redump(db, d, &pr));
        CHECK((d->flags & MH_DF_TRACKSET_OK) && !(d->flags & MH_DF_INTEGRITY_BAD) && (d->flags & MH_DF_TRACKSET_CHECKED));
        CHECK(d->crc32 != 0);
        CHECK(note_has_end(d, "\r\nIntegridade confirmada pelo DAT Redump."));
        CHECK(d->status == MH_DISC_STATUS_OK);
        mh_disc_free(d);
    }
}

static void test_redump_negative(const MhDb *db)
{
    char p[MH_PATH_MAX];
    MhDisc *d;
    /* a BIN that is not in Redump */
    put_zero_bin("z.bin", 2352 * 100);
    put_text("z.cue", "FILE \"z.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n");
    tmp_path("z.cue", p);
    d = mh_cue_parse(p);
    CHECK(!mh_db_verify_redump(db, d, NULL));
    CHECK((d->flags & MH_DF_INTEGRITY_BAD) && !(d->flags & MH_DF_TRACKSET_OK));
    CHECK(note_has_end(d, "\r\nO hash nao foi localizado no DAT Redump ou algum BIN difere."));
    CHECK(d->crc32 != 0 || d->sha1[0] != 0);                    /* CRC of 235200 zero bytes */
    CHECK(d->status == MH_DISC_STATUS_NOTE);
    mh_disc_free(d);
    /* a missing BIN */
    put_text("m.cue", "FILE \"nope.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n");
    tmp_path("m.cue", p);
    d = mh_cue_parse(p);
    CHECK(!mh_db_verify_redump(db, d, NULL));
    mh_disc_free(d);
    CHECK(!mh_db_verify_redump(db, NULL, NULL));
}

/* a CUE that declares one track for a BIN that really holds data + a second data track */
static void test_recovery(const MhDb *db)
{
    char src[MH_PATH_MAX], p[MH_PATH_MAX], link[MH_PATH_MAX];
    MhDisc *d;
    snprintf(src, sizeof src, "%s/Breath of Fire IV (USA).bin", FIXTURES);
    if (access(src, R_OK) != 0) { fprintf(stderr, "skip recovery (fixture absent)\n"); return; }
    tmp_path("bof4.bin", link);
    CHECK(symlink(src, link) == 0);
    put_text("bof4.cue", "FILE \"bof4.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n");
    tmp_path("bof4.cue", p);
    d = mh_cue_parse(p);
    CHECK(d->ntracks == 1 && d->nfiles == 1);
    CHECK((d->flags & MH_DF_STRUCT_OK) && d->serial[0] != 0);
    CHECK(mh_db_apply_disc(db, d));
    CHECK(d->flags & MH_DF_TRACKS_SUSPECT);
    CHECK(mh_db_recover_track_map(db, d));
    CHECK(d->ntracks == 2 && (d->flags & MH_DF_TRACKS_FIXED) && !(d->flags & MH_DF_TRACKS_SUSPECT));
    CHECK(d->tracks[0].kind == 2 && d->tracks[0].start == 0 && d->tracks[0].length == 274263);
    CHECK(d->tracks[1].kind == 2 && d->tracks[1].pregap == 150 && d->tracks[1].start == 274413 && d->tracks[1].length == 18388);
    CHECK(d->tracks[1].index0 == 274263 && d->tracks[1].index1 == 274413);
    CHECK(d->ps.status == MH_PSS_TRACKSET && (d->flags & MH_DF_TRACKSET_OK));
    CHECK(note_has_end(d, "\r\nCUE reparado em memoria: mapa confirmado pelos SHA-1 de todas as tracks na ordem. Arquivos originais preservados."));
    mh_disc_free(d);
    /* the whole manager flow does the same */
    d = mh_manager_parse(db, p, NULL);
    CHECK(d->ntracks == 2 && (d->flags & MH_DF_TRACKS_FIXED) && d->status == MH_DISC_STATUS_OK);
    mh_disc_free(d);
}

int main(void)
{
    MhDb db;
    bool ok;
    tmp_init("r");
    load_db(&db, &ok);
    if (!ok) { printf("disc redump: skipped (database absent)\n"); rm_rf(g_tmp); return 0; }
    test_redump_negative(&db);
    if (access(FIXTURES "/Alundra (USA) (Rev 1).cue", R_OK) != 0) {
        printf("disc redump: fixtures absent, skipped the slow part\n");
    } else {
        test_redump(&db);
        test_recovery(&db);
    }
    mh_db_free(&db);
    rm_rf(g_tmp);
    printf("disc redump: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
