/* cheat module: code text, both MHDialect variants, classic conversion, TXT/CHT import and
 * export. Every error text is checked byte for byte. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/cheat/cheat_internal.h"
#include "mh/base.h"
#include "mh/cheat.h"

static int g_checks;
#define CHECK(c)                                                                  \
    do {                                                                          \
        g_checks++;                                                               \
        if (!(c)) {                                                               \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
            abort();                                                              \
        }                                                                         \
    } while (0)
#define CHECK_STR(a, b)                                                                   \
    do {                                                                                  \
        const char *_a = (a), *_b = (b);                                                  \
        g_checks++;                                                                       \
        if (strcmp(_a, _b) != 0) {                                                        \
            fprintf(stderr, "%s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, _a, _b);     \
            abort();                                                                      \
        }                                                                                 \
    } while (0)

static char err[512];
#define V5 "MHDialect=V5ORIG_BANK_V01\n"
#define CL "MHDialect=GAMESHARK_CLASSIC_V1\n"

static void expect_lines(const char *text, const char *hex)
{
    uint8_t *lines = NULL;
    uint16_t n = 0;
    MhWbuf w;
    char want[2048];
    size_t k = 0;
    if (!mh_cheat_parse_codes(text, &lines, &n, err, sizeof err)) {
        fprintf(stderr, "parse failed: %s\ntext: %s\n", err, text);
        abort();
    }
    mh_wbuf_init(&w);
    CHECK(mh_cheat_codes_to_text(lines, n, &w));
    /* hex is "XXXXXXXX XXXX/XXXXXXXX XXXX" */
    for (const char *s = hex; *s; s++) {
        if (*s == '/') {
            want[k++] = '\r';
            want[k++] = '\n';
        } else {
            want[k++] = *s;
        }
    }
    want[k++] = '\r';
    want[k++] = '\n';
    want[k] = 0;
    CHECK_STR(mh_wbuf_cstr(&w), want);
    mh_wbuf_free(&w);
    free(lines);
}

static void expect_error(const char *text, const char *msg)
{
    uint8_t *lines = (uint8_t *)1;
    uint16_t n = 77;
    if (mh_cheat_parse_codes(text, &lines, &n, err, sizeof err)) {
        fprintf(stderr, "expected failure (%s) but parsed %u lines\ntext: %s\n", msg, n, text);
        abort();
    }
    if (strcmp(err, msg) != 0) {
        fprintf(stderr, "text: %s\n got: %s\nwant: %s\n", text, err, msg);
        abort();
    }
    CHECK(lines == NULL && n == 0);
}

static void test_code_line(void)
{
    uint8_t o[6];
    static const uint8_t want[6] = {0x80, 0x05, 0xE9, 0xCC, 0x03, 0xE8};
#define P(s) mh_cheat_parse_code_line((s), strlen(s), o)
    CHECK(P("8005E9CC 03E8") && memcmp(o, want, 6) == 0);
    CHECK(P("8005e9cc 03e8") && memcmp(o, want, 6) == 0);
    CHECK(P("8005E9CC03E8") && memcmp(o, want, 6) == 0);
    CHECK(P("8005E9CC:03E8") && memcmp(o, want, 6) == 0);
    CHECK(P("8005-E9CC-03E8") && memcmp(o, want, 6) == 0);
    CHECK(P("\t8005E9CC\t03E8") && memcmp(o, want, 6) == 0);
    CHECK(P("8005E9CC 03E8 ; note") && memcmp(o, want, 6) == 0);
    CHECK(P("8005E9CC 03E8 # note") && memcmp(o, want, 6) == 0);
    CHECK(P("8005E9CC 03E8 // note") && memcmp(o, want, 6) == 0);
    CHECK(P("8005E9CC 03E8 \\") && memcmp(o, want, 6) == 0);
    CHECK(P("8005E9CC 03E8 \\ \t") && memcmp(o, want, 6) == 0);
    CHECK(!P("8005E9CC 03E8 \\ x"));
    CHECK(!P("8005E9CC 03E8 /"));
    CHECK(!P("8005E9CC 03E8 /x"));
    CHECK(!P("8005E9CC 03E"));
    CHECK(!P("8005E9CC 03E80"));
    CHECK(!P("8005E9CC 03EG"));
    CHECK(!P("8005E9CC 03E8,"));
    CHECK(!P(""));
    CHECK(!P("# 8005E9CC 03E8"));
    CHECK(!mh_cheat_parse_code_line(NULL, 0, o));
    CHECK(!mh_cheat_parse_code_line("8005E9CC 03E8", 0, o)); /* length 0 */
    CHECK(mh_cheat_parse_code_line("8005E9CC 03E8 junk", 13, o)); /* the length decides */
#undef P
}

static void test_parse_v5(void)
{
    expect_lines(V5 "8005E9CC 03E8", "8005E9CC 03E8");
    expect_lines("mhdialect=v5orig_bank_v01\r\n8005e9cc 03e8\r\n", "8005E9CC 03E8");
    expect_lines("  # c\n; c2\n\n  " V5 "\t7003C1FA F7FF   \n\n3005E9CC 0001\n\n", "7003C1FA F7FF/3005E9CC 0001");
    expect_lines(V5 "9003C1FA F7FF\n31000000 0001\n70000000 0000\n80000000 0001", "9003C1FA F7FF/31000000 0001/70000000 0000/80000000 0001");

    expect_error(NULL, "Informe os codigos GameShark.");
    expect_error("", "A primeira linha deve informar MHDialect.");
    expect_error("   \n# only a comment\n", "A primeira linha deve informar MHDialect.");
    expect_error("8005E9CC 03E8\n", "A primeira linha deve ser MHDialect=V5ORIG_BANK_V01 ou MHDialect=GAMESHARK_CLASSIC_V1.");
    expect_error("MHDialect=FOO\n", "MHDialect aceitos: V5ORIG_BANK_V01 ou GAMESHARK_CLASSIC_V1.");
    expect_error("MHDialect=V5ORIG_BANK_V01 \nMHDialect=V5ORIG_BANK_V01\n", "Informe MHDialect apenas uma vez, antes dos codigos.");
    expect_error(V5 CL "8005E9CC 03E8", "Informe MHDialect apenas uma vez, antes dos codigos.");
    expect_error(V5 "8005E9CC 03E8\nMHDialect=V5ORIG_BANK_V01\n", "Informe MHDialect apenas uma vez, antes dos codigos.");
    expect_error(V5 "8005E9CC 03E\n", "Linha de cheat invalida. Use XXXXXXXX XXXX.");
    expect_error(V5 "hello\n", "Linha de cheat invalida. Use XXXXXXXX XXXX.");
    expect_error(V5 "// c\n", "Linha de cheat invalida. Use XXXXXXXX XXXX.");
    /* no program at all, or lines outside 30/31/70/80/90 */
    expect_error(V5, "V5ORIG_BANK_V01 aceita somente os tipos nativos 30/31/70/80/90 do runtime auditado.");
    expect_error(V5 "D00A1234 0001\n800A1238 0002", "V5ORIG_BANK_V01 aceita somente os tipos nativos 30/31/70/80/90 do runtime auditado.");
    expect_error(V5 "50000204 0001\n300A1000 0010", "V5ORIG_BANK_V01 aceita somente os tipos nativos 30/31/70/80/90 do runtime auditado.");
    expect_error(V5 "7003C1FA F7FF", "V5ORIG_BANK_V01 aceita somente os tipos nativos 30/31/70/80/90 do runtime auditado."); /* prefix at the end */
    expect_error(V5 "20000000 0001", "V5ORIG_BANK_V01 aceita somente os tipos nativos 30/31/70/80/90 do runtime auditado.");

    /* 255 lines are accepted, the 256th is not */
    {
        MhWbuf w;
        uint8_t *lines;
        uint16_t n;
        mh_wbuf_init(&w);
        mh_wbuf_puts(&w, V5);
        for (int i = 0; i < 255; i++) mh_wbuf_printf(&w, "30%06X 00%02X\n", 0x0A0000 + i, i & 0xff);
        CHECK(mh_cheat_parse_codes(mh_wbuf_cstr(&w), &lines, &n, err, sizeof err) && n == 255);
        CHECK(lines[254 * 6 + 3] == 0xFE && lines[254 * 6 + 5] == 0xFE);
        free(lines);
        mh_wbuf_puts(&w, "30FFFFFF 0000\n");
        expect_error(mh_wbuf_cstr(&w), "Linha de cheat invalida. Use XXXXXXXX XXXX.");
        mh_wbuf_free(&w);
    }
    /* a dialect line of 64 or more characters is just a (bad) code line */
    expect_error(V5 "MHDialect=V5ORIG_BANK_V01                                            \n", "Informe MHDialect apenas uma vez, antes dos codigos."); /* trailing blanks are trimmed first */
    expect_error(V5 "MHDialect=V5ORIG_BANK_V01                                      x\n", "Linha de cheat invalida. Use XXXXXXXX XXXX.");
}

static void test_parse_classic(void)
{
    /* plain writes pass through */
    expect_lines(CL "300A1234 0063\n800A1236 03E8", "300A1234 0063/800A1236 03E8");
    expect_lines(CL "30010000 0001", "30010000 0001"); /* lowest RAM address */
    expect_lines(CL "801FFFFE 0001", "801FFFFE 0001"); /* highest even address */
    expect_lines(CL "301FFFFF 0001", "301FFFFF 0001"); /* write8 may be odd */
    /* D0 / D1 become the 70 / 90 prefixes of V5 */
    expect_lines(CL "D00A1234 0001\n800A1238 0002", "700A1234 0001/800A1238 0002");
    expect_lines(CL "D10A1234 0001\n300A1238 0002", "900A1234 0001/300A1238 0002");
    /* 50 slide: NN=2 SS=4 VVVV=0001 */
    expect_lines(CL "50000204 0001\n300A1000 0010", "300A1000 0010/300A1004 0011");
    expect_lines(CL "50000302 FFFF\n800A1000 0001", "800A1000 0001/800A1002 0000/800A1004 FFFF");
    expect_lines(CL "50000101 0001\n300A1000 0010", "300A1000 0010");
    /* D0 + 50: the condition is repeated before every write */
    expect_lines(CL "D00A0000 0001\n50000204 0001\n300A1000 0010",
                 "700A0000 0001/300A1000 0010/700A0000 0001/300A1004 0011");
    expect_lines(CL "D10A0000 0001\n50000104 0001\n300A1000 0010\n800A2000 0003",
                 "900A0000 0001/300A1000 0010/800A2000 0003");

    expect_error(CL, "Nenhuma linha GameShark classica foi informada.");
    expect_error(CL "300A1234 0063\nMHDialect=V5ORIG_BANK_V01\n", "Informe MHDialect apenas uma vez, antes dos codigos.");
    expect_error(CL "30001234 0001", "Escrita 30/80 usa endereco fora da RAM segura do V5 ou WRITE16 desalinhado.");
    expect_error(CL "30200000 0001", "Escrita 30/80 usa endereco fora da RAM segura do V5 ou WRITE16 desalinhado.");
    expect_error(CL "800A1235 0001", "Escrita 30/80 usa endereco fora da RAM segura do V5 ou WRITE16 desalinhado.");
    expect_error(CL "801FFFFF 0001", "Escrita 30/80 usa endereco fora da RAM segura do V5 ou WRITE16 desalinhado.");
    expect_error(CL "31000000 0001", "Opcode classico 0x31 nao tem conversao exata comprovada para o GameShark V5 original. Use apenas 30, 80, D0, D1 e 50+30/80.");
    expect_error(CL "C00A1234 0001", "Opcode classico 0xC0 nao tem conversao exata comprovada para o GameShark V5 original. Use apenas 30, 80, D0, D1 e 50+30/80.");
    expect_error(CL "E00A1234 0001", "Opcode classico 0xE0 nao tem conversao exata comprovada para o GameShark V5 original. Use apenas 30, 80, D0, D1 e 50+30/80.");
    expect_error(CL "D00A1235 0001\n800A1238 0002", "Condicao D0/D1 usa endereco invalido ou desalinhado para o V5.");
    expect_error(CL "D0000000 0001\n300A1238 0002", "Condicao D0/D1 usa endereco invalido ou desalinhado para o V5.");
    expect_error(CL "D01FFFFF 0001\n300A1238 0002", "Condicao D0/D1 usa endereco invalido ou desalinhado para o V5.");
    expect_error(CL "D00A1234 0001", "Condicao D0/D1 sem acao seguinte.");
    expect_error(CL "D00A1234 0001\nD00A1238 0002\n300A1238 0002", "Cadeias de duas ou mais condicoes classicas nao possuem conversao exata para o V5 original.");
    expect_error(CL "D00A1234 0001\nD10A1238 0002\n300A1238 0002", "Cadeias de duas ou mais condicoes classicas nao possuem conversao exata para o V5 original.");
    expect_error(CL "D00A1234 0001\nE30A1238 0002\n300A1238 0002", "Cadeias de duas ou mais condicoes classicas nao possuem conversao exata para o V5 original.");
    expect_error(CL "D00A1234 0001\nE00A1238 0002\n300A1238 0002", "Cadeias de duas ou mais condicoes classicas nao possuem conversao exata para o V5 original.");
    expect_error(CL "D00A1234 0001\n310A1238 0002", "D0/D1 classico precisa controlar uma escrita 30/80 ou um slide 50 + 30/80.");
    expect_error(CL "D00A1234 0001\n800A1239 0002", "A escrita controlada por D0/D1 usa endereco fora da RAM segura do V5 ou WRITE16 desalinhado.");
    expect_error(CL "D00A1234 0001\n30000000 0002", "A escrita controlada por D0/D1 usa endereco fora da RAM segura do V5 ou WRITE16 desalinhado.");
    /* 50 slides */
    expect_error(CL "50000204 0001", "Codigo 50 incompleto: falta a linha seguidora 30/80.");
    expect_error(CL "50010204 0001\n300A1000 0010", "Formato 50 classico invalido. Use 5000NNSS VVVV.");
    expect_error(CL "50000004 0001\n300A1000 0010", "Codigo 50 com contagem zero nao e importado.");
    expect_error(CL "50000204 0001\n310A1000 0010", "GAMESHARK_CLASSIC_V1: o seguidor do 50 precisa ser 30 ou 80.");
    expect_error(CL "50000204 0001\n30001000 0010", "Codigo 50 gera endereco fora da RAM segura do V5 ou WRITE16 desalinhado.");
    expect_error(CL "50000210 0001\n301FFFF0 0010", "Codigo 50 gera endereco fora da RAM segura do V5 ou WRITE16 desalinhado.");
    expect_error(CL "50000203 0001\n800A1000 0010", "Codigo 50 gera endereco fora da RAM segura do V5 ou WRITE16 desalinhado.");
    expect_error(CL "50000201 0001\n800A1001 0010", "Codigo 50 gera endereco fora da RAM segura do V5 ou WRITE16 desalinhado.");
    expect_error(CL "D00A1000 0001\n50000204 0001\n300A1000 0010", "Condicao D0/D1 + slide 50 altera o proprio endereco testado; essa sequencia nao possui conversao exata para o V5.");
    expect_error(CL "D00A1000 0001\n50000204 0001\n300A1001 0010", "Condicao D0/D1 + slide 50 altera o proprio endereco testado; essa sequencia nao possui conversao exata para o V5.");
    expect_error(CL "D00A1000 0001\n50000204 0001\n800A1000 0010", "Condicao D0/D1 + slide 50 altera o proprio endereco testado; essa sequencia nao possui conversao exata para o V5.");
    expect_error(CL "D00A1004 0001\n50000204 0001\n300A1000 0010", "Condicao D0/D1 + slide 50 altera o proprio endereco testado; essa sequencia nao possui conversao exata para o V5."); /* second step lands on it */
    /* write8 below, write16 two below, or beyond the tested pair: no overlap */
    expect_lines(CL "D00A1000 0001\n50000204 0001\n300A0FFF 0010", "700A1000 0001/300A0FFF 0010/700A1000 0001/300A1003 0011");
    expect_lines(CL "D00A1000 0001\n50000204 0001\n800A0FFE 0010", "700A1000 0001/800A0FFE 0010/700A1000 0001/800A1002 0011");
    expect_lines(CL "D00A1000 0001\n50000204 0001\n300A1002 0010", "700A1000 0001/300A1002 0010/700A1000 0001/300A1006 0011");

    /* 255 V5 lines is the limit of a program */
    {
        MhWbuf w;
        uint8_t *lines;
        uint16_t n;
        mh_wbuf_init(&w);
        mh_wbuf_puts(&w, CL);
        for (int i = 0; i < 255; i++) mh_wbuf_printf(&w, "300A%04X 0001\n", i * 2);
        CHECK(mh_cheat_parse_codes(mh_wbuf_cstr(&w), &lines, &n, err, sizeof err) && n == 255);
        free(lines);
        mh_wbuf_free(&w);
        /* 253 writes (253 source lines) and a three step slide (2 source lines) make 256 lines */
        mh_wbuf_init(&w);
        mh_wbuf_puts(&w, CL);
        for (int i = 0; i < 253; i++) mh_wbuf_printf(&w, "300A%04X 0001\n", i * 2);
        mh_wbuf_puts(&w, "50000302 0001\n300B0000 0010\n");
        expect_error(mh_wbuf_cstr(&w), "A conversao do GameShark classico excede 255 linhas V5.");
        mh_wbuf_free(&w);
    }
    {
        /* condition + slide: one write plus 127 steps of 2 lines make exactly 255, 128 steps do not */
        MhWbuf w;
        uint8_t *lines;
        uint16_t n;
        mh_wbuf_init(&w);
        mh_wbuf_puts(&w, CL "300C0000 0001\nD00B0000 0001\n50007F04 0001\n300A1000 0010\n");
        CHECK(mh_cheat_parse_codes(mh_wbuf_cstr(&w), &lines, &n, err, sizeof err) && n == 255);
        CHECK(lines[0] == 0x30 && lines[6] == 0x70 && lines[254 * 6] == 0x30 && lines[254 * 6 + 3] == 0xf8);
        free(lines);
        mh_wbuf_free(&w);
        mh_wbuf_init(&w);
        mh_wbuf_puts(&w, CL "300C0000 0001\nD00B0000 0001\n50008004 0001\n300A1000 0010\n");
        expect_error(mh_wbuf_cstr(&w), "A conversao do GameShark classico excede 255 linhas V5.");
        mh_wbuf_free(&w);
        /* 127 condition pairs and a write: 255 lines from 255 source lines */
        mh_wbuf_init(&w);
        mh_wbuf_puts(&w, CL);
        for (int i = 0; i < 127; i++) mh_wbuf_printf(&w, "D00A%04X 0001\n300B0000 0002\n", i * 2);
        mh_wbuf_puts(&w, "300C0000 0001\n");
        CHECK(mh_cheat_parse_codes(mh_wbuf_cstr(&w), &lines, &n, err, sizeof err) && n == 255);
        free(lines);
        /* one more source line is one too many */
        mh_wbuf_puts(&w, "300C0002 0001\n");
        expect_error(mh_wbuf_cstr(&w), "Linha de cheat invalida. Use XXXXXXXX XXXX.");
        mh_wbuf_free(&w);
    }
}

/* ---- files ------------------------------------------------------------------------------------- */
static char g_dir[256];

static char *path_of(const char *name)
{
    static char buf[8][512];
    static int k;
    char *p = buf[k++ & 7];
    snprintf(p, 512, "%s/%s", g_dir, name);
    return p;
}

static void put(const char *name, const char *data, size_t n)
{
    CHECK(mh_write_entire_file(path_of(name), data, n));
}

static void put_s(const char *name, const char *text) { put(name, text, strlen(text)); }

static void put_utf16(const char *name, const char *ascii, int bom)
{
    size_t n = strlen(ascii), o = 0;
    uint8_t *b = malloc(n * 2 + 2);
    if (bom) {
        b[o++] = 0xff;
        b[o++] = 0xfe;
    }
    for (size_t i = 0; i < n; i++) {
        b[o++] = (uint8_t)ascii[i];
        b[o++] = 0;
    }
    put(name, (char *)b, o);
    free(b);
}

static int imp(MhCheatProfile *p, const char *name, const char *dialect, int *count)
{
    return mh_cheat_profile_import_text_dialect(p, path_of(name), dialect, count, err, sizeof err);
}

static void imp_err(const char *text, const char *dialect, const char *msg)
{
    MhCheatProfile *p = mh_cheat_profile_new();
    int n = 99;
    put_s("e.txt", text);
    if (imp(p, "e.txt", dialect, &n)) {
        fprintf(stderr, "expected failure (%s) but imported %d\ntext: %s\n", msg, n, text);
        abort();
    }
    if (strcmp(err, msg) != 0) {
        fprintf(stderr, "text: %s\n got: %s\nwant: %s\n", text, err, msg);
        abort();
    }
    CHECK(n == 0 && p->count == 0);
    mh_cheat_profile_free(p);
}

static void test_import(void)
{
    MhCheatProfile *p = mh_cheat_profile_new();
    int n = 0;
    uint8_t one[6] = {0x80, 0x05, 0xE9, 0xCC, 0x03, 0xE8};

    /* [NAME] blocks, V5 dialect in the file */
    put_s("a.txt", V5 "[Infinite Health]\n8005E9CC 03E8\n\n[Max Money]\r\n3005E9D0 0063\r\n7003C1FA F7FF\r\n800A0000 0001\r\n");
    CHECK(imp(p, "a.txt", NULL, &n) && n == 2 && p->count == 2);
    CHECK_STR(p->entries[0].name, "INFINITE HEALTH");
    CHECK(p->entries[0].flags == 0x05 && p->entries[0].nlines == 1 && memcmp(p->entries[0].lines, one, 6) == 0);
    CHECK_STR(p->entries[1].name, "MAX MONEY");
    CHECK(p->entries[1].nlines == 3 && p->entries[1].game == 0 && p->entries[1].variant == 0xFFFFFFFFu);
    CHECK(mh_cheat_profile_validate(p, err, sizeof err));
    mh_cheat_profile_free(p);

    /* the other layouts: bare name line, "NAME CODE", comment lines, code with no name */
    p = mh_cheat_profile_new();
    put_s("b.txt", "MHDialect=V5ORIG_BANK_V01\n# comment\nGod Mode\n8005E9CC 03E8\n8005E9D0 0001\n"
                   "Lives - 3005E9D4 0009\nSecond Line: 8005E9D8 000A\nThe End\n8005E9DC 0001 ; tail\n");
    CHECK(imp(p, "b.txt", NULL, &n) && n == 4);
    CHECK_STR(p->entries[0].name, "GOD MODE");
    CHECK(p->entries[0].nlines == 2);
    CHECK_STR(p->entries[1].name, "LIVES");
    CHECK_STR(p->entries[2].name, "SECOND LINE");
    CHECK_STR(p->entries[3].name, "THE END");
    mh_cheat_profile_free(p);

    p = mh_cheat_profile_new();
    put_s("Some Game.txt", V5 "8005E9CC 03E8\n");
    CHECK(imp(p, "Some Game.txt", NULL, &n) && n == 1);
    CHECK_STR(p->entries[0].name, "SOME GAME"); /* the file name is the title */
    mh_cheat_profile_free(p);

    /* dialect chosen by the caller, file without a dialect line */
    p = mh_cheat_profile_new();
    put_s("c.txt", "[Slide]\n50000204 0001\n300A1000 0010\n[Cond]\nD00A0000 0001\n800A0004 0002\n");
    CHECK(imp(p, "c.txt", "MHDialect=GAMESHARK_CLASSIC_V1", &n) && n == 2);
    CHECK(p->entries[0].flags == 0x45 && p->entries[0].nlines == 2);
    CHECK(p->entries[1].flags == 0x45 && p->entries[1].lines[0] == 0x70);
    mh_cheat_profile_free(p);
    p = mh_cheat_profile_new();
    CHECK(!imp(p, "c.txt", "MHDialect=V5ORIG_BANK_V01", &n));
    CHECK_STR(err, "V5ORIG_BANK_V01 aceita somente os tipos nativos 30/31/70/80/90 do runtime auditado.");
    CHECK(n == 0 && p->count == 0);
    CHECK(!imp(p, "c.txt", NULL, &n));
    CHECK_STR(err, "O arquivo deve comecar com MHDialect=V5ORIG_BANK_V01 ou GAMESHARK_CLASSIC_V1.");
    CHECK(!imp(p, "c.txt", "MHDialect=OTHER", &n));
    CHECK_STR(err, "Selecione GameShark classico ou V5/Xploder para importar.");
    put_s("d.txt", CL "[X]\n300A0000 0001\n");
    CHECK(!imp(p, "d.txt", "MHDialect=V5ORIG_BANK_V01", &n));
    CHECK_STR(err, "A versao indicada no arquivo difere da escolhida na importacao.");
    CHECK(imp(p, "d.txt", "mhdialect=gameshark_classic_v1", &n) && n == 1 && p->entries[0].flags == 0x45);
    mh_cheat_profile_free(p);

    /* UTF-8 with BOM and UTF-16LE with BOM */
    p = mh_cheat_profile_new();
    put_s("u8.txt", "\xef\xbb\xbf" V5 "[Caf\xc3\xa9 Mode]\n8005E9CC 03E8\n");
    CHECK(imp(p, "u8.txt", NULL, &n) && n == 1);
    CHECK_STR(p->entries[0].name, "CAFE MODE"); /* U+00E9 folds to E */
    mh_cheat_profile_free(p);
    p = mh_cheat_profile_new();
    put_utf16("u16.txt", V5 "[Wide]\n8005E9CC 03E8\n", 1);
    CHECK(imp(p, "u16.txt", NULL, &n) && n == 1);
    CHECK_STR(p->entries[0].name, "WIDE");
    mh_cheat_profile_free(p);

    /* new cheats are added to what the profile already has */
    p = mh_cheat_profile_new();
    CHECK(mh_cheat_profile_add_custom(p, "OLD", one, 1, 0));
    CHECK(imp(p, "a.txt", NULL, &n) && n == 2 && p->count == 3);
    CHECK_STR(p->entries[0].name, "OLD");
    /* a failed import leaves the profile alone */
    CHECK(!imp(p, "c.txt", NULL, &n) && p->count == 3);
    mh_cheat_profile_free(p);

    /* errors */
    imp_err(V5 "#activation=ON\n[X]\n8005E9CC 03E8\n", NULL, "Metadados do DuckStation nao sao importados. Use [NOME] e codigos literais de hardware.");
    imp_err(V5 "[X]\n#type=cheat\n", NULL, "Metadados do DuckStation nao sao importados. Use [NOME] e codigos literais de hardware.");
    imp_err(V5 "#GROUP=a\n", NULL, "Metadados do DuckStation nao sao importados. Use [NOME] e codigos literais de hardware.");
    imp_err("", NULL, "Nao foi possivel ler o TXT/CHT.");
    imp_err("# only a comment\n", NULL, "O arquivo nao informa MHDialect.");
    imp_err("8005E9CC 03E8\n", NULL, "O arquivo deve comecar com MHDialect=V5ORIG_BANK_V01 ou GAMESHARK_CLASSIC_V1.");
    imp_err("[X]\n8005E9CC 03E8\n", NULL, "O arquivo deve comecar com MHDialect=V5ORIG_BANK_V01 ou GAMESHARK_CLASSIC_V1.");
    imp_err("MHDialect=NOPE\n", NULL, "MHDialect aceitos: V5ORIG_BANK_V01 ou GAMESHARK_CLASSIC_V1.");
    imp_err(V5 V5, NULL, "MHDialect deve aparecer uma unica vez, antes do primeiro cheat.");
    imp_err(V5 "[X]\n" V5, NULL, "MHDialect deve aparecer uma unica vez, antes do primeiro cheat.");
    imp_err(V5 "[X]\n8005E9CC 03E8\n" V5, NULL, "MHDialect deve aparecer uma unica vez, antes do primeiro cheat.");
    imp_err(V5, "MHDialect=V5ORIG_BANK_V01", "Nenhum cheat foi importado.");
    imp_err(V5, NULL, "Nenhum cheat foi importado.");
    imp_err(V5 "[X]\n[Y]\n", NULL, "Nenhum cheat foi importado."); /* names without codes */
    imp_err(V5 "[X\n8005E9CC 03E8\n", NULL, "Cabecalho [NOME] incompleto.");
    imp_err(V5 "[]\n8005E9CC 03E8\n", NULL, "Cabecalho [NOME] incompleto.");
    imp_err(V5 "[   ]\n8005E9CC 03E8\n", NULL, "Nome de cheat vazio.");
    imp_err(V5 "[!!!]\n8005E9CC 03E8\n", NULL, "Um bloco de codigos nao possui nome."); /* normalises to nothing */
    imp_err(V5 "[X]\nfoo bar\n", NULL, "Instrucao ou parametro nao reconhecido. Use [NOME] e linhas XXXXXXXX YYYY.");
    imp_err(V5 "[X]\nsome=value\n", NULL, "Existe metadata nao suportada no arquivo. Nenhum cheat foi importado parcialmente.");
    imp_err(V5 "Name=Value\n8005E9CC 03E8\n", NULL, "Existe metadata nao suportada no arquivo. Nenhum cheat foi importado parcialmente.");
    imp_err(V5 "Some Name\n8005E9CC 03E8\n80E9CC001 0000\n", NULL, "Instrucao ou parametro nao reconhecido. Use [NOME] e linhas XXXXXXXX YYYY.");
    imp_err(V5 "[X]\n8005E9CC 03E8\n8005E9D0 03E\n", NULL, "Instrucao ou parametro nao reconhecido. Use [NOME] e linhas XXXXXXXX YYYY.");
    imp_err(V5 "[X]\n8005E9CC 03E8 8005E9D0 03E8\n", NULL, "Instrucao ou parametro nao reconhecido. Use [NOME] e linhas XXXXXXXX YYYY.");
    imp_err(V5 "[X]\nD00A1234 0001\n", NULL, "V5ORIG_BANK_V01 aceita somente os tipos nativos 30/31/70/80/90 do runtime auditado.");
    imp_err(V5 "[X]\n8005E9CC 03E8\n[X2]\n7003C1FA F7FF\n", NULL, "V5ORIG_BANK_V01 aceita somente os tipos nativos 30/31/70/80/90 do runtime auditado.");

    /* file level problems */
    {
        char longline[700];
        memset(longline, 'x', 600);
        longline[600] = 0;
        imp_err(V5 "# long\n", NULL, "Nenhum cheat foi importado.");
        {
            MhWbuf w;
            mh_wbuf_init(&w);
            mh_wbuf_puts(&w, V5 "#");
            for (int i = 0; i < 510; i++) mh_wbuf_putc(&w, 'c');
            mh_wbuf_puts(&w, "\n[X]\n8005E9CC 03E8\n"); /* 511 characters: accepted */
            p = mh_cheat_profile_new();
            put_s("l1.txt", mh_wbuf_cstr(&w));
            CHECK(imp(p, "l1.txt", NULL, &n) && n == 1);
            mh_cheat_profile_free(p);
            mh_wbuf_free(&w);
            mh_wbuf_init(&w);
            mh_wbuf_puts(&w, V5 "#");
            for (int i = 0; i < 511; i++) mh_wbuf_putc(&w, 'c');
            mh_wbuf_puts(&w, "\n[X]\n8005E9CC 03E8\n"); /* 512: refused */
            imp_err(mh_wbuf_cstr(&w), NULL, "A importacao foi cancelada: existe uma linha maior que 511 caracteres.");
            mh_wbuf_free(&w);
        }
    }
    p = mh_cheat_profile_new();
    CHECK(!imp(p, "does-not-exist.txt", NULL, &n));
    CHECK_STR(err, "Nao foi possivel ler o TXT/CHT.");
    put("nul.txt", V5 "[X]\n8005E9CC 03E8\n\0", strlen(V5 "[X]\n8005E9CC 03E8\n") + 1);
    CHECK(!imp(p, "nul.txt", NULL, &n));
    CHECK_STR(err, "Nao foi possivel ler o TXT/CHT.");
    put("bad8.txt", V5 "[\xff]\n", strlen(V5 "[X]\n"));
    CHECK(!imp(p, "bad8.txt", NULL, &n));
    CHECK_STR(err, "Nao foi possivel ler o TXT/CHT.");
    put("bom.txt", "\xef\xbb\xbf", 3);
    CHECK(!imp(p, "bom.txt", NULL, &n));
    CHECK_STR(err, "Nao foi possivel ler o TXT/CHT.");
    put("odd16.txt", "\xff\xfeM\0H\0x", 7);
    CHECK(!imp(p, "odd16.txt", NULL, &n));
    CHECK_STR(err, "Nao foi possivel ler o TXT/CHT.");
    CHECK(!mh_cheat_profile_import_text(NULL, path_of("a.txt"), &n, err, sizeof err));
    CHECK_STR(err, "Nao foi possivel ler o TXT/CHT.");
    CHECK(mh_cheat_profile_import_text(p, path_of("a.txt"), &n, err, sizeof err) && n == 2);
    mh_cheat_profile_free(p);

    /* a profile that cannot take more: 99 fit, the import of 2 more is refused as a whole */
    p = mh_cheat_profile_new();
    for (int i = 0; i < 98; i++) CHECK(mh_cheat_profile_add_custom(p, "N", one, 1, 0));
    CHECK(!imp(p, "a.txt", NULL, &n));
    CHECK_STR(err, "A importacao excede os limites do perfil ou cria conflito entre codigos C0.");
    CHECK(p->count == 98 && n == 0);
    mh_cheat_profile_free(p);
}

static void test_export(void)
{
    MhCheatProfile *p = mh_cheat_profile_new(), *q = mh_cheat_profile_new();
    uint8_t cond[12] = {0x70, 0x80, 0x12, 0x34, 0x00, 0x01, 0x30, 0x80, 0x12, 0x38, 0x00, 0x09};
    uint8_t w8[6] = {0x30, 0x80, 0x12, 0x34, 0x00, 0x7f};
    uint8_t *raw = NULL;
    uint64_t rn = 0;
    int n = 0;
    CHECK(mh_cheat_profile_add_custom(p, "ONE", w8, 1, 0));
    CHECK(mh_cheat_profile_add_custom_disabled(p, "TWO WITH COND", cond, 2, 0));
    CHECK(mh_cheat_profile_export_text(p, path_of("x.cht"), err, sizeof err));
    CHECK(mh_read_entire_file(path_of("x.cht"), &raw, &rn));
    CHECK(raw[0] == 0xff && raw[1] == 0xfe);
    {
        char text[1024];
        size_t i;
        for (i = 0; 2 + i * 2 < rn; i++) {
            CHECK(raw[3 + i * 2] == 0);
            text[i] = (char)raw[2 + i * 2];
        }
        text[i] = 0;
        CHECK_STR(text,
                  "MHDialect=V5ORIG_BANK_V01\r\n[ONE]\r\n30801234 007F\r\n\r\n"
                  "[TWO WITH COND]\r\n70801234 0001\r\n30801238 0009\r\n\r\n");
    }
    free(raw);
    /* and back in */
    CHECK(imp(q, "x.cht", NULL, &n) && n == 2 && q->count == 2);
    CHECK_STR(q->entries[0].name, "ONE");
    CHECK(q->entries[1].nlines == 2 && memcmp(q->entries[1].lines, cond, 12) == 0);
    CHECK(mh_cheat_profile_export_text(NULL, path_of("empty.cht"), err, sizeof err));
    CHECK(mh_read_entire_file(path_of("empty.cht"), &raw, &rn) && rn == 2 + 27 * 2);
    free(raw);
    CHECK(!mh_cheat_profile_export_text(p, "/nonexistent-dir/x.cht", err, sizeof err));
    CHECK_STR(err, "Nao foi possivel exportar o perfil.");
    mh_cheat_profile_free(p);
    mh_cheat_profile_free(q);
}

int main(void)
{
    char tmpl[] = "/tmp/mh_cheat_test_XXXXXX";
    if (!mkdtemp(tmpl)) return 1;
    snprintf(g_dir, sizeof g_dir, "%s", tmpl);
    test_code_line();
    test_parse_v5();
    test_parse_classic();
    test_import();
    test_export();
    {
        char cmd[300];
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", g_dir);
        if (system(cmd) != 0) return 1;
    }
    printf("test_cheat_text: %d checks passed\n", g_checks);
    return 0;
}
