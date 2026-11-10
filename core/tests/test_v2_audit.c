/* v2 part A, audit regressions: hostile but CRC-consistent records must not break the loader's
 * invariants. Uses the image builder of test_v2_image.c. */
#pragma clang diagnostic ignored "-Wunused-function"
#define main image_test_main
#include "test_v2_image.c"
#undef main

static int g_checks;
#define CHECK(c)                                                          \
    do {                                                                  \
        g_checks++;                                                       \
        if (!(c)) {                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);           \
            abort();                                                      \
        }                                                                 \
    } while (0)

static MhProject *load(const uint8_t *img, MhV2Result *r)
{
    MhProject *p = calloc(1, sizeof *p);
    MhV2Env env;
    char cache[300];
    memset(&env, 0, sizeof env);
    env.menu = g_menu;
    env.menu_size = g_menu_size;
    snprintf(cache, sizeof cache, "%s/cache", g_dir);
    save(g_path, img, g_size);
    mh_project_init(p);
    if (!mh_v2_load_image(g_path, cache, &env, p, r)) {
        free(p);
        return NULL;
    }
    return p;
}

/* bin_count (+0x61) is a byte, MhDisc.files holds MH_DISC_MAX_FILES entries: a card with a larger
 * value must not make nfiles run past the array. */
static void test_bin_count(void)
{
    uint8_t *c = malloc(g_size);
    MhV2Result r;
    MhProject *p;
    unsigned i;
    memcpy(c, g_img, g_size);
    for (i = 0; i < NDISC; i++) {
        drec(c, i)[0x61] = 200;
        disc_rec_fix(c, i);
    }
    fix_header(c);
    save(g_path, c, g_size);
    CHECK(mh_v2_validate_image(g_path, NULL, &r));
    p = load(c, &r);
    CHECK(p != NULL);
    CHECK(p->cases[0].discs[0]->nfiles <= MH_DISC_MAX_FILES);
    CHECK(p->cases[1].discs[0]->nfiles <= MH_DISC_MAX_FILES);
    mh_project_clear(p);
    free(p);
    free(c);
}


static MhV2Env g_env;

static void hdr_crc(uint8_t *slot) { mh_v2_wr32(slot + MH_V2_HDR_CRC_OFF, mh_crc_record(slot, MH_V2_HDR_SIZE)); }

/* loads a mutated copy; returns the project (NULL on failure) and the result */
static MhProject *load_copy(const uint8_t *img, uint64_t size, MhV2Result *r)
{
    MhProject *p = calloc(1, sizeof *p);
    char cache[300];
    snprintf(cache, sizeof cache, "%s/cache", g_dir);
    save(g_path, img, size);
    mh_project_init(p);
    if (!mh_v2_load_image(g_path, cache, &g_env, p, r)) {
        free(p);
        return NULL;
    }
    return p;
}

#define MSG_ONE "V2 recuperado por um dos headers. Salve/aplique para reparar o par principal/backup."

static void expect_fail(uint8_t *c, const char *msg)
{
    MhV2Result r;
    MhProject *p = load_copy(c, g_size, &r);
    if (p || strcmp(r.msg, msg) != 0) {
        printf("FAIL expected '%s' got '%s'\n", msg, p ? "success" : r.msg);
        abort();
    }
    g_checks++;
}

/* which of the two header slots wins (original v2_load_source selection tree) */
static void test_header_selection(void)
{
    uint8_t *c = malloc(g_size);
    MhV2Result r;
    MhProject *p;
    uint8_t *A = c, *B = c + 0xa00;

    /* A broken, B good with matching tables: B, flagged for repair */
    memcpy(c, g_img, g_size);
    A[0x20] ^= 1;
    mh_v2_wr32(B + 0xc, 3);
    hdr_crc(B);
    p = load_copy(c, g_size, &r);
    CHECK(p && strcmp(r.msg, MSG_ONE) == 0 && p->dirty == 1 && p->reserved_c == 3);
    mh_project_clear(p);
    free(p);

    /* B broken, A good */
    memcpy(c, g_img, g_size);
    B[0x20] ^= 1;
    p = load_copy(c, g_size, &r);
    CHECK(p && strcmp(r.msg, MSG_ONE) == 0 && p->dirty == 1 && p->reserved_c == 4);
    mh_project_clear(p);
    free(p);

    /* both valid and different, same generation: refused */
    memcpy(c, g_img, g_size);
    mh_v2_wr32(B + 0x1ca, 0x1234);
    hdr_crc(B);
    expect_fail(c, "Headers V2 divergentes com a mesma geracao; selecao automatica recusada.");

    /* both valid and different: the newer generation wins, with a signed wrap */
    {
        static const struct { uint32_t a, b, want; } t[] = {
            {4, 5, 5}, {6, 5, 6}, {0xffffffffu, 0, 0}, {0, 0xffffffffu, 0}, {0x7fffffffu, 0x80000000u, 0x80000000u}};
        unsigned i;
        for (i = 0; i < sizeof t / sizeof *t; i++) {
            memcpy(c, g_img, g_size);
            mh_v2_wr32(A + 0xc, t[i].a);
            hdr_crc(A);
            mh_v2_wr32(B + 0xc, t[i].b);
            hdr_crc(B);
            p = load_copy(c, g_size, &r);
            CHECK(p && p->reserved_c == t[i].want && strcmp(r.msg, MSG_ONE) == 0);
            mh_project_clear(p);
            free(p);
        }
    }

    /* A stale (case table CRC differs), B matches: B */
    memcpy(c, g_img, g_size);
    mh_v2_wr32(A + 0x50, mh_v2_rd32(A + 0x50) ^ 1);
    hdr_crc(A);
    mh_v2_wr32(B + 0xc, 9);
    hdr_crc(B);
    p = load_copy(c, g_size, &r);
    CHECK(p && p->reserved_c == 9 && strcmp(r.msg, MSG_ONE) == 0);
    mh_project_clear(p);
    free(p);

    /* A matches, B stale in the disc table CRC: A, repair flagged */
    memcpy(c, g_img, g_size);
    mh_v2_wr32(B + 0x54, mh_v2_rd32(B + 0x54) ^ 1);
    hdr_crc(B);
    p = load_copy(c, g_size, &r);
    CHECK(p && p->reserved_c == 4 && strcmp(r.msg, MSG_ONE) == 0 && p->dirty == 1);
    mh_project_clear(p);
    free(p);

    /* A matches the case table but not the disc table, B fully matches: B */
    memcpy(c, g_img, g_size);
    mh_v2_wr32(A + 0x54, mh_v2_rd32(A + 0x54) ^ 1);
    hdr_crc(A);
    mh_v2_wr32(B + 0xc, 8);
    hdr_crc(B);
    p = load_copy(c, g_size, &r);
    CHECK(p && p->reserved_c == 8);
    mh_project_clear(p);
    free(p);

    /* neither matches */
    memcpy(c, g_img, g_size);
    mh_v2_wr32(A + 0x50, mh_v2_rd32(A + 0x50) ^ 1);
    hdr_crc(A);
    mh_v2_wr32(B + 0x50, mh_v2_rd32(B + 0x50) ^ 1);
    hdr_crc(B);
    expect_fail(c, "Nenhum header V2 valido corresponde as tabelas gravadas.");

    /* only B valid and stale */
    memcpy(c, g_img, g_size);
    A[0x20] ^= 1;
    mh_v2_wr32(B + 0x54, mh_v2_rd32(B + 0x54) ^ 1);
    hdr_crc(B);
    expect_fail(c, "Nenhum header V2 valido corresponde as tabelas gravadas.");

    /* a slot whose padding is not zero is an unreadable header */
    memcpy(c, g_img, g_size);
    c[0x930] = 1;
    expect_fail(c, "Nao foi possivel ler os headers V2.");
    memcpy(c, g_img, g_size);
    c[0xa00 + 0x9ff] = 1;
    expect_fail(c, "Nao foi possivel ler os headers V2.");

    /* header counts that disagree with the tables */
    memcpy(c, g_img, g_size);
    c[8] = 3;
    hdr_crc(A);
    memcpy(B, A, 0xa00);
    expect_fail(c, "As contagens do header V2 nao conferem com as tabelas.");
    memcpy(c, g_img, g_size);
    c[10] = 4;
    hdr_crc(A);
    memcpy(B, A, 0xa00);
    expect_fail(c, "As contagens do header V2 nao conferem com as tabelas.");
    free(c);
}

static void test_load_records(void)
{
    uint8_t *c = malloc(g_size);
    MhV2Result r;
    MhProject *p;

    memcpy(c, g_img, g_size);
    c[MH_V2_MENU_OFF + 0x931] = 1;
    expect_fail(c, "Padding do menu V2 invalido.");

    memcpy(c, g_img, g_size);
    mh_v2_wr16(crec(c, 0) + 0x38, 7);
    case_rec_fix(c, 0);
    fix_header(c);
    expect_fail(c, "ID da arte de uma Case e invalido.");

    memcpy(c, g_img, g_size);
    mh_v2_wr16(drec(c, 0) + 0x62, 2);
    disc_rec_fix(c, 0);
    fix_header(c);
    expect_fail(c, "ID da arte de um Disco e invalido.");

    /* a disc referenced by two cases */
    memcpy(c, g_img, g_size);
    crec(c, 1)[0x3a] = 0;
    case_rec_fix(c, 1);
    fix_header(c);
    expect_fail(c, "Existe um registro de Disco invalido no cartao.");

    /* sectors beyond the end of the file */
    memcpy(c, g_img, g_size);
    {
        MhProject *q = calloc(1, sizeof *q);
        char cache[300];
        snprintf(cache, sizeof cache, "%s/cache", g_dir);
        save(g_path, c, g_size - 512 * 100); /* the last package ends 91 blocks before the end */
        mh_project_init(q);
        CHECK(!mh_v2_load_image(g_path, cache, &g_env, q, &r));
        CHECK(strcmp(r.msg, "Existe um registro de Disco invalido no cartao.") == 0);
        free(q);
    }

    /* a card (not an image): kind, device number and the card text */
    memcpy(c, g_img, g_size);
    {
        MhIo io;
        char cache[300];
        p = calloc(1, sizeof *p);
        snprintf(cache, sizeof cache, "%s/cache", g_dir);
        save(g_path, c, g_size);
        CHECK(mh_io_open(&io, g_path, MH_IO_READ, false, NULL));
        mh_project_init(p);
        CHECK(mh_v2_load_io(&io, MH_V2_SRC_CARD, 3, g_size, NULL, cache, &g_env, p, &r));
        mh_io_close(&io);
        CHECK(strcmp(r.msg, "Cartao PSXRAW V2 carregado para edicao.") == 0);
        CHECK(p->card_loaded == 1 && p->image_loaded == 0 && p->device == 3 && p->dirty == 0);
        CHECK(p->cases[0].discs[0]->kind == MH_DISC_KIND_V2_DEVICE && p->cases[0].discs[0]->v2_drive == 3);
        CHECK(p->cases[0].discs[0]->source_path[0] == 0);
        mh_project_clear(p);
        free(p);
    }

    /* damaged multi-track map: listed, preserved, but "corrigivel" */
    memcpy(c, g_img, g_size);
    drec(c, 1)[0x60] = 2;
    disc_rec_fix(c, 1);
    c[BASE[1] * 512 + 0x40] ^= 1;
    fix_header(c);
    p = load_copy(c, g_size, &r);
    CHECK(p != NULL);
    CHECK(strcmp(r.msg, "PSXRAW V2 reconhecido pelas tabelas fixas. 1 disco(s) possuem Track Table multifaixa "
                        "danificada; os jogos foram listados e preservados, mas APLICAR permanece bloqueado ate "
                        "informar os CUEs originais.") == 0);
    {
        const MhDisc *d = p->cases[0].discs[1];
        CHECK(d->status == MH_DISC_STATUS_FIXABLE && d->ntracks == 2 && d->flags == (MH_DF_FROM_V2 | MH_DF_TRACKSET_CHECKED | MH_DF_SERIAL));
        CHECK(strstr(d->notes, "possui varias faixas") != NULL && p->dirty == 1);
    }
    mh_project_clear(p);
    free(p);

    /* a damaged cheat blob: the disc loads, the catalog is flagged for regeneration */
    memcpy(c, g_img, g_size);
    c[(BASE[0] + 5) * 512 + 20] ^= 0xff;
    p = load_copy(c, g_size, &r);
    CHECK(p != NULL);
    CHECK(strcmp(r.msg, "V2 carregado com catalogo de cheats anterior ou invalido. A atualizacao sera informada por "
                        "disco.") == 0);
    CHECK(p->cases[0].discs[0]->cheats == NULL && p->cases[0].discs[0]->cheat_regenerated == 1 &&
          p->cases[0].discs[0]->cheat_note[0] != 0 && p->dirty == 1);
    mh_project_clear(p);
    free(p);

    /* SBI block in the track map is extracted to the cache */
    memcpy(c, g_img, g_size);
    {
        uint8_t *t = c + BASE[2] * 512;
        static const uint8_t sbi[8] = {'S', 'B', 'I', 0, 1, 2, 3, 4};
        MhProject *q;
        uint8_t *got = NULL;
        uint64_t gotn = 0;
        t[10] |= 1;
        mh_v2_wr16(t + 0x640, 8);
        mh_v2_wr32(t + 0x644, mh_crc32(sbi, 8));
        memcpy(t + 0x648, sbi, 8);
        mh_v2_wr32(t + 0xc, 0);
        mh_v2_wr32(t + 0xc, mh_crc32(t, 0x930));
        q = load_copy(c, g_size, &r);
        CHECK(q != NULL);
        CHECK((q->cases[1].discs[0]->flags & MH_DF_SBI) != 0);
        CHECK(mh_read_entire_file(q->cases[1].discs[0]->sbi_path, &got, &gotn) && gotn == 8 && memcmp(got, sbi, 8) == 0);
        free(got);
        mh_project_clear(q);
        free(q);
    }

    /* resume marker with a verification stamp marks the disc verified; a different record does not */
    {
        MhV2Resume rs;
        uint8_t blk[512];
        unsigned pass;
        for (pass = 0; pass < 3; pass++) {
            const MhDisc *d;
            memcpy(c, g_img, g_size);
            memset(&rs, 0, sizeof rs);
            memcpy(rs.disc_rec, drec(c, 2), MH_V2_DISC_SIZE);
            rs.stamp = 77;
            rs.has_verify = pass != 1;
            rs.verify_stamp = 0x0000000200000040ull;
            if (pass == 2) rs.disc_rec[0x7c] ^= 1;
            mh_v2_resume_encode(&rs, blk);
            memcpy(c + (BASE[2] + 0xff) * 512, blk, 512);
            p = load_copy(c, g_size, &r);
            CHECK(p != NULL);
            d = p->cases[1].discs[0];
            if (pass == 0) {
                CHECK(d->verified == 1 && d->verify_issues == 0x40 && d->verify_accepted == 2);
                CHECK(d->verify_crc32 == d->crc32 && memcmp(d->verify_sha1, d->sha1, 20) == 0);
                CHECK(strstr(d->verify_text, "Alguns setores estao inconsistentes") == d->verify_text);
            } else {
                CHECK(d->verified == 0 && d->verify_issues == 0);
            }
            mh_project_clear(p);
            free(p);
        }
    }
    free(c);
}


static MhProgress *g_pp;
static int g_progress_calls;
static void cancel_after_first(void *u, uint64_t d, uint64_t t, const char *x)
{
    (void)u; (void)d; (void)t; (void)x;
    g_progress_calls++;
    mh_progress_request_cancel(g_pp);
}

/* sd_write_image: the headers are invalidated first (A, then B) and nothing publishes them again
 * before the body is complete, so a cancelled write leaves a card that is "uncommitted". */
static void test_sd_write_cancel(void)
{
    char dev[300], image[300], err[512];
    uint8_t *card = calloc(1, g_size + 0x100000), *back = malloc(g_size + 0x100000), *img = malloc(g_size);
    MhDiskInfo disk;
    MhProgress prog;
    int fd;
    memcpy(card, g_img, g_size);
    memcpy(img, g_img, g_size);
    img[0x1400 + 3] ^= 0xff; /* the body of the new image differs from the old card */
    snprintf(dev, sizeof dev, "%s/card.bin", g_dir);
    snprintf(image, sizeof image, "%s/new.bin", g_dir);
    save(dev, card, g_size + 0x100000);
    save(image, img, g_size);
    memset(&disk, 0, sizeof disk);
    disk.size = g_size + 0x100000;
    g_v2_dev_path = dev;
    g_pp = &prog;
    mh_progress_init(&prog, cancel_after_first, NULL);
    CHECK(!mh_sd_write_image(&disk, NULL, image, &prog, err, sizeof err));
    CHECK(strcmp(err, "Gravacao cancelada. Os headers permaneceram invalidos.") == 0 && g_progress_calls == 1);
    fd = open(dev, O_RDONLY);
    CHECK(fd >= 0 && read(fd, back, g_size + 0x100000) == (ssize_t)(g_size + 0x100000));
    close(fd);
    CHECK(memcmp(back, "PSXRAW", 6) != 0 && memcmp(back + 0xa00, "PSXRAW", 6) != 0);
    CHECK(!mh_sd_v2_header_raw_valid(back) && !mh_sd_v2_header_raw_valid(back + 0xa00));
    CHECK(back[0x1400 + 3] == img[0x1400 + 3]); /* the first chunk of the body was written */
    g_v2_dev_path = NULL;
    free(card);
    free(back);
    free(img);
}

int main(void)
{
    MhV2Result r;
    snprintf(g_dir, sizeof g_dir, "/tmp/mhv2audit.XXXXXX");
    if (!mkdtemp(g_dir)) return 1;
    if (!mh_read_entire_file("resources/embedded/picostation_menu.bin", &g_menu, &g_menu_size) ||
        !mh_read_entire_file("resources/embedded/umbrella_music.vag", &g_music, &g_music_size)) {
        puts("fixtures missing, skipped");
        return 0;
    }
    snprintf(g_path, sizeof g_path, "%s/img.bin", g_dir);
    g_size = build(&g_img);
    save(g_path, g_img, g_size);
    CHECK(mh_v2_validate_image(g_path, NULL, &r));

    memset(&g_env, 0, sizeof g_env);
    g_env.menu = g_menu;
    g_env.menu_size = g_menu_size;
    test_bin_count();
    test_header_selection();
    test_load_records();
    test_sd_write_cancel();

    {
        char cmd[512];
        snprintf(cmd, sizeof cmd, "rm -rf %s", g_dir);
        (void)!system(cmd);
    }
    printf("v2 audit ok (%d checks)\n", g_checks);
    return 0;
}
