/* cheat module: profile rules, limits, error texts, PSTCHT1 round trips. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#define NONE 0xFFFFFFFFu
#define CDX_PATH "resources/embedded/cdx_cheats.bin"
#define ERR_SYNTAX "A cheat has an unsupported name, syntax or code format."

static uint8_t *g_blob;
static uint64_t g_blob_len;
static MhCheatDb g_db;
static char err[512];

static const uint8_t W8[6] = {0x30, 0x80, 0x12, 0x34, 0x00, 0x7f};  /* write8 */
static const uint8_t W16[6] = {0x80, 0x80, 0x12, 0x36, 0x03, 0xe8}; /* write16 */
static const uint8_t COND[12] = {0x70, 0x80, 0x12, 0x34, 0x00, 0x01, 0x30, 0x80, 0x12, 0x38, 0x00, 0x09};

/* a hand built profile of n custom entries "CHEAT" with one write8 line (bypasses add_*, so
 * every validate rule can be hit) */
static MhCheatProfile *manual(unsigned n)
{
    MhCheatProfile *p = mh_cheat_profile_new();
    p->entries = calloc(n ? n : 1, sizeof(MhCheatEntry));
    p->cap = n;
    p->count = n;
    for (unsigned i = 0; i < n; i++) {
        MhCheatEntry *e = &p->entries[i];
        snprintf(e->name, sizeof e->name, "CHEAT");
        e->flags = MH_CHEAT_F_CUSTOM | MH_CHEAT_F_ENABLED;
        e->variant = e->id = NONE;
        e->nlines = 1;
        e->lines = malloc(6);
        memcpy(e->lines, W8, 6);
    }
    return p;
}

static void check_invalid(MhCheatProfile *p, const char *msg)
{
    char e[256];
    CHECK(!mh_cheat_profile_validate(p, e, sizeof e));
    CHECK_STR(e, msg);
    mh_cheat_profile_free(p);
}

static void test_validate_messages(void)
{
    MhCheatProfile *p;
    MhCheatEntry *e;

    CHECK(mh_cheat_profile_validate(NULL, err, sizeof err) && err[0] == 0);
    p = mh_cheat_profile_new();
    CHECK(mh_cheat_profile_validate(p, err, sizeof err));
    CHECK(mh_cheat_profile_validate_hardware(p, err, sizeof err));
    mh_cheat_profile_free(p);
    p = manual(1);
    CHECK(mh_cheat_profile_validate(p, err, sizeof err));
    mh_cheat_profile_free(p);

    p = mh_cheat_profile_new();
    p->count = 257;
    CHECK(!mh_cheat_profile_validate(p, err, sizeof err));
    CHECK_STR(err, "The catalog exceeds 256 cheats.");
    p->count = 0; /* no entry array: nothing to free */
    mh_cheat_profile_free(p);
    p = mh_cheat_profile_new();
    p->count = 1; /* entries == NULL */
    CHECK(!mh_cheat_profile_validate(p, err, sizeof err));
    CHECK_STR(err, ERR_SYNTAX);
    p->count = 0;
    mh_cheat_profile_free(p);

    for (int k = 0; k < 8; k++) {
        p = manual(1);
        e = &p->entries[0];
        switch (k) {
        case 0: e->name[0] = 0; break;
        case 1: strcpy(e->name, "A[B"); break;
        case 2: strcpy(e->name, "A]B"); break;
        case 3: strcpy(e->name, "caf\xc3\xa9"); break;
        case 4: memset(e->name, 'A', 64); break;      /* 64 characters, no terminator */
        case 5: e->nlines = 0; break;
        case 6: e->nlines = 256; break;
        case 7: e->lines[0] = 0x61; break;            /* not a native opcode */
        }
        check_invalid(p, ERR_SYNTAX);
    }
    p = manual(1);
    free(p->entries[0].lines);
    p->entries[0].lines = NULL;
    check_invalid(p, ERR_SYNTAX);
    p = manual(1); /* a prefix (70) with nothing after it */
    p->entries[0].lines[0] = 0x70;
    check_invalid(p, ERR_SYNTAX);

    p = manual(1);
    memset(p->entries[0].name, 'A', 63); /* 63 characters is the maximum */
    p->entries[0].name[63] = 0;
    CHECK(mh_cheat_profile_validate(p, err, sizeof err));
    mh_cheat_profile_free(p);

    p = manual(1);
    p->entries[0].flags |= 0x80;
    check_invalid(p, "A cheat has unknown flags.");
    p = manual(1);
    p->entries[0].flags |= MH_CHEAT_F_AUDITED;
    check_invalid(p, "Each cheat must be exactly AUDITED xor CUSTOM.");
    p = manual(1);
    p->entries[0].flags = 0;
    check_invalid(p, "Each cheat must be exactly AUDITED xor CUSTOM.");
    p = manual(1);
    p->entries[0].flags |= MH_CHEAT_F_MANDATORY;
    check_invalid(p, "A mandatory cheat must be audited and stay enabled.");
    p = manual(1);
    p->entries[0].flags |= MH_CHEAT_F_DB_DEFAULT;
    check_invalid(p, "A custom cheat has invalid official provenance or flags.");
    p = manual(1);
    p->entries[0].id = 5;
    check_invalid(p, "A custom cheat has invalid official provenance or flags.");
    p = manual(1);
    p->entries[0].game = 1;
    check_invalid(p, "A custom cheat has invalid official provenance or flags.");
    p = manual(1);
    p->entries[0].variant = 3;
    check_invalid(p, "A custom cheat has invalid official provenance or flags.");

    /* audited provenance against the profile header */
    p = manual(1);
    e = &p->entries[0];
    e->flags = MH_CHEAT_F_AUDITED;
    e->game = 1; e->variant = 3; e->id = 7;
    p->game = 1; p->title = 3; p->variant = 3; p->db_crc = MH_CHEAT_DB_CRC;
    CHECK(mh_cheat_profile_validate(p, err, sizeof err));
    e->game = 4;
    check_invalid(p, "An audited cheat lost or differs from the header provenance.");
    p = manual(1);
    e = &p->entries[0];
    e->flags = MH_CHEAT_F_AUDITED; e->game = 1; e->variant = 3; e->id = 7;
    p->game = 1; p->title = 3; p->variant = 4; p->db_crc = MH_CHEAT_DB_CRC;
    check_invalid(p, "An audited cheat lost or differs from the header provenance.");
    p = manual(1);
    e = &p->entries[0];
    e->flags = MH_CHEAT_F_AUDITED; e->game = 1; e->variant = 3; e->id = NONE;
    p->game = 1; p->title = 3; p->variant = 3; p->db_crc = MH_CHEAT_DB_CRC;
    check_invalid(p, "An audited cheat lost or differs from the header provenance.");
    p = manual(1);
    e = &p->entries[0];
    e->flags = MH_CHEAT_F_AUDITED; e->game = 1; e->variant = 3; e->id = 7;
    p->game = 1; p->title = 3; p->variant = 3; p->db_crc = 12345;
    check_invalid(p, "The audited profile has an invalid provenance header.");
    p = manual(1);
    e = &p->entries[0];
    e->flags = MH_CHEAT_F_AUDITED | MH_CHEAT_F_MANDATORY | MH_CHEAT_F_ENABLED;
    e->game = 1; e->variant = 3; e->id = 7;
    p->game = 1; p->title = 3; p->variant = 3; p->db_crc = MH_CHEAT_DB_CRC;
    CHECK(mh_cheat_profile_validate(p, err, sizeof err));
    mh_cheat_profile_free(p);

    /* a custom only profile needs the sentinel header */
    p = manual(1);
    p->db_crc = 1;
    check_invalid(p, "A CUSTOM-only profile must use the sentinel provenance and a zero CRC.");
    p = manual(1);
    p->game = 1;
    check_invalid(p, "A CUSTOM-only profile must use the sentinel provenance and a zero CRC.");
    p = manual(1);
    p->title = 0;
    check_invalid(p, "A CUSTOM-only profile must use the sentinel provenance and a zero CRC.");

    /* line and size limits */
    p = manual(3);
    for (int i = 0; i < 3; i++) {
        free(p->entries[i].lines);
        p->entries[i].lines = malloc(200 * 6);
        for (int k = 0; k < 200; k++) memcpy(p->entries[i].lines + k * 6, W8, 6);
        p->entries[i].nlines = 200;
    }
    check_invalid(p, "The catalog exceeds 510 code lines.");
    p = manual(100); /* 100 entries of 1 line: 64 + 100 * 102 = 10264 > 10240 */
    CHECK(mh_cheat_profile_serialized_size(p) == 0x40 + 100 * (0x60 + 6));
    check_invalid(p, "The cheat catalog exceeds the reserved 10,240 byte region.");
    p = manual(99);
    CHECK(mh_cheat_profile_validate(p, err, sizeof err));
    mh_cheat_profile_free(p);
}

static void test_add_remove_clone(void)
{
    MhCheatProfile *p = mh_cheat_profile_new(), *c;
    uint8_t lines[256 * 6];
    for (int i = 0; i < 256; i++) memcpy(lines + i * 6, W8, 6);

    CHECK(p->title == NONE && p->variant == NONE && p->game == 0 && p->count == 0 && p->state == 0);
    CHECK(!mh_cheat_profile_add_custom(p, "", W8, 1, 0));
    CHECK(!mh_cheat_profile_add_custom(p, "X", NULL, 1, 0));
    CHECK(!mh_cheat_profile_add_custom(p, "X", W8, 0, 0));
    CHECK(!mh_cheat_profile_add_custom(p, "X", lines, 256, 0));
    CHECK(!mh_cheat_profile_add_custom(p, "X[", W8, 1, 0));
    CHECK(!mh_cheat_profile_add_custom(NULL, "X", W8, 1, 0));
    CHECK(p->count == 0);
    CHECK(mh_cheat_profile_add_custom(p, "WRITE", W8, 1, 0));
    CHECK(p->count == 1 && p->entries[0].flags == 0x05 && p->entries[0].game == 0);
    CHECK(p->entries[0].variant == NONE && p->entries[0].id == NONE);
    CHECK(mh_cheat_profile_add_custom_disabled(p, "OFF", W16, 1, 0x40 | 0x01));
    CHECK(p->entries[1].flags == 0x44); /* bit 0 dropped, bit 6 kept */
    CHECK(mh_cheat_profile_add_custom(p, "BITS", W16, 1, 0x20));
    CHECK(p->entries[2].flags == 0x25);
    CHECK(!mh_cheat_profile_add_custom(p, "BAD", W16, 1, 0x80)); /* bit 7 is refused by validate */
    CHECK(p->count == 3);
    CHECK(mh_cheat_profile_enabled_count(p) == 2 && mh_cheat_profile_total_lines(p) == 3);
    CHECK(mh_cheat_profile_enabled_count(NULL) == 0 && mh_cheat_profile_total_lines(NULL) == 0);
    CHECK(mh_cheat_profile_active_c0_count(p) == 0);
    CHECK(mh_cheat_profile_find_audited(p, 0, 0) == -1);

    /* clone is deep and equal */
    c = mh_cheat_profile_clone(p);
    CHECK(c && c != p && c->count == 3 && c->entries != p->entries && c->entries[1].lines != p->entries[1].lines);
    CHECK(memcmp(c->entries[1].lines, p->entries[1].lines, 6) == 0 && c->entries[1].flags == 0x44);
    CHECK_STR(c->entries[2].name, "BITS");
    mh_cheat_profile_free(c);
    c = mh_cheat_profile_clone(NULL);
    CHECK(c && c->count == 0 && c->title == NONE);
    mh_cheat_profile_free(c);

    /* hardware serial */
    CHECK(mh_cheat_profile_prepare_hardware(p, "SLUS-00594") == 0);
    CHECK_STR(p->serial, "SLUS-00594");
    mh_cheat_profile_prepare_hardware(p, "ABCDEFGHIJKLMNOPQRSTU");
    CHECK_STR(p->serial, "ABCDEFGHIJKLMNO");
    CHECK(mh_cheat_profile_prepare_hardware(p, NULL) == 0 && mh_cheat_profile_prepare_hardware(NULL, "X") == 0);

    /* remove */
    CHECK(!mh_cheat_profile_remove(p, 3) && !mh_cheat_profile_remove(NULL, 0));
    CHECK(mh_cheat_profile_remove(p, 0) && p->count == 2);
    CHECK_STR(p->entries[0].name, "OFF");
    CHECK_STR(p->entries[1].name, "BITS");
    mh_cheat_profile_free(p);
    mh_cheat_profile_free(NULL);

    /* limits: two entries of 255 lines make 510, the next line does not fit */
    p = mh_cheat_profile_new();
    CHECK(mh_cheat_profile_add_custom(p, "A", lines, 255, 0));
    CHECK(mh_cheat_profile_add_custom(p, "B", lines, 255, 0));
    CHECK(mh_cheat_profile_total_lines(p) == 510);
    CHECK(!mh_cheat_profile_add_custom(p, "C", lines, 1, 0));
    CHECK(p->count == 2);
    mh_cheat_profile_free(p);
    /* 99 one line entries fit in 10240 bytes, the 100th does not */
    p = mh_cheat_profile_new();
    for (int i = 0; i < 99; i++) CHECK(mh_cheat_profile_add_custom(p, "N", W8, 1, 0));
    CHECK(!mh_cheat_profile_add_custom(p, "N", W8, 1, 0));
    CHECK(p->count == 99 && mh_cheat_profile_serialized_size(p) == 0x40 + 99 * 102);
    mh_cheat_profile_free(p);

    CHECK_STR(mh_cheat_entry_hardware_note(),
              "Use the correct game region and revision. See the PT/EN notes and the original code in "
              "the catalog. Button conditions need the controller to be initialized by the game.");
    CHECK(mh_cheat_entry_hardware_exclusion(NULL) == 0);
}

static int entry_equal(const MhCheatEntry *a, const MhCheatEntry *b)
{
    if (strcmp(a->name, b->name) || a->nlines != b->nlines || a->game != b->game || a->variant != b->variant ||
        a->id != b->id || memcmp(a->lines, b->lines, (size_t)a->nlines * 6))
        return 0;
    if (a->flags & MH_CHEAT_F_CUSTOM) return (a->flags & 1) == (b->flags & 1) && (b->flags & MH_CHEAT_F_CUSTOM);
    return a->flags == b->flags;
}

/* equal except the documented custom flag normalisation (bits 5 and 6 are transport markers) */
static int profile_equal(const MhCheatProfile *a, const MhCheatProfile *b)
{
    if (!a || !b) return a == b;
    if (a->title != b->title || a->variant != b->variant || a->game != b->game || a->db_crc != b->db_crc ||
        a->count != b->count)
        return 0;
    for (uint32_t i = 0; i < a->count; i++)
        if (!entry_equal(&a->entries[i], &b->entries[i])) return 0;
    return 1;
}

static void rd_fix_crc(uint8_t *b, uint32_t len)
{
    static const uint8_t z[4];
    uint32_t c = mh_crc32_begin();
    c = mh_crc32_update(c, b, 0x3c);
    c = mh_crc32_update(c, z, 4);
    if (len > 0x40) c = mh_crc32_update(c, b + 0x40, len - 0x40);
    ch_wr32(b + 0x3c, mh_crc32_end(c));
}

static void test_wire_roundtrip_and_validation(void)
{
    MhCheatProfile *p = mh_cheat_profile_new(), *q = NULL;
    uint8_t *blob = NULL, *bad;
    uint32_t len = 0;
    uint8_t two[12];
    memcpy(two, COND, 12);

    /* empty profiles serialise to nothing */
    CHECK(mh_cheat_profile_serialize(p, 2, &blob, &len, err, sizeof err) && blob == NULL && len == 0);
    CHECK(mh_cheat_profile_serialize(NULL, 2, &blob, &len, err, sizeof err) && blob == NULL && len == 0);
    CHECK(mh_cheat_profile_serialized_size(p) == 0 && mh_cheat_profile_serialized_size(NULL) == 0);

    CHECK(mh_cheat_profile_add_custom(p, "INFINITE LIVES", W8, 1, 0));
    CHECK(mh_cheat_profile_add_custom_disabled(p, "COND WRITE", COND, 2, 0x40));
    CHECK(mh_cheat_profile_add_custom(p, "BIG MONEY", W16, 1, 0x20));
    CHECK(mh_cheat_profile_needs_wire(p));
    CHECK(mh_cheat_profile_serialized_size(p) == 0x40 + 3 * 0x60 + 4 * 6);

    CHECK(!mh_cheat_profile_serialize(p, 0, &blob, &len, err, sizeof err));
    CHECK_STR(err, "Invalid cheat scope.");
    CHECK(!mh_cheat_profile_serialize(p, 3, &blob, &len, err, sizeof err) && blob == NULL && len == 0);
    CHECK(mh_cheat_profile_serialize(p, 2, &blob, &len, err, sizeof err));
    CHECK(len == 0x40 + 3 * 0x60 + 4 * 6);

    /* byte level layout of the version 4 blob */
    CHECK(memcmp(blob, "PSTCHT1\0", 8) == 0);
    CHECK(ch_rd16(blob + 8) == 4 && ch_rd16(blob + 10) == 0x40 && ch_rd32(blob + 0xc) == 0);
    CHECK(ch_rd32(blob + 0x10) == 3 && ch_rd32(blob + 0x14) == 2 && ch_rd32(blob + 0x18) == 4);
    CHECK(ch_rd32(blob + 0x1c) == 0x60 && ch_rd32(blob + 0x20) == 0x40);
    CHECK(ch_rd32(blob + 0x24) == 0x40 + 3 * 0x60 && ch_rd32(blob + 0x28) == len);
    CHECK(ch_rd32(blob + 0x2c) == NONE && ch_rd32(blob + 0x30) == NONE && ch_rd32(blob + 0x34) == 0);
    CHECK(blob[0x38] == 2 && blob[0x39] == 0 && ch_rd16(blob + 0x3a) == 0);
    CHECK(memcmp(blob + 0x40, "INFINITE LIVES", 15) == 0 && blob[0x40 + 15] == 0);
    CHECK(ch_rd32(blob + 0x40 + 0x40) == NONE && ch_rd32(blob + 0x40 + 0x44) == NONE);
    CHECK(ch_rd32(blob + 0x40 + 0x48) == 0x40 + 3 * 0x60 && ch_rd16(blob + 0x40 + 0x4c) == 1);
    CHECK(blob[0x40 + 0x4e] == 0 && blob[0x40 + 0x4f] == 0x05);
    CHECK(ch_rd32(blob + 0x40 + 0x50) == mh_crc32(W8, 6));
    CHECK(blob[0x40 + 0x60 + 0x4f] == 0x04 && blob[0x40 + 2 * 0x60 + 0x4f] == 0x25); /* bit 6 is not stored */
    CHECK(memcmp(blob + 0x40 + 3 * 0x60, W8, 6) == 0);
    CHECK(mh_cheat_blob_scope(blob, len) == 2);
    CHECK(mh_cheat_blob_validate(blob, len, err, sizeof err));
    CHECK(mh_cheat_blob_scope(NULL, 0) == 0 && mh_cheat_blob_scope(blob, 0x3f) == 0);

    CHECK(mh_cheat_profile_deserialize(blob, len, &q, err, sizeof err) && q);
    CHECK(profile_equal(p, q));
    CHECK(q->state == MH_CHEAT_STATE_NONE);
    /* bit 6 is derived again: COND WRITE is "70 + 30" so it is simple; the others are plain writes */
    CHECK(q->entries[0].flags == 0x45 && q->entries[1].flags == 0x44 && q->entries[2].flags == 0x65);
    mh_cheat_profile_free(q);

    /* every header rule of the validator, each with the original text */
    bad = malloc(len);
#define BAD(msg, expr)                                         \
    do {                                                       \
        memcpy(bad, blob, len);                                \
        expr;                                                  \
        rd_fix_crc(bad, len);                                  \
        CHECK(!mh_cheat_blob_validate(bad, len, err, sizeof err)); \
        CHECK_STR(err, msg);                                   \
    } while (0)
    BAD("Invalid PSTCHT1 header.", bad[0] = 'Q');
    BAD("Invalid PSTCHT1 header.", ch_wr16(bad + 8, 3));
    BAD("Invalid PSTCHT1 header.", ch_wr16(bad + 10, 0x41));
    BAD("Invalid PSTCHT1 header.", ch_wr32(bad + 0xc, 1));
    BAD("Invalid PSTCHT1 header.", ch_wr32(bad + 0x10, 4));
    BAD("Invalid PSTCHT1 header.", ch_wr32(bad + 0x1c, 0x58));
    BAD("Invalid PSTCHT1 header.", ch_wr32(bad + 0x28, len + 1));
    BAD("Invalid PSTCHT1 header.", bad[0x38] = 0);
    BAD("Invalid PSTCHT1 header.", bad[0x38] = 3);
    BAD("Invalid PSTCHT1 header.", ch_wr32(bad + 0x18, 600));
    memcpy(bad, blob, len);
    bad[0x41] ^= 1; /* crc not fixed */
    CHECK(!mh_cheat_blob_validate(bad, len, err, sizeof err));
    CHECK_STR(err, "Invalid PSTCHT1 header.");
    BAD("Invalid provenance in the PSTCHT1 header.", bad[0x39] = 4);
    BAD("Invalid provenance in the PSTCHT1 header.", ch_wr32(bad + 0x34, 1));
    BAD("Invalid PSTCHT1 cheat record.", bad[0x40] = 0);               /* empty name */
    BAD("Invalid PSTCHT1 cheat record.", bad[0x40] = '[');
    BAD("Invalid PSTCHT1 cheat record.", bad[0x40 + 20] = 'X');       /* junk after the NUL */
    BAD("Invalid PSTCHT1 cheat record.", bad[0x40 + 0x58] = 1);       /* reserved bytes */
    BAD("Invalid PSTCHT1 cheat record.", ch_wr16(bad + 0x40 + 0x4c, 0));
    BAD("Invalid PSTCHT1 cheat record.", ch_wr32(bad + 0x40 + 0x48, 0x40 + 3 * 0x60 + 1));
    BAD("Invalid PSTCHT1 cheat record.", ch_wr32(bad + 0x40 + 0x50, 1));
    BAD("Invalid PSTCHT1 cheat record.", bad[0x40 + 0x4f] |= 0x80);
    BAD("Invalid PSTCHT1 cheat record.", bad[0x40 + 3 * 0x60] = 0x61); /* bad opcode (crc of code too) */
    BAD("A PSTCHT1 record must be exactly AUDITED xor CUSTOM.", bad[0x40 + 0x4f] = 0x01);
    BAD("A CUSTOM record has invalid official provenance.", bad[0x40 + 0x4e] = 1);
    BAD("A CUSTOM record has invalid official provenance.", ch_wr32(bad + 0x40 + 0x44, 3));
    BAD("A CUSTOM record has invalid official provenance.", bad[0x40 + 0x4f] = 0x15);
    BAD("A mandatory cheat must be AUDITED and ENABLED in the PSTCHT1.", bad[0x40 + 0x4f] = 0x07);
    /* "auditado e personalizado ao mesmo tempo" (flags 0x0c) is shadowed by the xor rule above it */
    BAD("A PSTCHT1 record must be exactly AUDITED xor CUSTOM.", bad[0x40 + 0x4f] = 0x0d);
    BAD("Invalid counts, effect, provenance or 31-active-line limit in the PSTCHT1.",
        ch_wr32(bad + 0x14, 1));
    BAD("Invalid counts, effect, provenance or 31-active-line limit in the PSTCHT1.",
        ch_wr32(bad + 0x18, 5));
    CHECK(!mh_cheat_blob_validate(blob, len - 1, err, sizeof err));
    CHECK_STR(err, "Invalid PSTCHT1 header.");
    CHECK(!mh_cheat_blob_validate(blob, 0x3f, err, sizeof err));
    CHECK_STR(err, "The cheat blob is missing, truncated or larger than 10,240 bytes.");
    CHECK(!mh_cheat_blob_validate(NULL, 100, err, sizeof err));
    CHECK_STR(err, "The cheat blob is missing, truncated or larger than 10,240 bytes.");
    {
        uint8_t *huge = calloc(1, 0x2801);
        CHECK(!mh_cheat_blob_validate(huge, 0x2801, err, sizeof err));
        CHECK_STR(err, "The cheat blob is missing, truncated or larger than 10,240 bytes.");
        free(huge);
    }
    free(bad);
    free(blob);
    mh_cheat_profile_free(p);
}

/* ---- tests that need the database -------------------------------------------------------- */

/* the firmware ignores a version 4 blob with more than 0x100 lines (FUN_100228c8), so the Manager
 * must refuse it instead of writing cheats that never run */
static MhCheatProfile *wire_lines(unsigned first, unsigned second)
{
    MhCheatProfile *p = manual(2);
    unsigned n[2] = {first, second};
    for (unsigned i = 0; i < 2; i++) {
        MhCheatEntry *e = &p->entries[i];
        free(e->lines);
        e->nlines = (uint16_t)n[i];
        e->lines = malloc((size_t)n[i] * 6);
        for (unsigned j = 0; j < n[i]; j++) memcpy(e->lines + j * 6, W8, 6);
    }
    return p;
}

static void test_wire_firmware_line_limit(void)
{
    uint8_t *blob = NULL;
    uint32_t len = 0;
    MhCheatProfile *p = wire_lines(128, 128);
    CHECK(mh_cheat_profile_serialize(p, 1, &blob, &len, err, sizeof err) && ch_rd32(blob + 0x18) == 0x100);
    free(blob);
    mh_cheat_profile_free(p);

    blob = NULL;
    p = wire_lines(129, 128);
    CHECK(!mh_cheat_profile_serialize(p, 1, &blob, &len, err, sizeof err) && blob == NULL && len == 0);
    CHECK_STR(err, "The firmware only runs 256 cheat lines per disc. Remove lines before applying.");
    mh_cheat_profile_free(p);
}

static void test_audited(void)
{
    MhCheatProfile *p = mh_cheat_profile_new(), *q = NULL, *cat = NULL;
    uint8_t *blob = NULL;
    uint32_t len = 0;
    MhCheatEntry e;

    /* title 0 (007 Racing), variant 0, entries 0..4 */
    CHECK(mh_cheat_profile_add_audited(p, &g_db, 0, 2));
    CHECK(p->game == 1 && p->title == 0 && p->variant == 0 && p->db_crc == MH_CHEAT_DB_CRC && p->count == 1);
    CHECK(p->entries[0].flags & MH_CHEAT_F_AUDITED && p->entries[0].game == 1 && p->entries[0].variant == 0);
    CHECK_STR(p->entries[0].name, "HEALTH");
    CHECK(mh_cheat_profile_find_audited(p, 0, 4) == 0 && mh_cheat_profile_find_audited(p, 0, 3) == -1);
    CHECK(mh_cheat_profile_add_audited(p, &g_db, 0, 2) && p->count == 1); /* same group: no duplicate */
    CHECK(!mh_cheat_profile_add_audited(p, &g_db, 1, 0));                 /* other variant */
    CHECK(!mh_cheat_profile_add_audited(p, &g_db, 0, 9));                 /* no such entry */
    CHECK(!mh_cheat_profile_add_audited(NULL, &g_db, 0, 0) && !mh_cheat_profile_add_audited(p, NULL, 0, 0));
    CHECK(mh_cheat_profile_add_audited(p, &g_db, 0, 0));
    CHECK(mh_cheat_profile_add_audited(p, &g_db, 0, 3));
    CHECK(p->count == 3);
    p->entries[0].flags |= MH_CHEAT_F_ENABLED;
    CHECK(mh_cheat_profile_add_custom(p, "MY CODE", W8, 1, 0));
    CHECK(!mh_cheat_profile_needs_wire(p));
    /* compact: 0x40 + per entry (8 + strlen(name) + 1 + 6 * lines) */
    {
        unsigned expect = 0x40, i;
        for (i = 0; i < p->count; i++) expect += 9 + (unsigned)strlen(p->entries[i].name) + p->entries[i].nlines * 6;
        CHECK(mh_cheat_profile_serialized_size(p) == expect);
    }
    CHECK(mh_cheat_profile_serialize(p, 2, &blob, &len, err, sizeof err));
    CHECK(len == mh_cheat_profile_serialized_size(p));
    CHECK(ch_rd16(blob + 8) == 6 && ch_rd32(blob + 0x1c) == 8 && ch_rd32(blob + 0x24) == 0x40 + 4 * 8);
    CHECK(ch_rd16(blob + 0x28) == len && ch_rd32(blob + 0x2c) == 0 && ch_rd32(blob + 0x30) == 0);
    CHECK(ch_rd32(blob + 0x34) == MH_CHEAT_DB_CRC && blob[0x39] == 1 && blob[0x38] == 2);
    CHECK(mh_cheat_disc_bank_valid(blob, len) && mh_cheat_blob_validate(blob, len, err, sizeof err));
    {
        /* the compact format only takes scope 1 and 2: its own validator refuses anything else */
        uint8_t *none = NULL;
        uint32_t nl = 0;
        CHECK(!mh_cheat_profile_serialize(p, 0, &none, &nl, err, sizeof err) && none == NULL && nl == 0);
        CHECK_STR(err, "The original catalog is invalid or differs.");
        CHECK(mh_cheat_profile_serialize(p, 1, &none, &nl, err, sizeof err) && none[0x38] == 1);
        free(none);
    }
    /* the entry directory refers to bank positions: HEALTH is group 2, the custom one comes after
     * the 5 groups of the title */
    CHECK(ch_rd16(blob + 0x40 + 4) == 2 && ch_rd16(blob + 0x40 + 3 * 8 + 4) == 5);
    CHECK(blob[0x40 + 3 * 8 + 7] == 0x29); /* custom: 0x28 | enabled */
    CHECK(blob[0x40 + 7] == (p->entries[0].flags & 0x3f));
    /* device form of the audited program: opcode 80 is stored as 00 */
    {
        uint32_t co = ch_rd16(blob + 0x40 + 2);
        CHECK(blob[co] == 0x00 && memcmp(blob + co + 1, p->entries[0].lines + 1, 5) == 0);
    }
    CHECK(mh_cheat_profile_deserialize(blob, len, &q, err, sizeof err) && q);
    CHECK(profile_equal(p, q) && q->state == MH_CHEAT_STATE_NONE);
    CHECK(q->entries[3].flags == 0x25 && q->entries[0].lines[0] == 0x80); /* decoded form restored */
    mh_cheat_profile_free(q);

    /* removing the custom one keeps provenance, removing every audited one resets it */
    CHECK(mh_cheat_profile_remove(p, 3) && p->game == 1);
    CHECK(mh_cheat_profile_remove(p, 0) && mh_cheat_profile_remove(p, 0) && p->game == 1);
    CHECK(mh_cheat_profile_remove(p, 0));
    CHECK(p->count == 0 && p->game == 0 && p->db_crc == 0 && p->title == NONE && p->variant == NONE);
    free(blob);
    blob = NULL;

    /* a changed audited program no longer matches the bank: full format */
    CHECK(mh_cheat_profile_add_audited(p, &g_db, 0, 2));
    p->entries[0].lines[5] ^= 0x01;
    CHECK(mh_cheat_profile_needs_wire(p));
    CHECK(mh_cheat_profile_serialized_size(p) == 0x40 + 0x60 + 6);
    CHECK(mh_cheat_profile_serialize(p, 1, &blob, &len, err, sizeof err));
    CHECK(ch_rd16(blob + 8) == 4 && blob[0x38] == 1 && blob[0x39] == 1);
    CHECK(ch_rd32(blob + 0x2c) == 0 && ch_rd32(blob + 0x30) == 0 && ch_rd32(blob + 0x34) == MH_CHEAT_DB_CRC);
    CHECK(mh_cheat_blob_scope(blob, len) == 1);
    CHECK(mh_cheat_profile_deserialize(blob, len, &q, err, sizeof err) && q);
    CHECK(profile_equal(p, q) && q->state == MH_CHEAT_STATE_FULL_SNAPSHOT);
    mh_cheat_profile_free(q);
    free(blob);
    blob = NULL;
    mh_cheat_profile_free(p);

    /* whole catalog: enable a few groups, add customs, round trip in the compact format */
    CHECK(mh_cheat_profile_build_exact_catalog(&g_db, 835, "SLUS-00402", &cat, err, sizeof err));
    CHECK(cat->count > 5 && cat->title == 835 && cat->game == 1);
    CHECK_STR(cat->serial, "SLUS-00402");
    CHECK(mh_cheat_profile_enabled_count(cat) == 0);
    cat->entries[0].flags |= MH_CHEAT_F_ENABLED;
    cat->entries[2].flags |= MH_CHEAT_F_ENABLED;
    CHECK(mh_cheat_profile_add_custom(cat, "A CUSTOM ONE", COND, 2, 0));
    CHECK(mh_cheat_profile_validate(cat, err, sizeof err));
    CHECK(mh_cheat_profile_serialize(cat, 2, &blob, &len, err, sizeof err));
    CHECK(len <= MH_CHEAT_BLOB_MAX && ch_rd16(blob + 8) == 6);
    CHECK(mh_cheat_profile_deserialize(blob, len, &q, err, sizeof err) && profile_equal(cat, q));
    CHECK(mh_cheat_profile_enabled_count(q) == 3);
    mh_cheat_profile_free(q);
    free(blob);
    mh_cheat_profile_free(cat);

    /* serial binding path */
    CHECK(mh_cheat_profile_build_for_disc(&g_db, "SLUS-00402", &cat, err, sizeof err) && cat->title == 835);
    mh_cheat_profile_free(cat);
    cat = NULL;
    CHECK(!mh_cheat_profile_build_for_disc(&g_db, "SLUS-99999", &cat, err, sizeof err) && cat == NULL);
    CHECK_STR(err, "The confirmed disc has no unambiguous cheat catalog.");
    CHECK(!mh_cheat_profile_build_for_disc(&g_db, "", &cat, err, sizeof err));
    CHECK_STR(err, "The confirmed disc has no unambiguous cheat catalog.");
    {
        MhCheatDb none;
        memset(&none, 0, sizeof none);
        CHECK(!mh_cheat_profile_build_for_disc(&none, "SLUS-00402", &cat, err, sizeof err));
        CHECK_STR(err, "The embedded cheat database is unavailable.");
        CHECK(!mh_cheat_profile_build_exact_catalog(&none, 835, "X", &cat, err, sizeof err));
        CHECK_STR(err, "The audited cheat database is unavailable.");
    }
    CHECK(!mh_cheat_profile_build_exact_catalog(&g_db, 5000, "X", &cat, err, sizeof err));
    CHECK_STR(err, "No audited variant fits safely in the extended 10,240 byte PSTCHT1.");

    (void)e;
}

/* every audited entry of the database must serialise within the region and read back equal.
 * 79 entries hold lines the runtime cannot take (20, 21, C1, or a condition with nothing after
 * it); the original refuses those too (cheat_db_entry_info), the catalog builder skips them. */
static void test_every_entry(void)
{
    uint32_t v, k, total = 0, usable = 0, maxlen = 0;
    for (v = 0; v < g_db.n_variants; v++) {
        MhCheatVariantInfo vi;
        CHECK(mh_cheat_db_variant_info(&g_db, v, &vi));
        for (k = 0; k < vi.entry_count; k++) {
            MhCheatProfile *p = mh_cheat_profile_new(), *q = NULL;
            MhCheatEntryInfo ei;
            uint8_t *blob = NULL;
            uint32_t len = 0;
            total++;
            if (!mh_cheat_db_entry_info(&g_db, v, k, &ei)) {
                CHECK(!mh_cheat_profile_add_audited(p, &g_db, v, k) && p->count == 0);
                mh_cheat_profile_free(p);
                continue;
            }
            usable++;
            CHECK(mh_cheat_profile_add_audited(p, &g_db, v, k));
            CHECK(!mh_cheat_profile_needs_wire(p));
            CHECK(mh_cheat_profile_serialize(p, 2, &blob, &len, err, sizeof err));
            CHECK(len >= 0x40 && len <= MH_CHEAT_BLOB_MAX);
            CHECK(mh_cheat_disc_bank_valid(blob, len));
            CHECK(mh_cheat_profile_deserialize(blob, len, &q, err, sizeof err));
            CHECK(profile_equal(p, q));
            if (len > maxlen) maxlen = len;
            free(blob);
            mh_cheat_profile_free(p);
            mh_cheat_profile_free(q);
        }
    }
    CHECK(total == 10118 && usable == 10118 - 79);
    fprintf(stderr, "every entry: %u single-entry blobs, largest %u bytes\n", usable, maxlen);
}

/* the full catalog of each title in both PSTCHT1 flavours */
static void test_every_catalog(void)
{
    uint32_t t, maxlen = 0;
    for (t = 0; t < g_db.n_titles; t++) {
        MhCheatProfile *p = NULL, *q = NULL;
        uint8_t *blob = NULL;
        uint32_t len = 0;
        CHECK(mh_cheat_profile_build_exact_catalog(&g_db, t, "SLUS-00001", &p, err, sizeof err));
        CHECK(!mh_cheat_profile_needs_wire(p));
        CHECK(mh_cheat_profile_serialize(p, 2, &blob, &len, err, sizeof err));
        CHECK(len <= MH_CHEAT_BLOB_MAX && mh_cheat_blob_validate(blob, len, err, sizeof err));
        CHECK(mh_cheat_profile_deserialize(blob, len, &q, err, sizeof err) && profile_equal(p, q));
        if (len > maxlen) maxlen = len;
        free(blob);
        mh_cheat_profile_free(q);
        mh_cheat_profile_free(p);
    }
    fprintf(stderr, "every catalog: largest compact blob %u bytes\n", maxlen);
}

static void test_legacy_and_refs(void)
{
    MhCheatProfile *p = mh_cheat_profile_new(), *q = NULL;
    uint8_t *blob = NULL, *b2;
    uint32_t len = 0, i;

    /* A version 4 blob that names the older database (crc 3CC8BE42) for an audited game is not
     * accepted by the validator, so deserialize's "drop the old audited entries" branch is dead
     * code in this version of the original; the port keeps it, the validator stops it first. */
    CHECK(mh_cheat_profile_add_audited(p, &g_db, 0, 2));
    p->entries[0].lines[5] ^= 0x01; /* force the full format */
    CHECK(mh_cheat_profile_add_custom(p, "KEEP ME", W8, 1, 0));
    CHECK(mh_cheat_profile_serialize(p, 2, &blob, &len, err, sizeof err) && ch_rd16(blob + 8) == 4);
    b2 = malloc(len);
    memcpy(b2, blob, len);
    ch_wr32(b2 + 0x34, MH_CHEAT_DB_LEGACY_CRC);
    rd_fix_crc(b2, len);
    CHECK(!mh_cheat_blob_validate(b2, len, err, sizeof err));
    CHECK_STR(err, "Invalid provenance in the PSTCHT1 header.");
    CHECK(!mh_cheat_profile_deserialize(b2, len, &q, err, sizeof err) && q == NULL);
    free(b2);
    free(blob);
    mh_cheat_profile_free(p);

    /* version 5: references only (4 byte records), read only */
    len = 0x40 + 3 * 4;
    b2 = calloc(1, len);
    memcpy(b2, "PSTCHT1", 7);
    ch_wr16(b2 + 8, 5);
    ch_wr16(b2 + 10, 0x40);
    ch_wr32(b2 + 0x10, 3);
    ch_wr32(b2 + 0x1c, 4);
    ch_wr32(b2 + 0x20, 0x40);
    ch_wr32(b2 + 0x24, len);
    ch_wr32(b2 + 0x28, len);
    ch_wr32(b2 + 0x2c, 1);
    ch_wr32(b2 + 0x30, 1);
    ch_wr32(b2 + 0x34, MH_CHEAT_DB_CRC);
    b2[0x38] = 2;
    b2[0x39] = 1;
    {
        const MhCheatBank *bank = ch_bank_active();
        unsigned lines = 0;
        for (i = 0; i < 3; i++) {
            uint8_t *r = b2 + 0x40 + i * 4;
            ch_wr16(r, i + 1);          /* positions 1, 2, 3 of title 1 */
            r[2] = i == 1 ? 0x09 : 0x08; /* audited, the middle one enabled */
            lines += ch_bank_group(bank, 1, i + 1)->nlines;
        }
        ch_wr32(b2 + 0x14, 1);
        ch_wr32(b2 + 0x18, lines);
    }
    rd_fix_crc(b2, len);
    CHECK(mh_cheat_blob_validate(b2, len, err, sizeof err));
    CHECK(mh_cheat_blob_scope(b2, len) == 2);
    CHECK(mh_cheat_profile_deserialize(b2, len, &q, err, sizeof err) && q);
    CHECK(q->count == 3 && q->state == MH_CHEAT_STATE_REFS_ONLY && q->title == 1 && q->variant == 1);
    CHECK_STR(q->entries[0].name, ch_bank_group(ch_bank_active(), 1, 1)->name);
    CHECK(q->entries[1].flags == 0x09 && q->entries[0].flags == 0x08 && q->entries[0].game == 1);
    CHECK(q->entries[0].id == ch_bank_group(ch_bank_active(), 1, 1)->id);
    CHECK(mh_cheat_profile_enabled_count(q) == 1);
    mh_cheat_profile_free(q);
    /* each reference rule */
#define BAD5(expr)                                                  \
    do {                                                            \
        uint8_t *x = malloc(len);                                   \
        memcpy(x, b2, len);                                         \
        expr;                                                       \
        rd_fix_crc(x, len);                                         \
        CHECK(!mh_cheat_blob_validate(x, len, err, sizeof err));    \
        CHECK_STR(err, "Invalid references to the original catalog."); \
        free(x);                                                    \
    } while (0)
    BAD5(ch_wr16(x + 0x40, 200));            /* no such group */
    BAD5(ch_wr16(x + 0x44, 1));              /* duplicate position */
    BAD5(x[0x42] = 0x0c);                    /* custom bit in a reference */
    BAD5(x[0x42] = 0x00);                    /* not audited */
    BAD5(x[0x42] = 0x0a);                    /* mandatory but not enabled */
    BAD5(x[0x43] = 1);
    BAD5(ch_wr32(x + 0x14, 2));
    BAD5(ch_wr32(x + 0x18, 1));
    BAD5(ch_wr32(x + 0x30, 2));              /* title != variant */
    BAD5(x[0x39] = 2);
    free(b2);
    mh_cheat_profile_free(NULL);
}

static void test_restore_and_combined(void)
{
    mh_cheat_profile_active_stats(NULL, NULL, NULL, NULL, NULL); /* NULL is an empty profile */
    MhCheatProfile *fresh = NULL, *saved = NULL, *other = NULL, *cust = mh_cheat_profile_new();
    uint32_t i;
    CHECK(mh_cheat_profile_build_exact_catalog(&g_db, 0, "S", &saved, err, sizeof err));
    CHECK(saved->count == 5);
    saved->entries[1].flags |= MH_CHEAT_F_ENABLED;
    saved->entries[4].flags |= MH_CHEAT_F_ENABLED;
    CHECK(mh_cheat_profile_add_custom(saved, "MINE", W8, 1, 0));
    /* the user reorders: move the last audited entry to the front */
    {
        MhCheatEntry t = saved->entries[4];
        memmove(&saved->entries[1], &saved->entries[0], 4 * sizeof t);
        saved->entries[0] = t;
    }
    CHECK(mh_cheat_profile_validate(saved, err, sizeof err));
    CHECK(mh_cheat_profile_build_exact_catalog(&g_db, 0, "S", &fresh, err, sizeof err));
    CHECK(mh_cheat_profile_restore_user_state(fresh, saved, err, sizeof err));
    CHECK(fresh->count == 6);
    for (i = 0; i < 6; i++) {
        CHECK_STR(fresh->entries[i].name, saved->entries[i].name); /* order follows the saved profile */
        CHECK((fresh->entries[i].flags & 1) == (saved->entries[i].flags & 1));
    }
    mh_cheat_profile_free(fresh);

    /* a saved group that the new catalog lacks */
    CHECK(mh_cheat_profile_build_exact_catalog(&g_db, 0, "S", &fresh, err, sizeof err));
    mh_cheat_profile_remove(fresh, 0);
    CHECK(!mh_cheat_profile_restore_user_state(fresh, saved, err, sizeof err));
    CHECK_STR(err, "The saved profile was kept: the new selection does not contain all the previous groups.");
    mh_cheat_profile_free(fresh);
    /* another library */
    CHECK(mh_cheat_profile_build_exact_catalog(&g_db, 1, "S", &other, err, sizeof err));
    CHECK(!mh_cheat_profile_restore_user_state(other, saved, err, sizeof err));
    CHECK_STR(err,
              "The saved profile was kept. Another library can be browsed in the editor without "
              "changing your choices.");
    CHECK(mh_cheat_profile_restore_user_state(NULL, saved, err, sizeof err));
    CHECK(mh_cheat_profile_restore_user_state(other, NULL, err, sizeof err));

    /* combined Case + disc validation */
    CHECK(mh_cheat_profiles_validate_combined(NULL, NULL, err, sizeof err));
    CHECK(mh_cheat_profile_add_custom(cust, "CASE CODE", W8, 1, 0));
    CHECK(mh_cheat_profiles_validate_combined(cust, saved, err, sizeof err));
    CHECK(mh_cheat_profiles_validate_combined(NULL, saved, err, sizeof err));
    CHECK(mh_cheat_profiles_validate_combined(cust, NULL, err, sizeof err));
    other->entries[0].flags |= MH_CHEAT_F_ENABLED;
    CHECK(!mh_cheat_profiles_validate_combined(other, saved, err, sizeof err));
    CHECK_STR(err,
              "Audited cheats on the Case and on the disc belong to different variants. Use the "
              "official variant on the disc or keep only custom cheats on the Case.");
    {
        /* enabled lines of both together: 510 + 255 */
        MhCheatProfile *a = manual(2), *b = manual(1);
        uint32_t en = 0, ln = 0, cost = 0, aud = 0;
        for (int k = 0; k < 2; k++) {
            free(a->entries[k].lines);
            a->entries[k].lines = malloc(255 * 6);
            for (i = 0; i < 255; i++) memcpy(a->entries[k].lines + i * 6, W8, 6);
            a->entries[k].nlines = 255;
        }
        free(b->entries[0].lines);
        b->entries[0].lines = malloc(255 * 6);
        for (i = 0; i < 255; i++) memcpy(b->entries[0].lines + i * 6, W8, 6);
        b->entries[0].nlines = 255;
        CHECK(mh_cheat_profile_validate(a, err, sizeof err) && mh_cheat_profile_validate(b, err, sizeof err));
        CHECK(!mh_cheat_profiles_validate_combined(a, b, err, sizeof err));
        CHECK_STR(err, "Case and disc exceed the internal line limit. Disable some codes.");
        mh_cheat_profile_active_stats(a, &en, &ln, &cost, &aud);
        CHECK(en == 2 && ln == 510 && cost == 510 * 0x2c && aud == 0);
        mh_cheat_profile_free(a);
        mh_cheat_profile_free(b);
    }
    mh_cheat_profile_free(cust);
    mh_cheat_profile_free(other);
    mh_cheat_profile_free(saved);
}

static void test_catalog_entry(void)
{
    MhCheatProfile *p = mh_cheat_profile_new();
    CHECK(mh_cheat_profile_add_catalog_entry(p, &g_db, 0, 1, MH_CHEAT_BUDGET_DISC, err, sizeof err));
    CHECK(p->count == 1 && !(p->entries[0].flags & MH_CHEAT_F_ENABLED) && p->variant == 0);
    CHECK(mh_cheat_profile_add_catalog_entry(p, &g_db, 0, 1, MH_CHEAT_BUDGET_DISC, err, sizeof err));
    CHECK(p->count == 1); /* identical group is already there */
    CHECK(!mh_cheat_profile_add_catalog_entry(p, &g_db, 1, 0, MH_CHEAT_BUDGET_DISC, err, sizeof err));
    CHECK_STR(err, "Remove the cheats of the previous title before adding another original game.");
    CHECK(p->variant == 0 && p->title == 0 && p->count == 1);
    CHECK(!mh_cheat_profile_add_catalog_entry(p, &g_db, 0, 77, MH_CHEAT_BUDGET_DISC, err, sizeof err));
    CHECK_STR(err, "This group is not supported by the current engine.");
    CHECK(!mh_cheat_profile_add_catalog_entry(p, &g_db, 0, 2, 0x44, err, sizeof err));
    CHECK_STR(err, "The whole group does not fit. Remove another group from the prepared list before adding this one.");
    CHECK(p->count == 1);
    CHECK(mh_cheat_profile_add_catalog_entry(p, &g_db, 0, 2, 0x48, err, sizeof err));
    mh_cheat_profile_free(p);
    p = mh_cheat_profile_new();
    CHECK(!mh_cheat_profile_add_catalog_entry(p, &g_db, 0, 1, 0x43, err, sizeof err));
    CHECK(p->game == 0 && p->title == NONE);
    CHECK_STR(err, "The whole group does not fit. Remove another group from the prepared list before adding this one.");
    mh_cheat_profile_free(p);
}

int main(void)
{
    test_validate_messages();
    test_add_remove_clone();
    test_wire_roundtrip_and_validation();
    test_wire_firmware_line_limit();
    if (!mh_read_entire_file(CDX_PATH, &g_blob, &g_blob_len)) {
        fprintf(stderr, "SKIP database tests: %s not found (run from the repository root)\n", CDX_PATH);
        printf("test_cheat_profile: %d checks passed (database tests skipped)\n", g_checks);
        return 0;
    }
    CHECK(mh_cheat_db_load(&g_db, g_blob, (size_t)g_blob_len));
    test_audited();
    test_catalog_entry();
    test_legacy_and_refs();
    test_restore_and_combined();
    test_every_entry();
    test_every_catalog();
    mh_cheat_db_free(&g_db);
    free(g_blob);
    /* without a database the compact format is not available, the full one still is */
    {
        MhCheatProfile *p = mh_cheat_profile_new();
        MhCheatProfile *c = manual(1);
        uint8_t *blob = NULL;
        uint32_t len = 0;
        CHECK(ch_bank_active() == NULL);
        CHECK(mh_cheat_profile_add_custom(p, "ONE", W8, 1, 0));
        CHECK(mh_cheat_profile_serialize(p, 1, &blob, &len, err, sizeof err) && ch_rd16(blob + 8) == 4);
        free(blob);
        mh_cheat_profile_free(c);
        mh_cheat_profile_free(p);
    }
    printf("test_cheat_profile: %d checks passed\n", g_checks);
    return 0;
}
