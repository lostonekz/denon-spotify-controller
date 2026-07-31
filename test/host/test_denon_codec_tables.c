/*
 * Host test for the Denon PW/MU/SI/MS wire codecs. Runs on the dev machine.
 * No ESP32, no hardware, no network.
 */

#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "denon_codec_tables.h"

void setUp(void) {}
void tearDown(void) {}

/* ------------------------------------------------------------------ */
/* PW */

static void test_pw_parse_valid(void)
{
    denon_power_t v;

    TEST_ASSERT_EQUAL_INT(DENON_TABLE_OK, denon_pw_parse("PWON", &v));
    TEST_ASSERT_EQUAL_INT(DENON_PW_ON, v);

    TEST_ASSERT_EQUAL_INT(DENON_TABLE_OK, denon_pw_parse("PWSTANDBY", &v));
    TEST_ASSERT_EQUAL_INT(DENON_PW_STANDBY, v);
}

static void test_pw_roundtrip_exhaustive(void)
{
    for (int i = DENON_PW_ON; i <= DENON_PW_STANDBY; i++) {
        char buf[DENON_TABLE_BUF_LEN];
        denon_power_t back;

        TEST_ASSERT_EQUAL_INT(DENON_TABLE_OK, denon_pw_format((denon_power_t)i, buf, sizeof(buf)));
        TEST_ASSERT_EQUAL_INT(DENON_TABLE_OK, denon_pw_parse(buf, &back));
        TEST_ASSERT_EQUAL_INT(i, back);
    }
}

static void test_pw_hostile_input(void)
{
    denon_power_t v;

    TEST_ASSERT_EQUAL_INT(DENON_TABLE_MALFORMED, denon_pw_parse(NULL, &v));
    TEST_ASSERT_EQUAL_INT(DENON_TABLE_NOT_MATCHED, denon_pw_parse("", &v));
    TEST_ASSERT_EQUAL_INT(DENON_TABLE_NOT_MATCHED, denon_pw_parse("MUON", &v));
    TEST_ASSERT_EQUAL_INT(DENON_TABLE_MALFORMED, denon_pw_parse("PWNONSENSE", &v));
    TEST_ASSERT_EQUAL_INT(DENON_TABLE_MALFORMED, denon_pw_parse("PW", &v));

    char small[3];
    TEST_ASSERT_EQUAL_INT(DENON_TABLE_MALFORMED, denon_pw_format(DENON_PW_STANDBY, small, sizeof(small)));
    TEST_ASSERT_EQUAL_INT(DENON_TABLE_MALFORMED, denon_pw_format(DENON_PW_ON, NULL, DENON_TABLE_BUF_LEN));
}

/* ------------------------------------------------------------------ */
/* MU */

static void test_mu_parse_valid(void)
{
    denon_mute_t v;

    TEST_ASSERT_EQUAL_INT(DENON_TABLE_OK, denon_mu_parse("MUON", &v));
    TEST_ASSERT_EQUAL_INT(DENON_MU_ON, v);

    TEST_ASSERT_EQUAL_INT(DENON_TABLE_OK, denon_mu_parse("MUOFF", &v));
    TEST_ASSERT_EQUAL_INT(DENON_MU_OFF, v);
}

static void test_mu_roundtrip_exhaustive(void)
{
    for (int i = DENON_MU_ON; i <= DENON_MU_OFF; i++) {
        char buf[DENON_TABLE_BUF_LEN];
        denon_mute_t back;

        TEST_ASSERT_EQUAL_INT(DENON_TABLE_OK, denon_mu_format((denon_mute_t)i, buf, sizeof(buf)));
        TEST_ASSERT_EQUAL_INT(DENON_TABLE_OK, denon_mu_parse(buf, &back));
        TEST_ASSERT_EQUAL_INT(i, back);
    }
}

static void test_mu_hostile_input(void)
{
    denon_mute_t v;

    TEST_ASSERT_EQUAL_INT(DENON_TABLE_MALFORMED, denon_mu_parse(NULL, &v));
    TEST_ASSERT_EQUAL_INT(DENON_TABLE_NOT_MATCHED, denon_mu_parse("PWON", &v));
    TEST_ASSERT_EQUAL_INT(DENON_TABLE_MALFORMED, denon_mu_parse("MUMAYBE", &v));
}

/* ------------------------------------------------------------------ */
/* SI */

static void test_si_parse_valid(void)
{
    denon_input_id_t v;

    TEST_ASSERT_EQUAL_INT(DENON_TABLE_OK, denon_si_parse("SICD", &v));
    TEST_ASSERT_EQUAL_INT(DENON_INPUT_CD, v);

    TEST_ASSERT_EQUAL_INT(DENON_TABLE_OK, denon_si_parse("SISAT/CBL", &v));
    TEST_ASSERT_EQUAL_INT(DENON_INPUT_SAT_CBL, v);

    TEST_ASSERT_EQUAL_INT(DENON_TABLE_OK, denon_si_parse("SIBT", &v));
    TEST_ASSERT_EQUAL_INT(DENON_INPUT_BT, v);
}

static void test_si_roundtrip_exhaustive(void)
{
    for (int i = DENON_INPUT_PHONO; i < DENON_INPUT_COUNT; i++) {
        char buf[DENON_TABLE_BUF_LEN];
        denon_input_id_t back;

        char msg[48];
        snprintf(msg, sizeof(msg), "input id=%d", i);

        TEST_ASSERT_EQUAL_INT_MESSAGE(DENON_TABLE_OK,
            denon_si_format((denon_input_id_t)i, buf, sizeof(buf)), msg);
        TEST_ASSERT_EQUAL_INT_MESSAGE(DENON_TABLE_OK, denon_si_parse(buf, &back), msg);
        TEST_ASSERT_EQUAL_INT_MESSAGE(i, back, msg);
    }
}

static void test_si_hostile_input(void)
{
    denon_input_id_t v;

    TEST_ASSERT_EQUAL_INT(DENON_TABLE_MALFORMED, denon_si_parse(NULL, &v));
    TEST_ASSERT_EQUAL_INT(DENON_TABLE_NOT_MATCHED, denon_si_parse("MUON", &v));
    TEST_ASSERT_EQUAL_INT(DENON_TABLE_MALFORMED, denon_si_parse("SINOTAREALINPUT", &v));

    /* format() with an id past DENON_INPUT_COUNT (e.g. a corrupted
     * denon_state_t.input_id read back from somewhere) must fail closed,
     * not emit garbage onto the wire. */
    char buf[DENON_TABLE_BUF_LEN];
    TEST_ASSERT_EQUAL_INT(DENON_TABLE_MALFORMED,
        denon_si_format((denon_input_id_t)DENON_INPUT_COUNT, buf, sizeof(buf)));
}

/* ------------------------------------------------------------------ */
/* MS */

static void test_ms_parse_valid(void)
{
    denon_listening_mode_id_t v;

    TEST_ASSERT_EQUAL_INT(DENON_TABLE_OK, denon_ms_parse("MSSTEREO", &v));
    TEST_ASSERT_EQUAL_INT(DENON_MODE_STEREO, v);

    /* The literal-space case -- the one thing about this family worth its
     * own explicit test, since it's the part most likely to be mishandled
     * by a parser that assumes commands never contain whitespace. */
    TEST_ASSERT_EQUAL_INT(DENON_TABLE_OK, denon_ms_parse("MSPURE DIRECT", &v));
    TEST_ASSERT_EQUAL_INT(DENON_MODE_PURE_DIRECT, v);
}

static void test_ms_roundtrip_exhaustive(void)
{
    for (int i = DENON_MODE_MOVIE; i < DENON_MODE_COUNT; i++) {
        char buf[DENON_TABLE_BUF_LEN];
        denon_listening_mode_id_t back;

        char msg[48];
        snprintf(msg, sizeof(msg), "mode id=%d", i);

        TEST_ASSERT_EQUAL_INT_MESSAGE(DENON_TABLE_OK,
            denon_ms_format((denon_listening_mode_id_t)i, buf, sizeof(buf)), msg);
        TEST_ASSERT_EQUAL_INT_MESSAGE(DENON_TABLE_OK, denon_ms_parse(buf, &back), msg);
        TEST_ASSERT_EQUAL_INT_MESSAGE(i, back, msg);
    }
}

static void test_ms_hostile_input(void)
{
    denon_listening_mode_id_t v;

    TEST_ASSERT_EQUAL_INT(DENON_TABLE_MALFORMED, denon_ms_parse(NULL, &v));
    TEST_ASSERT_EQUAL_INT(DENON_TABLE_NOT_MATCHED, denon_ms_parse("SICD", &v));
    TEST_ASSERT_EQUAL_INT(DENON_TABLE_MALFORMED, denon_ms_parse("MSNOTAREALMODE", &v));
    /* A prefix of a real code is not the code -- "MSPURE" alone (missing
     * " DIRECT") must not fuzzily match. */
    TEST_ASSERT_EQUAL_INT(DENON_TABLE_MALFORMED, denon_ms_parse("MSPURE", &v));
}

/* ------------------------------------------------------------------ */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pw_parse_valid);
    RUN_TEST(test_pw_roundtrip_exhaustive);
    RUN_TEST(test_pw_hostile_input);
    RUN_TEST(test_mu_parse_valid);
    RUN_TEST(test_mu_roundtrip_exhaustive);
    RUN_TEST(test_mu_hostile_input);
    RUN_TEST(test_si_parse_valid);
    RUN_TEST(test_si_roundtrip_exhaustive);
    RUN_TEST(test_si_hostile_input);
    RUN_TEST(test_ms_parse_valid);
    RUN_TEST(test_ms_roundtrip_exhaustive);
    RUN_TEST(test_ms_hostile_input);
    return UNITY_END();
}
