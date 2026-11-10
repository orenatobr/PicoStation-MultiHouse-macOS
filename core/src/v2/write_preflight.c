/* Readiness checks before a write and the texts of the write commands (orig: preflight_serials,
 * serial_preflight_error, v2_prepare_write, all_ready, cmd_write, write_failure). */
#include "write_internal.h"

/* orig: serial_preflight_error @0x140030000 */
bool mh_v2_serial_preflight_error(MhV2Result *res, const MhCase *c, int disc_index,
                                  const MhDisc *d, const char *msg)
{
    MhWbuf w;
    mh_wbuf_init(&w);
    mh_wbuf_puts(&w, "Gravacao nao iniciada. Case: ");
    mh_wbuf_puts(&w, c->title);
    mh_wbuf_puts(&w, "\r\nDisco ");
    mh_wbuf_u32(&w, (uint32_t)(disc_index + 1));
    mh_wbuf_puts(&w, ": ");
    if (!d) {
        mh_wbuf_puts(&w, "\r\n");
        mh_wbuf_puts(&w, msg);
    } else {
        char base[MH_PATH_MAX];
        mh_wbuf_puts(&w, d->name);
        mh_wbuf_puts(&w, "\r\n");
        mh_wbuf_puts(&w, msg);
        mh_path_basename(d->nfiles != 0 ? d->files[0].path : d->source_path, base, sizeof base);
        mh_wbuf_puts(&w, "\r\nArquivo: ");
        mh_wbuf_puts(&w, base);
    }
    if (res) {
        res->ok = 0;
        mh_str_copy(res->msg, MH_V2W_MSG_MAX,
                    w.data ? w.data : "Identificacao nao resolvida; gravacao nao iniciada.");
    }
    mh_wbuf_free(&w);
    return false;
}

/* orig: preflight_serials.constprop.0 @0x1400260a0 (the original unrolled the five discs) */
bool mh_v2_preflight_serials(const MhProject *p, MhV2Result *res)
{
    unsigned ci, j;
    for (ci = 0; ci < MH_PROJECT_MAX_CASES; ci++) {
        const MhCase *c = &p->cases[ci];
        if (!c->in_use) continue;
        if (c->disc_count - 1u > 4u)
            return mh_v2_serial_preflight_error(res, c, 0, NULL, "Quantidade de discos invalida.");
        for (j = 0; j < c->disc_count; j++) {
            const MhDisc *d = c->discs[j];
            if (!d) return mh_v2_serial_preflight_error(res, c, (int)j, NULL, "Disco ausente.");
            if (d->kind == MH_DISC_KIND_CUE && d->verified == 0 && d->verified_aux == 0)
                return mh_v2_serial_preflight_error(res, c, (int)j, d,
                                                    "Este CUE/BIN ainda nao foi preparado ao ser adicionado.");
            if (d->verified != 0) {
                if (d->verify_issues & 0x20)
                    return mh_v2_serial_preflight_error(
                        res, c, (int)j, d,
                        "Padding RAW invalido; restaure a partir do CUE/BIN para manter o formato RAW.");
                if ((~d->verify_accepted & d->verify_issues) != 0)
                    return mh_v2_serial_preflight_error(
                        res, c, (int)j, d,
                        "As observacoes deste disco ainda nao foram aceitas na inclusao ou verificacao manual.");
            }
        }
    }
    return true;
}

/* orig: v2_prepare_write @0x140038760 (the original forwards its fourth argument, the result) */
bool mh_v2_prepare_write(const MhProject *p, MhV2Result *res) { return mh_v2_preflight_serials(p, res); }

/* orig: all_ready @0x1400018a0 */
bool mh_v2_all_ready(const MhProject *p, bool *has_observations, char *msg, size_t msgn)
{
    unsigned ci, j;
    if (has_observations) *has_observations = false;
    if (msg && msgn) msg[0] = 0;
    if (p->disc_count == 0) {
        mh_str_copy(msg, msgn, MH_V2W_NEED_GAME);
        return false;
    }
    for (ci = 0; ci < MH_PROJECT_MAX_CASES; ci++) {
        const MhCase *c = &p->cases[ci];
        if (!c->in_use || c->disc_count == 0) continue;
        for (j = 0; j < c->disc_count; j++) {
            const MhDisc *d = c->discs[j];
            MhWbuf w;
            mh_wbuf_init(&w);
            if (d && d->status != MH_DISC_STATUS_FIXABLE && d->status != MH_DISC_STATUS_ERROR) {
                if (d->status == MH_DISC_STATUS_NOTE && has_observations) *has_observations = true;
                continue;
            }
            mh_wbuf_puts(&w, "Gravacao nao iniciada. Case: ");
            mh_wbuf_puts(&w, c->title);
            mh_wbuf_puts(&w, "\r\nDisco ");
            mh_wbuf_u32(&w, j + 1);
            if (d) {
                char base[MH_PATH_MAX];
                mh_wbuf_puts(&w, "\r\n");
                mh_wbuf_puts(&w, d->notes);
                if (d->verify_text[0]) {
                    mh_wbuf_puts(&w, "\r\n\r\nUltima verificacao: ");
                    mh_wbuf_puts(&w, d->verify_text);
                }
                mh_path_basename(d->cue_path, base, sizeof base);
                mh_wbuf_puts(&w, "\r\nCUE: ");
                mh_wbuf_puts(&w, base);
            }
            mh_str_copy(msg, msgn, w.data ? w.data : "Estrutura do disco incompleta.");
            mh_wbuf_free(&w);
            return false;
        }
    }
    return true;
}

/* orig: cmd_write @0x140011980 / write_image_direct_to.isra.0 @0x14003f880: the first two tests */
const char *mh_v2_blocked_text(bool db_ok, bool cheats_ok)
{
    if (!db_ok) return MH_V2W_BLOCKED_DB;
    if (!cheats_ok) return MH_V2W_BLOCKED_CHEATS;
    return NULL;
}

/* orig: cmd_write @0x140011980, the checks before the confirmation window */
const char *mh_v2_write_precheck(const MhProject *p, bool db_ok, bool cheats_ok, bool has_disk,
                                 uint32_t disk_number, uint64_t disk_size, uint64_t *estimate)
{
    uint64_t est;
    const char *blocked = mh_v2_blocked_text(db_ok, cheats_ok);
    if (estimate) *estimate = 0;
    if (blocked) return blocked;
    if (!has_disk) return MH_V2W_NEED_SD;
    if (p->card_loaded != 0 && p->device != (int32_t)disk_number) return MH_V2W_OTHER_CARD;
    /* An estimate of 0 (overlapping preserved extents, no room) passes this test, as in the
     * original; sd_write_project_direct refuses it later. */
    est = mh_v2_estimate_project_bytes_for_target(p, (int32_t)disk_number, NULL);
    if (estimate) *estimate = est;
    if (est > disk_size) return MH_V2W_NO_FIT;
    return NULL;
}

void mh_v2_write_confirm_text(const MhProject *p, const char *drive, const char *model,
                              const char *serial, uint64_t estimate, char *out, size_t outn)
{
    MhWbuf w;
    mh_wbuf_init(&w);
    mh_wbuf_puts(&w, p->card_loaded != 0
                         ? "As alteracoes serao aplicadas diretamente no cartao PSXRAW V2. Os dados RAW dos jogos preservados permanecerao nos mesmos LBAs.\r\n\r\n"
                         : "O cartao inteiro sera preparado diretamente como PSXRAW V2.\r\n\r\n");
    mh_wbuf_puts(&w, drive);
    mh_wbuf_puts(&w, "\r\nModelo: ");
    mh_wbuf_puts(&w, model);
    mh_wbuf_puts(&w, "\r\nSerial: ");
    mh_wbuf_puts(&w, serial && serial[0] ? serial : "nao informado");
    mh_wbuf_puts(&w, "\r\nCases: ");
    mh_wbuf_u32(&w, p->case_count);
    mh_wbuf_puts(&w, "  |  Discos: ");
    mh_wbuf_u32(&w, p->disc_count);
    mh_wbuf_puts(&w, "\r\nEspaco RAW final aproximado: ");
    mh_wbuf_size(&w, estimate);
    mh_wbuf_puts(&w, "\r\nDiscos novos preparados na inclusao; discos ja gravados serao preservados.");
    mh_wbuf_puts(&w, "\r\n\r\nATENCAO: escolher o PhysicalDrive errado pode apagar outro dispositivo.");
    mh_str_copy(out, outn, w.data ? w.data : "");
    mh_wbuf_free(&w);
}

void mh_v2_write_done_text(uint16_t cases, uint16_t discs, uint64_t size, char *out, size_t outn)
{
    MhWbuf w;
    mh_wbuf_init(&w);
    mh_wbuf_puts(&w, "Operacao concluida diretamente no SD.\r\n\r\nCases: ");
    mh_wbuf_u32(&w, cases);
    mh_wbuf_puts(&w, "\r\nDiscos: ");
    mh_wbuf_u32(&w, discs);
    mh_wbuf_puts(&w, "\r\nTamanho RAW utilizado: ");
    mh_wbuf_size(&w, size);
    mh_wbuf_puts(&w, "\r\n\r\nO cartao e PSXRAW e nao possui arquivos visiveis no Windows. Nao formate o cartao.");
    mh_str_copy(out, outn, w.data ? w.data : "");
    mh_wbuf_free(&w);
}

/* orig: write_failure @0x14003f7c0 */
void mh_v2_write_failure_text(const char *msg, bool cancelled, char *out, size_t outn)
{
    MhWbuf w;
    if (cancelled) {
        mh_str_copy(out, outn,
                    "Cancelado. Mantenha o Manager aberto e reaplique para retomar os discos pendentes.");
        return;
    }
    mh_wbuf_init(&w);
    mh_wbuf_puts(&w, msg);
    mh_wbuf_puts(&w, "\r\n\r\nMantenha o Manager aberto. Reconecte o mesmo destino e reaplique: os discos azuis ja concluidos serao aproveitados. A biblioteca precisa ser finalizada antes do uso no console.");
    mh_str_copy(out, outn, w.data ? w.data : "");
    mh_wbuf_free(&w);
}
