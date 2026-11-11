/* mh_v2_hydrate_project after a card load. The card stores the case art on the case, not on the discs, so
 * the default art found by serial must not make an unchanged card show "changes pending". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mh/v2write.h"
#include "v2_stubs.inc"

#define EMBEDDED "resources/embedded"

static int g_fail, g_checks;
#define CHECK(c)                                                                  \
    do {                                                                          \
        g_checks++;                                                               \
        if (!(c)) {                                                               \
            g_fail++;                                                             \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);          \
        }                                                                         \
    } while (0)

/* one case with one disc of the given serial; case_art is the art the card gave the case ("" for none) */
static void make_project(MhProject *p, const char *serial, const char *case_art)
{
    MhCase *c;
    MhDisc *d = mh_calloc(sizeof *d);
    mh_project_init(p);
    mh_str_copy(d->serial, sizeof d->serial, serial);
    c = &p->cases[0];
    c->in_use = 1;
    c->disc_count = 1;
    c->discs[0] = d;
    mh_str_copy(c->art_path, sizeof c->art_path, case_art);
    p->case_count = 1;
    p->disc_count = 1;
    p->card_loaded = 1;
}

int main(void)
{
    static MhDb db;
    MhV2Env env;
    MhProject *p = calloc(1, sizeof *p);
    char dir[MH_PATH_MAX], art[MH_PATH_MAX];
    const char *t = getenv("TMPDIR");

    if (access(EMBEDDED "/disc_index.bin", R_OK) != 0) {
        printf("embedded resources absent, skipping\n");
        return 0;
    }
    snprintf(dir, sizeof dir, "%s/mhhydrate-XXXXXX", t && t[0] ? t : "/tmp");
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 2; }
    snprintf(art, sizeof art, "%s/database", dir); mkdir(art, 0755);
    snprintf(art, sizeof art, "%s/database/art", dir); mkdir(art, 0755);
    snprintf(art, sizeof art, "%s/database/art/default", dir); mkdir(art, 0755);
    snprintf(art, sizeof art, "%s/database/art/default/SLUS-00553-CASE.art", dir);
    { FILE *f = fopen(art, "wb"); fputs("x", f); fclose(f); }
    CHECK(mh_db_load_dir(&db, EMBEDDED, dir));
    memset(&env, 0, sizeof env);
    env.db = &db;

    /* the card gave the case its art: nothing new, so the project stays clean */
    make_project(p, "SLUS-00553", "/cache/CASE-1.art");
    mh_v2_hydrate_project(p, &env);
    CHECK(p->dirty == 0);
    CHECK(strcmp(p->cases[0].art_path, "/cache/CASE-1.art") == 0);   /* the card art is kept */
    mh_project_clear(p);

    /* the case has no art and the default art is found: that is a real change to apply */
    make_project(p, "SLUS-00553", "");
    mh_v2_hydrate_project(p, &env);
    CHECK(p->dirty == 1);
    CHECK(strstr(p->cases[0].art_path, "SLUS-00553-CASE.art") != NULL);
    mh_project_clear(p);

    free(p);
    printf("test_v2_hydrate: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail != 0;
}
