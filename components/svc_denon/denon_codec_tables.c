/*
 * PW/MU/SI/MS wire codecs. See denon_codec_tables.h for the KNOWN GAP note
 * on SI/MS table completeness.
 *
 * No allocation, no I/O, no ESP-IDF. Host-tested.
 */

#include "denon_codec_tables.h"

#include <string.h>

typedef struct {
    int         id;      /* the enum value, stored as int so one table shape fits all four families */
    const char *code;    /* wire text AFTER the 2-char command prefix ("ON", "PHONO", "DOLBY DIGITAL"...) */
} denon_table_row_t;

static denon_table_status_t table_parse(const char *s, const char *prefix, size_t prefix_len,
                                          const denon_table_row_t *rows, size_t row_count, int *out_id)
{
    if (s == NULL || out_id == NULL) {
        return DENON_TABLE_MALFORMED;
    }
    if (strncmp(s, prefix, prefix_len) != 0) {
        return DENON_TABLE_NOT_MATCHED;
    }

    const char *payload = s + prefix_len;
    for (size_t i = 0; i < row_count; i++) {
        if (strcmp(payload, rows[i].code) == 0) {
            *out_id = rows[i].id;
            return DENON_TABLE_OK;
        }
    }
    /* Has the right prefix but the payload matches nothing known -- this is
     * the "vendor field drift" case docs/RISKS.md R5 names explicitly: a
     * changed/updated device sending a code this table doesn't have. */
    return DENON_TABLE_MALFORMED;
}

static denon_table_status_t table_format(int id, const char *prefix,
                                           const denon_table_row_t *rows, size_t row_count,
                                           char *buf, size_t buflen)
{
    if (buf == NULL) {
        return DENON_TABLE_MALFORMED;
    }
    for (size_t i = 0; i < row_count; i++) {
        if (rows[i].id == id) {
            size_t prefix_len = strlen(prefix);
            size_t code_len   = strlen(rows[i].code);
            if (buflen < prefix_len + code_len + 1) {
                return DENON_TABLE_MALFORMED;
            }
            memcpy(buf, prefix, prefix_len);
            memcpy(buf + prefix_len, rows[i].code, code_len + 1);   /* + NUL */
            return DENON_TABLE_OK;
        }
    }
    return DENON_TABLE_MALFORMED;   /* id not in this table -- caller passed an invalid enum value */
}

/* --- PW: power. High confidence -- unchanged across a decade-plus of Denon/Marantz models. --- */

static const denon_table_row_t PW_ROWS[] = {
    { DENON_PW_ON,       "ON" },
    { DENON_PW_STANDBY,  "STANDBY" },
};

denon_table_status_t denon_pw_parse(const char *s, denon_power_t *out)
{
    int id = 0;
    denon_table_status_t st = table_parse(s, "PW", 2, PW_ROWS,
                                            sizeof(PW_ROWS) / sizeof(PW_ROWS[0]), &id);
    if (st == DENON_TABLE_OK) {
        *out = (denon_power_t)id;
    }
    return st;
}

denon_table_status_t denon_pw_format(denon_power_t v, char *buf, size_t buflen)
{
    return table_format((int)v, "PW", PW_ROWS, sizeof(PW_ROWS) / sizeof(PW_ROWS[0]), buf, buflen);
}

/* --- MU: mute. High confidence -- same universality as PW. --- */

static const denon_table_row_t MU_ROWS[] = {
    { DENON_MU_ON,   "ON" },
    { DENON_MU_OFF,  "OFF" },
};

denon_table_status_t denon_mu_parse(const char *s, denon_mute_t *out)
{
    int id = 0;
    denon_table_status_t st = table_parse(s, "MU", 2, MU_ROWS,
                                            sizeof(MU_ROWS) / sizeof(MU_ROWS[0]), &id);
    if (st == DENON_TABLE_OK) {
        *out = (denon_mute_t)id;
    }
    return st;
}

denon_table_status_t denon_mu_format(denon_mute_t v, char *buf, size_t buflen)
{
    return table_format((int)v, "MU", MU_ROWS, sizeof(MU_ROWS) / sizeof(MU_ROWS[0]), buf, buflen);
}

/* --- SI: input select. See the KNOWN GAP note in denon_codec_tables.h. --- */

static const denon_table_row_t SI_ROWS[] = {
    { DENON_INPUT_PHONO,    "PHONO" },
    { DENON_INPUT_CD,       "CD" },
    { DENON_INPUT_TUNER,    "TUNER" },
    { DENON_INPUT_DVD,      "DVD" },
    { DENON_INPUT_BD,       "BD" },
    { DENON_INPUT_TV,       "TV" },
    { DENON_INPUT_SAT_CBL,  "SAT/CBL" },
    { DENON_INPUT_MPLAY,    "MPLAY" },
    { DENON_INPUT_GAME,     "GAME" },
    { DENON_INPUT_AUX1,     "AUX1" },
    { DENON_INPUT_NET,      "NET" },
    { DENON_INPUT_BT,       "BT" },
};

denon_table_status_t denon_si_parse(const char *s, denon_input_id_t *out)
{
    int id = 0;
    denon_table_status_t st = table_parse(s, "SI", 2, SI_ROWS,
                                            sizeof(SI_ROWS) / sizeof(SI_ROWS[0]), &id);
    if (st == DENON_TABLE_OK) {
        *out = (denon_input_id_t)id;
    }
    return st;
}

denon_table_status_t denon_si_format(denon_input_id_t v, char *buf, size_t buflen)
{
    return table_format((int)v, "SI", SI_ROWS, sizeof(SI_ROWS) / sizeof(SI_ROWS[0]), buf, buflen);
}

/* --- MS: listening mode. See the KNOWN GAP note in denon_codec_tables.h.
 * Some codes contain a literal space -- unusual but real Denon wire text. --- */

static const denon_table_row_t MS_ROWS[] = {
    { DENON_MODE_MOVIE,        "MOVIE" },
    { DENON_MODE_MUSIC,        "MUSIC" },
    { DENON_MODE_GAME,         "GAME" },
    { DENON_MODE_DIRECT,       "DIRECT" },
    { DENON_MODE_PURE_DIRECT,  "PURE DIRECT" },
    { DENON_MODE_STEREO,       "STEREO" },
    { DENON_MODE_STANDARD,     "STANDARD" },
};

denon_table_status_t denon_ms_parse(const char *s, denon_listening_mode_id_t *out)
{
    int id = 0;
    denon_table_status_t st = table_parse(s, "MS", 2, MS_ROWS,
                                            sizeof(MS_ROWS) / sizeof(MS_ROWS[0]), &id);
    if (st == DENON_TABLE_OK) {
        *out = (denon_listening_mode_id_t)id;
    }
    return st;
}

denon_table_status_t denon_ms_format(denon_listening_mode_id_t v, char *buf, size_t buflen)
{
    return table_format((int)v, "MS", MS_ROWS, sizeof(MS_ROWS) / sizeof(MS_ROWS[0]), buf, buflen);
}
