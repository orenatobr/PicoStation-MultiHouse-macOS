/* v2_validate.c: orig v2_validate_image @0x1400389e0. */
#include <stdlib.h>
#include <unistd.h>

#include "v2_internal.h"

#define RAW_CHUNK 0x800u /* slots read at a time (0x500000 bytes) */

/* Reports a failed rule and closes. */
static bool fail(MhIo *io, MhV2Result *res, const char *msg)
{
    mh_io_close(io);
    mh_v2_result_fail(res, msg);
    return false;
}

/* orig: whole-file CRC-32 + SHA-1 done with a second handle at the end of v2_validate_image. */
static bool hash_file(const char *path, uint64_t *size, uint32_t *crc, uint8_t sha[20])
{
    MhIo io;
    uint8_t *buf;
    uint32_t c;
    MhSha1 s;
    uint64_t off = 0, total;
    if (!mh_io_open(&io, path, MH_IO_READ, false, NULL)) return false;
    if (!mh_io_size(&io, &total)) {
        mh_io_close(&io);
        return false;
    }
    buf = malloc(0x100000);
    if (!buf) {
        mh_io_close(&io);
        return false;
    }
    c = mh_crc32_begin();
    mh_sha1_init(&s);
    /* ReadFile until it returns an error or 0 bytes: a vanished tail ends the loop */
    while (off < total) {
        size_t chunk = total - off > 0x100000 ? 0x100000 : (size_t)(total - off);
        ssize_t n = pread(io.fd, buf, chunk, (off_t)off);
        if (n <= 0) break;
        c = mh_crc32_update(c, buf, (size_t)n);
        mh_sha1_update(&s, buf, (size_t)n);
        off += (uint64_t)n;
    }
    free(buf);
    mh_io_close(&io);
    *size = total;
    *crc = mh_crc32_end(c);
    mh_sha1_final(&s, sha);
    return true;
}

bool mh_v2_validate_image(const char *path, MhProgress *prog, MhV2Result *res)
{
    MhIo io;
    uint64_t fsize;
    uint8_t ha[MH_V2_HDR_SIZE], hb[MH_V2_HDR_SIZE];
    uint8_t *cases = NULL, *discs = NULL, *raw = NULL;
    uint8_t used[256], case_of[256], slot_of[256];
    unsigned i, j, ncases = 0, ndiscs = 0;
    uint64_t total_raw = 0, raw_done = 0;
    MhV2Header hd;
    bool ok = false;
    uint32_t c;

    if (res) memset(res, 0, sizeof *res);
    if (!mh_io_open(&io, path, MH_IO_READ, false, NULL)) {
        if (!res) return false;
        mh_v2_result_fail(res, "Imagem V2 ausente ou pequena demais.");
        return false;
    }
    if (!mh_io_size(&io, &fsize) || fsize < MH_V2_MIN_IMAGE) {
        mh_io_close(&io);
        if (res) mh_v2_result_fail(res, "Imagem V2 ausente ou pequena demais.");
        return false;
    }
    /* both header slots, 0x930 bytes each */
    if (!mh_io_read(&io, MH_V2_HDR_A_OFF, ha, sizeof ha) || !mh_io_read(&io, MH_V2_HDR_B_OFF, hb, sizeof hb)) {
        mh_io_close(&io);
        return false;
    }
    if (memcmp(ha, hb, sizeof ha) != 0 || !mh_v2_header_valid(ha))
        return fail(&io, res, "Header V2 invalido ou divergente do backup.");
    mh_v2_header_decode(ha, &hd);

    cases = malloc(MH_V2_CASE_TABLE);
    discs = malloc(MH_V2_DISC_TABLE);
    if (!cases || !discs) {
        mh_io_close(&io);
        free(cases);
        free(discs);
        return false;
    }
    if (!mh_io_read(&io, MH_V2_CASE_OFF, cases, MH_V2_CASE_TABLE) ||
        !mh_io_read(&io, MH_V2_DISC_OFF, discs, MH_V2_DISC_TABLE)) {
        mh_io_close(&io);
        free(cases);
        free(discs);
        return false;
    }
    {
        uint32_t crc_art = 0;
        /* a failed region read gets the same text as a mismatch (the original tests
         * "crc_file_region != 0 && crc == stored" and falls through to the message) */
        if (mh_crc32(cases, MH_V2_CASE_TABLE) != hd.crc_cases || mh_crc32(discs, MH_V2_DISC_TABLE) != hd.crc_discs ||
            !mh_io_crc_region(&io, MH_V2_ART_OFF, MH_V2_ART_REGION, &crc_art) || crc_art != hd.crc_art) {
            free(cases);
            free(discs);
            return fail(&io, res, "CRC das tabelas/arts V2 nao confere.");
        }
    }

    memset(used, 0, sizeof used);
    memset(case_of, 0xff, sizeof case_of);
    memset(slot_of, 0xff, sizeof slot_of);
    for (i = 0; i < 256; i++) {
        const uint8_t *r = cases + (size_t)i * MH_V2_CASE_SIZE;
        uint16_t art;
        if (r[4] == 0) continue;
        if (memcmp(r, MH_V2_CASE_MAGIC, 4) != 0 || r[6] != i || (uint8_t)(r[5] - 1) > 4 ||
            mh_crc_record(r, MH_V2_CASE_SIZE) != mh_v2_rd32(r + 0x4c)) {
            free(cases); free(discs);
            return fail(&io, res, "Registro de Case V2 invalido.");
        }
        art = mh_v2_rd16(r + 0x38);
        if (art != 0xffff && art != i) {
            free(cases); free(discs);
            return fail(&io, res, "ID de arte da Case invalido.");
        }
        for (j = 0; j < 5; j++) {
            uint8_t id = r[0x3a + j];
            if (j < r[5]) {
                if (used[id]) {
                    free(cases); free(discs);
                    return fail(&io, res, "Referencia de disco duplicada/invalida em uma Case.");
                }
                used[id] = 1;
                case_of[id] = (uint8_t)i;
                slot_of[id] = (uint8_t)j;
            } else if (id != 0) {
                free(cases); free(discs);
                return fail(&io, res, "Case possui lixo apos o ultimo disco.");
            }
        }
        ncases++;
    }
    if (hd.case_count != ncases) {
        free(cases); free(discs);
        return fail(&io, res, "case_count nao confere.");
    }

    for (i = 0; i < 256; i++) {
        const uint8_t *r = discs + (size_t)i * MH_V2_DISC_SIZE;
        if (r[4]) total_raw += (uint64_t)mh_v2_rd32(r + 0x5c) * MH_V2_SLOT;
    }
    raw = malloc((size_t)RAW_CHUNK * MH_V2_SLOT);
    if (!raw) {
        mh_io_close(&io);
        free(cases); free(discs);
        return false;
    }

#define DFAIL(msg) do { free(raw); free(cases); free(discs); return fail(&io, res, msg); } while (0)
#define DSILENT() do { free(raw); free(cases); free(discs); mh_io_close(&io); return false; } while (0)

    for (i = 0; i < 256; i++) {
        const uint8_t *r = discs + (size_t)i * MH_V2_DISC_SIZE;
        uint64_t base;
        uint8_t trk[MH_V2_HDR_SIZE], pad[0xd0];
        uint32_t sectors, remaining, cheat_size;
        uint16_t aid;
        uint32_t dcrc;
        MhSha1 sha;
        uint8_t dsha[20];

        if (r[4] == 0) {
            if (used[i]) DFAIL("Case referencia DiscRecord vazio.");
            continue;
        }
        if (!used[i]) DFAIL("DiscRecord V2 invalido.");
        sectors = mh_v2_rd32(r + 0x5c);
        base = mh_v2_rd64(r + 0x3c);
        if (memcmp(r, MH_V2_DISC_MAGIC, 4) != 0 || r[5] != i || r[6] != case_of[i] || r[7] != slot_of[i] ||
            (uint8_t)(r[0x60] - 1) > 0x62 || sectors == 0 || mh_v2_rd64(r + 0x54) != (uint64_t)sectors * 5 ||
            (base & 0xff) != 0 || base != mh_v2_rd64(r + 0x44))
            DFAIL("DiscRecord V2 invalido.");
        if (!mh_disc_layout_valid(r) || mh_v2_rd64(r + 0x4c) != base + 0x100 ||
            mh_crc_record(r, MH_V2_DISC_SIZE) != mh_v2_rd32(r + 0xfc))
            DFAIL("DiscRecord V2 invalido.");

        /* track map + the zero padding behind it */
        if (!mh_io_read(&io, mh_v2_rd64(r + 0x44) << 9, trk, sizeof trk) ||
            !mh_io_read(&io, (mh_v2_rd64(r + 0x44) << 9) + sizeof trk, pad, sizeof pad) ||
            !mh_track_sector_valid(trk, sectors))
            DFAIL("Track Table V2 invalida.");
        {
            unsigned k;
            for (k = (unsigned)(base & 0xff); k < sizeof pad; k++)
                if (pad[k]) DFAIL("Padding da Track Table nao esta zerado.");
        }

        /* cheat blob */
        cheat_size = mh_v2_rd32(r + 0x6c);
        if (cheat_size != 0) {
            uint8_t *cb = malloc(cheat_size);
            char e[256];
            if (!cb || !mh_io_read(&io, mh_v2_rd64(r + 0x64) << 9, cb, cheat_size) ||
                !mh_cheat_blob_validate(cb, cheat_size, e, sizeof e) || mh_cheat_blob_scope(cb, cheat_size) != 2) {
                free(cb);
                DFAIL("Bloco de cheats V2 invalido.");
            }
            free(cb);
        }
        aid = mh_v2_rd16(r + 0x62);
        if (aid != 0xffff && aid != i) DFAIL("disc_art_id invalido.");

        /* RAW data */
        c = mh_crc32_begin();
        mh_sha1_init(&sha);
        remaining = sectors;
        {
            uint64_t off = mh_v2_rd64(r + 0x4c) << 9;
            while (remaining != 0) {
                uint32_t n = remaining > RAW_CHUNK ? RAW_CHUNK : remaining, s;
                if (!mh_io_read(&io, off, raw, (size_t)n * MH_V2_SLOT)) DSILENT();
                for (s = 0; s < n; s++) {
                    const uint8_t *sl = raw + (size_t)s * MH_V2_SLOT;
                    unsigned k;
                    c = mh_crc32_update(c, sl, MH_V2_SECTOR);
                    mh_sha1_update(&sha, sl, MH_V2_SECTOR);
                    for (k = MH_V2_SECTOR; k < MH_V2_SLOT; k++)
                        if (sl[k]) DFAIL("Padding dos dados RAW nao esta zerado.");
                }
                raw_done += (uint64_t)n * MH_V2_SLOT;
                remaining -= n;
                off += (uint64_t)n * MH_V2_SLOT;
                if (!mh_progress_report(prog, raw_done, total_raw, "Validando dados RAW V2")) DFAIL("Validacao cancelada.");
                mh_progress_yield();
            }
        }
        dcrc = mh_crc32_end(c);
        mh_sha1_final(&sha, dsha);
        if (mh_v2_rd32(r + 0x7c) != dcrc || memcmp(dsha, r + 0x80, 20) != 0) DFAIL("Hash de um disco V2 nao confere.");
        ndiscs++;
    }
    free(raw);
    raw = NULL;
    free(cases);
    free(discs);
    cases = discs = NULL;
    if (hd.disc_count != ndiscs) return fail(&io, res, "disc_count nao confere.");

    /* menu */
    c = mh_crc32_begin();
    {
        MhSha1 sha;
        uint8_t slot[MH_V2_SLOT], msha[20], cfg[MH_V2_HDR_SIZE], cfgpad[0xd0];
        uint32_t k, mcrc;
        mh_sha1_init(&sha);
        if (hd.menu_sectors - 1u >= 0x5d) {
            mh_io_close(&io);
            return false;
        }
        for (k = 0; k < hd.menu_sectors; k++) {
            unsigned b;
            if (!mh_io_read(&io, (hd.menu_lba << 9) + (uint64_t)k * MH_V2_SLOT, slot, sizeof slot)) {
                mh_io_close(&io);
                return false;
            }
            c = mh_crc32_update(c, slot, MH_V2_SECTOR);
            mh_sha1_update(&sha, slot, MH_V2_SECTOR);
            for (b = MH_V2_SECTOR; b < MH_V2_SLOT; b++)
                if (slot[b]) return fail(&io, res, "Padding do menu V2 nao esta zerado.");
        }
        mcrc = mh_crc32_end(c);
        mh_sha1_final(&sha, msha);
        if (mcrc != hd.crc_menu || memcmp(msha, hd.menu_sha1, 20) != 0) return fail(&io, res, "Menu V2 nao confere.");

        /* global config + music */
        {
            MhV2Config cf;
            bool cfg_ok = mh_io_read(&io, hd.cfg_lba << 9, cfg, sizeof cfg) &&
                          mh_io_read(&io, (hd.cfg_lba << 9) + sizeof cfg, cfgpad, sizeof cfgpad) && mh_v2_cfg_valid(cfg);
            if (!cfg_ok) return fail(&io, res, "Configuracao global do menu V2 invalida.");
            mh_v2_cfg_decode(cfg, &cf);
            if (cf.music_size != 0) {
                uint32_t mc = 0;
                if (cf.music_size > MH_V2_MUSIC_MAX || cf.music_lba == 0 ||
                    !mh_io_crc_region(&io, cf.music_lba << 9, cf.music_size, &mc) || mc != cf.music_crc)
                    return fail(&io, res, "Musica global do menu V2 invalida.");
            }
        }
    }
    mh_io_close(&io);

    /* hash of the whole file */
    {
        uint64_t size = 0;
        uint32_t crc = 0;
        uint8_t sha[20];
        ok = hash_file(path, &size, &crc, sha);
        if (!ok) {
            mh_v2_result_fail(res, "Falha ao calcular hash final da imagem V2.");
            return false;
        }
        if (res) {
            res->ok = 1;
            res->cases = (uint16_t)ncases;
            res->discs = (uint16_t)ndiscs;
            res->size = size;
            res->crc32 = crc;
            memcpy(res->sha1, sha, 20);
            mh_str_copy(res->msg, sizeof res->msg,
                        "PSXRAW V2 valido: headers, Cases, discos, tracks, menu, cheats e hashes conferidos.");
        }
        return true;
    }
}
