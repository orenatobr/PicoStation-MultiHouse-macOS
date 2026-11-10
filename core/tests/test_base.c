/* base module: strings, wbuf, paths, hashing, collector. Run with `make test-base`. */
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mh/base.h"

static int g_checks;
#define CHECK(c)                                                                       \
    do {                                                                               \
        g_checks++;                                                                    \
        if (!(c)) {                                                                    \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c);      \
            abort();                                                                   \
        }                                                                              \
    } while (0)
#define CHECK_STR(a, b)                                                                \
    do {                                                                               \
        const char *_a = (a), *_b = (b);                                               \
        g_checks++;                                                                    \
        if (strcmp(_a, _b) != 0) {                                                     \
            fprintf(stderr, "%s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, _a, _b);  \
            abort();                                                                   \
        }                                                                              \
    } while (0)

static void test_common(void)
{
    char buf[8];
    MhErr e;
    void *p = mh_alloc(0);
    CHECK(p != NULL);
    p = mh_realloc(p, 0);
    CHECK(p != NULL);
    mh_free(p);
    p = mh_realloc(NULL, 10);
    CHECK(p != NULL);
    mh_free(p);
    p = mh_calloc(16);
    CHECK(((uint8_t *)p)[15] == 0);
    mh_free(p);
    mh_free(NULL);

    CHECK(mh_u64_div(100, 7) == 14 && mh_u64_div(5, 0) == 0);
    CHECK(mh_u64_mod(100, 7) == 2 && mh_u64_mod(5, 0) == 0);
    CHECK(mh_u64_mul_u32(0x100000000ull, 3) == 0x300000000ull);

    CHECK_STR(mh_err_set(buf, sizeof buf, "n=%d", 12345678), "n=12345");
    mh_err_set(buf, 3, "\xC3\xA9\xC3\xA9"); /* two 2-byte chars into 3 bytes: only one fits */
    CHECK_STR(buf, "\xC3\xA9");
    mh_err_set(NULL, 0, "x"); /* must not crash */
    mh_err_fail_code(&e, "Failed", 5);
    CHECK(e.ok == 0 && e.code == 5);
    CHECK_STR(e.msg, "Failed (error 5)");
    mh_err_fail_code(&e, "Failed", 0);
    CHECK_STR(e.msg, "Failed");
    mh_err_clear(&e);
    CHECK(e.ok == 1 && e.msg[0] == 0);

    {
        MhProgress pr;
        int calls = 0;
        mh_progress_init(&pr, NULL, NULL);
        CHECK(mh_progress_report(&pr, 1, 2, "x"));
        CHECK(mh_progress_report(NULL, 1, 2, "x"));
        mh_progress_request_cancel(&pr);
        CHECK(mh_progress_cancelled(&pr) && !mh_progress_report(&pr, 1, 2, "x"));
        (void)calls;
    }
}

static void test_str(void)
{
    char b[32];
    CHECK(mh_strlen(NULL) == 0 && mh_strlen("abc") == 3);

    mh_str_copy(b, 4, "abcdef");
    CHECK_STR(b, "abc");
    mh_str_copy(b, 1, "abc");
    CHECK_STR(b, "");
    mh_str_copy(b, 3, "\xC3\xA9\xC3\xA9"); /* 'e acute' x2, room for 2 bytes: one char */
    CHECK_STR(b, "\xC3\xA9");
    mh_str_copy(b, 2, "\xC3\xA9"); /* does not fit at all */
    CHECK_STR(b, "");
    mh_str_copy(b, sizeof b, NULL);
    CHECK_STR(b, "");
    mh_str_copy(NULL, 5, "x");

    mh_str_copy(b, sizeof b, "ab");
    mh_str_cat(b, 5, "cdefg");
    CHECK_STR(b, "abcd");
    mh_str_cat(b, 5, "zz"); /* full: unchanged */
    CHECK_STR(b, "abcd");
    mh_str_cat(b, 3, "zz"); /* strlen >= n: unchanged */
    CHECK_STR(b, "abcd");

    CHECK(mh_str_cmp("a", "b") == -1 && mh_str_cmp("b", "a") == 1 && mh_str_cmp("a", "a") == 0);
    CHECK(mh_str_cmp("a", "A") == 1);
    CHECK(mh_str_cmp(NULL, "x") == -1 && mh_str_cmp(NULL, "") == 0 && mh_str_cmp("x", NULL) == 1);
    CHECK(mh_str_cmp("ab", "abc") == -1);
    CHECK(mh_str_icmp("Hello", "hELLO") == 0);
    CHECK(mh_str_icmp("a", "B") == -1 && mh_str_icmp("b", "A") == 1);
    CHECK(mh_str_icmp("abc", "ab") == 1 && mh_str_icmp(NULL, "A") == -1 && mh_str_icmp("A", NULL) == 1);
    CHECK(mh_str_icmp(NULL, NULL) == 0);
    /* '_' (0x5F) sorts after 'Z' but before 'a'; folding goes to lower case in icmp */
    CHECK(mh_str_icmp("_", "a") == -1 && mh_str_icmp("_", "A") == -1);

    CHECK(mh_str_starts_i("Disc 1", "dISC") && !mh_str_starts_i("Dis", "disc"));
    CHECK(mh_str_starts_i("abc", "") && !mh_str_starts_i(NULL, "a") && !mh_str_starts_i("a", NULL));
    CHECK(mh_str_ends_i("GAME.CUE", ".cue") && !mh_str_ends_i("cue", ".cue"));
    CHECK(mh_str_ends_i("x", "") && mh_str_ends_i("x", NULL) && mh_str_ends_i(NULL, ""));
    CHECK(!mh_str_ends_i(NULL, "a") && mh_str_ends_i("a.cue", "a.cue"));

    mh_str_copy(b, sizeof b, "aBc\xC3\xA9z");
    mh_str_upper(b);
    CHECK_STR(b, "ABC\xC3\xA9Z"); /* non-ASCII untouched */

    CHECK(mh_str_find_i("Final Fantasy", "FANT") == 6);
    CHECK(mh_str_find_i("abc", "abcd") == -1 && mh_str_find_i("abc", "") == -1);
    CHECK(mh_str_find_i("abc", "c") == 2 && mh_str_find_i("abc", "abc") == 0);
    CHECK(mh_str_find_i("aab", "ab") == 1 && mh_str_find_i(NULL, "a") == -1);

    {
        const char *s = "A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";
        CHECK(mh_utf8_next(&s) == 'A');
        CHECK(mh_utf8_next(&s) == 0xE9);
        CHECK(mh_utf8_next(&s) == 0x20AC);
        CHECK(mh_utf8_next(&s) == 0x1F600);
        CHECK(mh_utf8_next(&s) == 0 && *s == 0);
        s = "\xC3";
        CHECK(mh_utf8_next(&s) == 0xFFFD);
        s = "\xC0\x80";
        CHECK(mh_utf8_next(&s) == 0xFFFD && s[0] == (char)0x80);
        s = "\xED\xA0\x80"; /* surrogate */
        CHECK(mh_utf8_next(&s) == 0xFFFD);
    }

    mh_ascii_from_utf8("a\xC3\xA9z\xF0\x9F\x98\x80!", b, sizeof b);
    CHECK_STR(b, "a?z?" "?" "!"); /* the emoji is two UTF-16 units */
    mh_ascii_from_utf8("abcdef", b, 4);
    CHECK_STR(b, "abc");
    mh_ascii_from_utf8(NULL, b, 4);
    CHECK_STR(b, "");
    mh_utf8_from_latin1("a\xE9z", b, sizeof b);
    CHECK_STR(b, "a\xC3\xA9z");
    mh_utf8_from_latin1("a\xE9z", b, 3); /* room for 2 bytes: 'a' only, the pair needs 2 more */
    CHECK_STR(b, "a");

    {
        const uint8_t d[] = {0xAB, 0x01, 0xFF};
        mh_bytes_to_hex(d, 3, b, 7);
        CHECK_STR(b, "AB01FF");
        mh_bytes_to_hex(d, 3, b, 6);
        CHECK_STR(b, "AB01");
        mh_bytes_to_hex(d, 3, b, 2);
        CHECK_STR(b, "");
        mh_bytes_to_hex(d, 3, b, 3);
        CHECK_STR(b, "AB");
        mh_bytes_to_hex_lower(d, 3, b, 7);
        CHECK_STR(b, "ab01ff");
        {
            uint8_t o[3];
            CHECK(mh_hex_decode("aB01Ff", o, 3) && !memcmp(o, d, 3));
            CHECK(!mh_hex_decode("aB01F", o, 3) && !mh_hex_decode("aB01Fg", o, 3));
            CHECK(!mh_hex_decode("aB01FfAA", o, 3) && !mh_hex_decode(NULL, o, 3));
        }
    }

    mh_clean_ascii("  a   b \x01 c  ", b, sizeof b);
    CHECK_STR(b, "a b c");
    mh_clean_ascii("   ", b, sizeof b);
    CHECK_STR(b, "");
    mh_clean_ascii("ab\xC3\xA9" "cd", b, sizeof b); /* non-ASCII bytes are dropped */
    CHECK_STR(b, "abcd");
    mh_clean_ascii("a b c d e", b, 5);
    CHECK_STR(b, "a b");
    mh_clean_ascii("abcdef", b, 1);
    CHECK_STR(b, "");

    mh_normalize_title("  Final Fantasy VII (USA) (Disc 1)  ", b, sizeof b);
    CHECK_STR(b, "FINAL FANTASY VII USA DISC 1");
    mh_normalize_title("R\xC3\xA9-vo & Co.", b, sizeof b);
    CHECK_STR(b, "RE-VO & CO.");
    mh_normalize_title("\xC3\x87" "a va, l'\xC3\xA2" "me", b, sizeof b);
    CHECK_STR(b, "CA VA L'AME");
    mh_normalize_title("\xC3\x86on \xE2\x98\x85Star\xE2\x98\x85", b, sizeof b); /* AE -> separator */
    CHECK_STR(b, "ON STAR");
    mh_normalize_title("Ac/Dc___x", b, sizeof b);
    CHECK_STR(b, "AC DC X");
    mh_normalize_title("\xC3\x98" "x \xC3\xB7 y \xC3\xBF", b, sizeof b); /* O-slash and divide are separators, y-diaeresis -> Y */
    CHECK_STR(b, "X Y Y");
    mh_normalize_title("abcdefgh", b, 5);
    CHECK_STR(b, "ABCD");
    mh_normalize_title("ab cd ef", b, 6); /* cut right after a separator: it is trimmed */
    CHECK_STR(b, "AB CD");
    mh_normalize_title(NULL, b, 5);
    CHECK_STR(b, "");

    mh_str_copy(b, sizeof b, "slus_005.94");
    mh_normalize_serial(b);
    CHECK_STR(b, "SLUS-00594");
    mh_str_copy(b, sizeof b, "abcdefghijklmnopqrstuvwxyz0123");
    mh_normalize_serial(b);
    CHECK_STR(b, "ABCDEFGHIJKLMNO");
    mh_str_copy(b, sizeof b, " ..__ ");
    mh_normalize_serial(b);
    CHECK_STR(b, "--");

    mh_str_copy(b, sizeof b, "\t  hello world \r\n");
    mh_trim_line(b);
    CHECK_STR(b, "hello world");
    mh_str_copy(b, sizeof b, "a\x01");
    mh_trim_line(b);
    CHECK_STR(b, "a\x01");
    mh_str_copy(b, sizeof b, " \t ");
    mh_trim_line(b);
    CHECK_STR(b, "");
    mh_str_copy(b, sizeof b, "x");
    mh_trim_line(b);
    CHECK_STR(b, "x");
}

static void test_wbuf(void)
{
    MhWbuf w;
    size_t i;
    mh_wbuf_init(&w);
    CHECK_STR(mh_wbuf_cstr(&w), "");
    CHECK(mh_wbuf_puts(&w, NULL) && mh_wbuf_puts(&w, "") && w.len == 0);
    CHECK(mh_wbuf_puts(&w, "ab") && mh_wbuf_putc(&w, 'c') && mh_wbuf_u32(&w, 4294967295u));
    CHECK(mh_wbuf_putc(&w, ' ') && mh_wbuf_u64(&w, 0) && mh_wbuf_putc(&w, ' '));
    CHECK(mh_wbuf_u64(&w, 18446744073709551615ull) && mh_wbuf_putc(&w, ' ') && mh_wbuf_hex32(&w, 0xBEEF));
    CHECK_STR(mh_wbuf_cstr(&w), "abc4294967295 0 18446744073709551615 0000BEEF");
    CHECK(w.cap == 128 && w.len == strlen(w.data));
    for (i = 0; i < 40000; i++) CHECK(mh_wbuf_putc(&w, 'x'));
    CHECK(w.len == 40000 + 45 && w.data[w.len] == 0 && w.cap >= w.len + 1);
    CHECK(w.cap == 65536); /* 128 doubled up to 32768, then +32768 */
    mh_wbuf_free(&w);
    CHECK(w.data == NULL && w.len == 0 && w.cap == 0);

    {
        static const struct { uint64_t v; const char *s; } t[] = {
            {0, "0 B"}, {1023, "1023 B"}, {1024, "1.0 KB"}, {1536, "1.5 KB"},
            {1024 * 1024 - 1, "1023.9 KB"}, {5u << 20, "5.0 MB"}, {(1ull << 30) - 1, "1023.9 MB"},
            {1ull << 30, "1.0 GB"}, {3ull << 29, "1.5 GB"}, {UINT64_MAX, "17179869183.9 GB"}};
        for (i = 0; i < sizeof t / sizeof *t; i++) {
            mh_wbuf_init(&w);
            CHECK(mh_wbuf_size(&w, t[i].v));
            CHECK_STR(mh_wbuf_cstr(&w), t[i].s);
            mh_wbuf_free(&w);
        }
    }
    mh_wbuf_init(&w);
    CHECK(mh_wbuf_printf(&w, "%s-%d", "x", 7));
    {
        char big[600];
        memset(big, 'q', sizeof big - 1);
        big[sizeof big - 1] = 0;
        CHECK(mh_wbuf_printf(&w, "%s", big) && w.len == 3 + 599);
    }
    mh_wbuf_free(&w);
}

static void test_paths(void)
{
    char b[64];
    mh_path_basename("a/b/c.txt", b, sizeof b); CHECK_STR(b, "c.txt");
    mh_path_basename("c.txt", b, sizeof b); CHECK_STR(b, "c.txt");
    mh_path_basename("a\\b\\c", b, sizeof b); CHECK_STR(b, "c");
    mh_path_basename("dir/", b, sizeof b); CHECK_STR(b, "");
    mh_path_basename(NULL, b, sizeof b); CHECK_STR(b, "");
    mh_path_basename("a/bcdef", b, 4); CHECK_STR(b, "bcd");

    mh_path_dirname("a/b/c", b, sizeof b); CHECK_STR(b, "a/b");
    mh_path_dirname("c", b, sizeof b); CHECK_STR(b, ".");
    mh_path_dirname("/x", b, sizeof b); CHECK_STR(b, "");
    mh_path_dirname("a/b/", b, sizeof b); CHECK_STR(b, "a/b");
    mh_path_dirname("", b, sizeof b); CHECK_STR(b, ".");
    mh_path_dirname(NULL, b, sizeof b); CHECK_STR(b, ".");
    mh_path_dirname("a\\b", b, sizeof b); CHECK_STR(b, "a");
    mh_path_dirname("abc/def", b, 3); CHECK_STR(b, "ab");

    mh_path_join("a", "b", b, sizeof b); CHECK_STR(b, "a/b");
    mh_path_join("a/", "b", b, sizeof b); CHECK_STR(b, "a/b");
    mh_path_join("a", "/b", b, sizeof b); CHECK_STR(b, "a/b");
    mh_path_join("a", "\\/b", b, sizeof b); CHECK_STR(b, "a/b");
    mh_path_join("", "/b", b, sizeof b); CHECK_STR(b, "b");
    mh_path_join("a", "", b, sizeof b); CHECK_STR(b, "a/");
    mh_path_join("a/", "", b, sizeof b); CHECK_STR(b, "a/");
    mh_path_join("", "", b, sizeof b); CHECK_STR(b, "");
    mh_path_join("a\\", "b", b, sizeof b); CHECK_STR(b, "a\\b");
    mh_path_join("/", "b", b, sizeof b); CHECK_STR(b, "/b");
    mh_path_join(NULL, "b", b, sizeof b); CHECK_STR(b, "b");
    mh_path_join("abc", "defg", b, 6); CHECK_STR(b, "abc/d");
    mh_path_join("abcd", "ef", b, 5); CHECK_STR(b, "abcd");
    strcpy(b, "dir"); mh_path_join(b, "f", b, sizeof b); CHECK_STR(b, "dir/f"); /* a may alias out */

    mh_path_stem("x/y/z.tar.gz", b, sizeof b); CHECK_STR(b, "z.tar");
    mh_path_stem(".hidden", b, sizeof b); CHECK_STR(b, "");
    mh_path_stem("noext", b, sizeof b); CHECK_STR(b, "noext");
    mh_path_stem("a.d/noext", b, sizeof b); CHECK_STR(b, "noext");
    mh_path_stem("Game (USA).cue", b, sizeof b); CHECK_STR(b, "Game (USA)");
    mh_path_stem(NULL, b, sizeof b); CHECK_STR(b, "");

    mh_path_change_ext("a/b.cue", ".bin", b, sizeof b); CHECK_STR(b, "a/b.bin");
    mh_path_change_ext("a/b.cue", "bin", b, sizeof b); CHECK_STR(b, "a/b.bin");
    mh_path_change_ext("a.d/b", "bin", b, sizeof b); CHECK_STR(b, "a.d/b.bin");
    mh_path_change_ext("a/b.cue", NULL, b, sizeof b); CHECK_STR(b, "a/b");
    mh_path_change_ext("a/b.cue", "", b, sizeof b); CHECK_STR(b, "a/b");
    mh_path_change_ext(".cue", "bin", b, sizeof b); CHECK_STR(b, ".bin");
    mh_path_change_ext("a/b", "", b, sizeof b); CHECK_STR(b, "a/b");
    mh_path_change_ext("a/b.", "x", b, sizeof b); CHECK_STR(b, "a/b.x");
    mh_path_change_ext("a.b.c", ".d", b, sizeof b); CHECK_STR(b, "a.b.d");
    mh_path_change_ext(NULL, "x", b, sizeof b); CHECK_STR(b, ".x");
    mh_path_change_ext("abc.def", "ghijk", b, 8); CHECK_STR(b, "abc.ghi");

    CHECK(mh_path_full("/x/y/../z//./w/", b, sizeof b)); CHECK_STR(b, "/x/z/w");
    CHECK(mh_path_full("/..", b, sizeof b)); CHECK_STR(b, "/");
    CHECK(mh_path_full("/", b, sizeof b)); CHECK_STR(b, "/");
    CHECK(mh_path_full("/a/../../b", b, sizeof b)); CHECK_STR(b, "/b");
    CHECK(!mh_path_full("", b, sizeof b) && !mh_path_full(NULL, b, sizeof b));
    CHECK(!mh_path_full("/abcdef", b, 5));
    {
        char cwd[512], want[600];
        CHECK(getcwd(cwd, sizeof cwd) != NULL);
        CHECK(mh_path_full("rel/./x", b, sizeof b) || strlen(cwd) > 40);
        snprintf(want, sizeof want, "%s/rel/x", cwd);
        if (strlen(want) < sizeof b) CHECK_STR(b, want);
    }
    CHECK(mh_path_equal_full("/tmp/Foo/", "/tmp/foo"));
    CHECK(mh_path_equal_full("/tmp/a/../Foo", "/TMP/foo/."));
    CHECK(!mh_path_equal_full("/a/b", "/a/c"));
    CHECK(!mh_path_equal_full("", "x") && !mh_path_equal_full(NULL, "x") && !mh_path_equal_full("x", ""));

    mh_game_directory("/g/Final Fantasy VII/Disc 1/disc1.cue", b, sizeof b); CHECK_STR(b, "/g/Final Fantasy VII");
    mh_game_directory("/g/Game (USA)/Game.cue", b, sizeof b); CHECK_STR(b, "/g/Game (USA)");
    mh_game_directory("/g/Game/CD 2/x.cue", b, sizeof b); CHECK_STR(b, "/g/Game");
    mh_game_directory("/g/Game/CD2/x.cue", b, sizeof b); CHECK_STR(b, "/g/Game/CD2"); /* needs "CD " */
    mh_game_directory("/g/Game/Disco 3/x.cue", b, sizeof b); CHECK_STR(b, "/g/Game");
    mh_game_directory("/g/Game/CDROM/x.cue", b, sizeof b); CHECK_STR(b, "/g/Game/CDROM");
    mh_game_directory("/g/Game/My Disc 2/x.cue", b, sizeof b); CHECK_STR(b, "/g/Game/My Disc 2");
    mh_game_directory("/g/Game/(Disc 2)/x.cue", b, sizeof b); CHECK_STR(b, "/g/Game"); /* punctuation normalises away */
}

static void test_natural(void)
{
    CHECK(mh_import_natural_compare("a2", "a10") < 0);
    CHECK(mh_import_natural_compare("a10", "a2") > 0);
    CHECK(mh_import_natural_compare("a02", "a2") == 0);
    CHECK(mh_import_natural_compare("File 9.cue", "file 10.cue") < 0);
    CHECK(mh_import_natural_compare("A", "a") == 0);
    CHECK(mh_import_natural_compare("a", "b") < 0 && mh_import_natural_compare("b", "a") > 0);
    CHECK(mh_import_natural_compare("", "a") < 0 && mh_import_natural_compare("a", "") > 0);
    CHECK(mh_import_natural_compare("", "") == 0);
    CHECK(mh_import_natural_compare("x1", "x") > 0 && mh_import_natural_compare("x", "x1") < 0);
    CHECK(mh_import_natural_compare("abc", "ab") > 0);
    CHECK(mh_import_natural_compare("01", "1") == 0 && mh_import_natural_compare("00", "0") == 0);
    CHECK(mh_import_natural_compare("0a", "1") < 0);
    CHECK(mh_import_natural_compare("1", "A") < 0);          /* digit vs letter: raw '1' < 'A' */
    CHECK(mh_import_natural_compare("-", "1") < 0);          /* '-' (0x2D) < '1' */
    CHECK(mh_import_natural_compare("_", "z") > 0);          /* folded to upper: '_' (0x5F) > 'Z' (0x5A) */
    CHECK(mh_import_natural_compare("12345678901234567890", "12345678901234567891") < 0);
    CHECK(mh_import_natural_compare("9", "10") < 0 && mh_import_natural_compare("10", "9") > 0);
    CHECK(mh_import_natural_compare("a1b2", "a1b10") < 0);
    CHECK(mh_import_natural_compare("a1b", "a01c") < 0);
    CHECK(mh_import_natural_compare(NULL, "a") < 0);
}

static void test_hash(void)
{
    static const struct { size_t n; const char *sha1, *sha256; uint32_t crc; } v[] = {
#include "base_hash_vectors.inc"
    };
    uint8_t d[1000], o1[20], o2[32];
    char hex[80];
    size_t i, j;
    MhSha1 s1;
    MhSha256 s2;

    CHECK(mh_crc32("123456789", 9) == 0xCBF43926u);
    CHECK(mh_crc32("", 0) == 0);
    {
        uint32_t c = mh_crc32_begin();
        c = mh_crc32_update(c, "1234", 4);
        c = mh_crc32_update(c, "56789", 5);
        CHECK(mh_crc32_end(c) == 0xCBF43926u);
    }
    {
        uint8_t rec[13];
        memcpy(rec, "123456789", 9);
        CHECK(mh_crc_record(rec, 13) == 0xCBF43926u);
        CHECK(mh_crc_record(rec, 3) == 0 && mh_crc_record(rec, 4) == 0);
    }
    for (i = 0; i < 1000; i++) d[i] = (uint8_t)(i * 7 + 3);
    for (i = 0; i < sizeof v / sizeof *v; i++) {
        mh_sha1(d, v[i].n, o1);
        mh_bytes_to_hex_lower(o1, 20, hex, sizeof hex);
        CHECK_STR(hex, v[i].sha1);
        mh_sha256(d, v[i].n, o2);
        mh_bytes_to_hex_lower(o2, 32, hex, sizeof hex);
        CHECK_STR(hex, v[i].sha256);
        CHECK(mh_crc32(d, v[i].n) == v[i].crc);
        /* incremental with awkward split sizes must equal one-shot */
        for (j = 1; j < 130; j += 13) {
            size_t pos = 0;
            uint8_t a1[20], a2[32];
            mh_sha1_init(&s1);
            mh_sha256_init(&s2);
            while (pos < v[i].n) {
                size_t k = v[i].n - pos < j ? v[i].n - pos : j;
                mh_sha1_update(&s1, d + pos, k);
                mh_sha256_update(&s2, d + pos, k);
                pos += k;
            }
            mh_sha1_final(&s1, a1);
            mh_sha256_final(&s2, a2);
            CHECK(!memcmp(a1, o1, 20) && !memcmp(a2, o2, 32));
        }
    }
    mh_sha1("abc", 3, o1);
    mh_bytes_to_hex_lower(o1, 20, hex, sizeof hex);
    CHECK_STR(hex, "a9993e364706816aba3e25717850c26c9cd0d89d");
    mh_sha256("abc", 3, o2);
    mh_bytes_to_hex_lower(o2, 32, hex, sizeof hex);
    CHECK_STR(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    {
        const char *m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        mh_sha1(m, strlen(m), o1);
        mh_bytes_to_hex_lower(o1, 20, hex, sizeof hex);
        CHECK_STR(hex, "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
        mh_sha256(m, strlen(m), o2);
        mh_bytes_to_hex_lower(o2, 32, hex, sizeof hex);
        CHECK_STR(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    }
    {
        uint8_t *mil = malloc(1000000);
        memset(mil, 'a', 1000000);
        mh_sha1(mil, 1000000, o1);
        mh_bytes_to_hex_lower(o1, 20, hex, sizeof hex);
        CHECK_STR(hex, "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
        mh_sha256(mil, 1000000, o2);
        mh_bytes_to_hex_lower(o2, 32, hex, sizeof hex);
        CHECK_STR(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
        free(mil);
    }
    {   /* the CRC table is the plain bitwise algorithm */
        uint32_t c = mh_crc32_begin(), r;
        uint8_t b = 0x5A;
        int k;
        r = c ^ b;
        for (k = 0; k < 8; k++) r = (r >> 1) ^ (r & 1 ? 0xEDB88320u : 0);
        CHECK(mh_crc32_update(c, &b, 1) == r);
    }
}

/* The fast CRC-32 and SHA-1 paths (hardware instructions on Apple Silicon) must agree with the portable
 * reference on data of every alignment and length, including the odd tails. */
static void test_hash_fast_paths(void)
{
    enum { N = 1 << 20 };
    uint8_t *buf = malloc(N + 64);
    uint32_t x = 12345u, h1[5], h2[5];
    size_t i, off, len;
    MhSha1 a, b;
    uint8_t da[20], db[20];

    CHECK(buf != NULL);
    for (i = 0; i < N + 64; i++) { /* xorshift filler */
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        buf[i] = (uint8_t)x;
    }
    for (off = 0; off < 9; off++) {
        static const size_t lens[] = {0, 1, 7, 8, 9, 63, 64, 65, 4095, 4096, 100003, N - 8};
        for (i = 0; i < sizeof lens / sizeof lens[0]; i++) {
            len = lens[i];
            CHECK(mh_crc32_update(0xFFFFFFFFu, buf + off, len) == mh_crc32_update_portable(0xFFFFFFFFu, buf + off, len));
        }
    }
    {   /* a CRC carried across two calls */
        uint32_t c = mh_crc32_update(mh_crc32_begin(), buf + 3, 12345);
        c = mh_crc32_update(c, buf + 12348, 777);
        CHECK(c == mh_crc32_update_portable(mh_crc32_update_portable(mh_crc32_begin(), buf + 3, 12345), buf + 12348, 777));
    }
    for (off = 0; off < 5; off++) { /* multi block SHA-1 against block-by-block */
        memcpy(h1, "\x01\x23\x45\x67\x89\xab\xcd\xef\xfe\xdc\xba\x98\x76\x54\x32\x10\xf0\xe1\xd2\xc3", 20);
        memcpy(h2, h1, 20);
        mh_sha1_blocks(h1, buf + off, 1000);
        for (i = 0; i < 1000; i++) mh_sha1_block(h2, buf + off + 64 * i);
        CHECK(memcmp(h1, h2, 20) == 0);
    }
    /* the streaming update with awkward chunk sizes against one call */
    mh_sha1_init(&a);
    mh_sha1_update(&a, buf, N);
    mh_sha1_final(&a, da);
    mh_sha1_init(&b);
    for (off = 0, i = 1; off < N; i = i * 3 % 1021 + 1) {
        len = i < N - off ? i : N - off;
        mh_sha1_update(&b, buf + off, len);
        off += len;
    }
    mh_sha1_final(&b, db);
    CHECK(memcmp(da, db, 20) == 0);
    free(buf);
}

/* End-to-end check of SHA-256 and CRC-32 against the original: the cheat database embedded in
 * the exe is an LZ4 block; its SHA-256 (kExpectedSha256 @0x140f1d080) and CRC-32 (the compare
 * against 0xA06CFC31 in cheat_db_load) are constants of the original program. */
static void test_embedded_cheats(void)
{
    static const uint8_t want[32] = {
        0x1b, 0x48, 0x39, 0x1f, 0x53, 0x00, 0x1a, 0x0b, 0x95, 0xb8, 0x13, 0xa9, 0x73, 0x9e, 0x65, 0x75,
        0x00, 0x7e, 0x8b, 0xa7, 0xf2, 0xcc, 0x73, 0x59, 0xfb, 0x26, 0x19, 0x6a, 0x42, 0x3c, 0xf1, 0xa3};
    uint8_t *in, *out, h[32];
    uint64_t n;
    size_t i = 12, o = 0;
    const size_t outsize = 0x34d836;
    if (!mh_read_entire_file("resources/embedded/cdx_cheats.bin", &in, &n)) {
        printf("skip: resources/embedded/cdx_cheats.bin not found\n");
        return;
    }
    CHECK(n > 12 && !memcmp(in, "CDXLZ1\0\0", 8));
    out = malloc(outsize);
    while (i < n) {
        unsigned t = in[i++], ll = t >> 4, ml = t & 15, off;
        if (ll == 15) { unsigned b; do { b = in[i++]; ll += b; } while (b == 255); }
        CHECK(o + ll <= outsize && i + ll <= n);
        memcpy(out + o, in + i, ll);
        o += ll;
        i += ll;
        if (i >= n) break;
        off = in[i] | in[i + 1] << 8;
        i += 2;
        if (ml == 15) { unsigned b; do { b = in[i++]; ml += b; } while (b == 255); }
        ml += 4;
        CHECK(off && off <= o && o + ml <= outsize);
        while (ml--) { out[o] = out[o - off]; o++; }
    }
    CHECK(o == outsize);
    mh_sha256(out, outsize, h);
    CHECK(!memcmp(h, want, 32));
    CHECK(mh_crc32(out, outsize) == 0xA06CFC31u);
    free(out);
    free(in);
}

/* ---- collector ---------------------------------------------------------------------------- */

static void touch(const char *dir, const char *rel)
{
    char p[1024], *s;
    FILE *f;
    snprintf(p, sizeof p, "%s/%s", dir, rel);
    for (s = p + strlen(dir) + 1; *s; s++)
        if (*s == '/') { *s = 0; mkdir(p, 0777); *s = '/'; }
    f = fopen(p, "w");
    CHECK(f != NULL);
    fputs("x", f);
    fclose(f);
}

static void test_collect(void)
{
    char root[] = "/tmp/mh_base_collect_XXXXXX", p[1100];
    MhCollect *c = mh_collect_new();
    CHECK(c != NULL);
    CHECK(mkdtemp(root) != NULL);
    {
        touch(root, "g/Game 10/disc.cue");
        touch(root, "g/Game 2/Disc 2/b.CUE");
        touch(root, "g/Game 2/Disc 1/a.cue");
        touch(root, "g/Game 2/Disc 1/a.bin");
        touch(root, "g/Game 2/._a.cue");
        touch(root, "g/readme.txt");
        touch(root, "solo.cue");
        snprintf(p, sizeof p, "%s/link.cue", root);
        CHECK(symlink("solo.cue", p) == 0);

        snprintf(p, sizeof p, "%s/g", root);
        CHECK(mh_import_collect(c, p));
        CHECK(c->count == 3 && c->duplicates == 0 && c->ignored == 0 && !c->failed);
        /* walk order: names sorted ignoring case: "Game 10", "Game 2", "readme.txt" */
        {
            char want[1100];
            snprintf(want, sizeof want, "%s/g/Game 10/disc.cue", root);
            CHECK_STR(c->paths[0], want);
            snprintf(want, sizeof want, "%s/g/Game 2/Disc 1/a.cue", root);
            CHECK_STR(c->paths[1], want);
            snprintf(want, sizeof want, "%s/g/Game 2/Disc 2/b.CUE", root);
            CHECK_STR(c->paths[2], want);
        }
        mh_import_sort(c);   /* natural order puts "Game 2" before "Game 10" */
        CHECK(strstr(c->paths[0], "Game 2/Disc 1") && strstr(c->paths[1], "Game 2/Disc 2") &&
              strstr(c->paths[2], "Game 10"));

        /* direct drops: duplicate, plain file, symlink */
        snprintf(p, sizeof p, "%s/g/Game 10/DISC.CUE", root);
        CHECK(mh_import_collect(c, p) && c->duplicates == 1 && c->count == 3);
        snprintf(p, sizeof p, "%s/g/readme.txt", root);
        CHECK(mh_import_collect(c, p) && c->ignored == 1);
        snprintf(p, sizeof p, "%s/link.cue", root);
        CHECK(mh_import_collect(c, p) && c->ignored == 2 && c->count == 3);
        snprintf(p, sizeof p, "%s/solo.cue", root);
        CHECK(mh_import_collect(c, p) && c->count == 4);

        /* error: missing path */
        snprintf(p, sizeof p, "%s/nope", root);
        CHECK(!mh_import_collect(c, p) && c->failed);
        {
            char want[1300];
            snprintf(want, sizeof want, "Could not access:\r\n%s", p);
            CHECK_STR(c->error, want);
        }
        CHECK(!mh_import_collect(c, root)); /* once failed, everything is refused */
    }
    mh_collect_free(c);

    {   /* NTFS lists names in upper case order: '_' (0x5F) sorts after letters, not before */
        char want[1300];
        c = mh_collect_new();
        touch(root, "ord/a_b.cue");
        touch(root, "ord/ab.cue");
        touch(root, "ord/aB2.cue");
        snprintf(p, sizeof p, "%s/ord", root);
        CHECK(mh_import_collect(c, p) && c->count == 3);
        snprintf(want, sizeof want, "%s/ord/ab.cue", root);
        CHECK_STR(c->paths[0], want);
        snprintf(want, sizeof want, "%s/ord/aB2.cue", root);
        CHECK_STR(c->paths[1], want);
        snprintf(want, sizeof want, "%s/ord/a_b.cue", root);
        CHECK_STR(c->paths[2], want);
        mh_collect_free(c);
    }
    {   /* more than 256 CUEs */
        int i;
        char name[64];
        c = mh_collect_new();
        for (i = 0; i < 257; i++) {
            snprintf(name, sizeof name, "many/c%03d.cue", i);
            touch(root, name);
        }
        snprintf(p, sizeof p, "%s/many", root);
        CHECK(!mh_import_collect(c, p) && c->failed && c->count == 256);
        CHECK(strncmp(c->error, "The batch exceeds 256 CUEs. Split the import into smaller batches.\r\n", 68) == 0);
        mh_collect_free(c);
    }
    {   /* depth: 33 nested folders (limit is 32 levels below the dropped folder) */
        int i;
        char deep[1000];
        c = mh_collect_new();
        snprintf(deep, sizeof deep, "%s/deep", root);
        mkdir(deep, 0777);
        for (i = 0; i < 33; i++) {
            strcat(deep, "/d");
            mkdir(deep, 0777);
        }
        snprintf(p, sizeof p, "%s/deep", root);
        CHECK(!mh_import_collect(c, p));
        CHECK(strncmp(c->error, "Subfolder limit exceeded:\r\n", 27) == 0);
        mh_collect_free(c);
    }
    {   /* a folder exactly 32 levels deep is accepted */
        int i;
        char deep[1000];
        c = mh_collect_new();
        snprintf(deep, sizeof deep, "%s/ok", root);
        mkdir(deep, 0777);
        for (i = 0; i < 31; i++) {
            strcat(deep, "/d");
            mkdir(deep, 0777);
        }
        strcat(deep, "/x.cue");
        {
            FILE *f = fopen(deep, "w");
            CHECK(f);
            fclose(f);
        }
        snprintf(p, sizeof p, "%s/ok", root);
        CHECK(mh_import_collect(c, p) && c->count == 1);
        mh_collect_free(c);
    }
    {   /* a path that is too long */
        char longp[1200];
        c = mh_collect_new();
        snprintf(longp, sizeof longp, "%s/", root);
        memset(longp + strlen(longp), 'a', 1010);
        longp[strlen(root) + 1 + 1010] = 0;
        CHECK(!mh_import_collect(c, longp));
        CHECK(c->failed);
        mh_collect_free(c);
    }
    snprintf(p, sizeof p, "rm -rf '%s'", root);
    CHECK(system(p) == 0);
}

int main(void)
{
    test_common();
    test_str();
    test_wbuf();
    test_paths();
    test_natural();
    test_hash();
    test_hash_fast_paths();
    test_embedded_cheats();
    test_collect();
    printf("test_base: %d checks passed\n", g_checks);
    return 0;
}
