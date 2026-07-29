/*
 * Host test for the Denon MV codec.
 *
 * The vector table is GENERATED from denon_mv_vectors.csv, which is also
 * validated by an independently-written Python implementation
 * (tools/denon_mv_oracle.py). Two implementations, one table. A wrong expected
 * value has to be wrong identically in C and in Python to slip through.
 *
 * Runs on the dev machine. No ESP32, no hardware, no network.
 */

#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "denon_codec.h"

#include "denon_mv_vectors.inc"

void setUp(void) {}
void tearDown(void) {}

/* ------------------------------------------------------------------ */

static void test_mv_parse_table(void)
{
    for (size_t i = 0; i < MV_PARSE_COUNT; i++) {
        const mv_vector_t *v = &MV_PARSE[i];
        uint16_t out = 0xFFFF;

        denon_codec_status_t st = denon_mv_parse(v->input_s, &out);

        char msg[160];
        snprintf(msg, sizeof(msg), "parse(\"%s\") [%s]", v->input_s, v->note);
        TEST_ASSERT_EQUAL_INT_MESSAGE(v->status, st, msg);

        if (v->status == DENON_CODEC_OK || v->status == DENON_CODEC_MAX_REPORT) {
            TEST_ASSERT_EQUAL_UINT16_MESSAGE(v->expect_v, out, msg);
        } else {
            /* Contract: *out is untouched on failure. Catches a parser that
             * writes a partial result before deciding the input is bad. */
            TEST_ASSERT_EQUAL_UINT16_MESSAGE(0xFFFF, out, msg);
        }
    }
}

static void test_mv_format_table(void)
{
    for (size_t i = 0; i < MV_FORMAT_COUNT; i++) {
        const mv_vector_t *v = &MV_FORMAT[i];
        char buf[DENON_MV_BUF_LEN];
        memset(buf, 0x7F, sizeof(buf));

        denon_codec_status_t st =
            denon_mv_format((uint16_t)v->input_v, buf, sizeof(buf));

        char msg[160];
        snprintf(msg, sizeof(msg), "format(%d) [%s]", v->input_v, v->note);
        TEST_ASSERT_EQUAL_INT_MESSAGE(v->status, st, msg);

        if (v->status == DENON_CODEC_OK) {
            TEST_ASSERT_EQUAL_STRING_MESSAGE(v->expect_s, buf, msg);
        }
    }
}

static void test_mv_roundtrip_table(void)
{
    for (size_t i = 0; i < MV_ROUNDTRIP_COUNT; i++) {
        const mv_vector_t *v = &MV_ROUNDTRIP[i];
        char buf[DENON_MV_BUF_LEN];
        uint16_t back = 0xFFFF;

        TEST_ASSERT_EQUAL_INT(DENON_CODEC_OK,
            denon_mv_format((uint16_t)v->input_v, buf, sizeof(buf)));
        TEST_ASSERT_EQUAL_INT(DENON_CODEC_OK, denon_mv_parse(buf, &back));
        TEST_ASSERT_EQUAL_UINT16(v->input_v, back);
    }
}

/* Exhaustive: every legal half-dB value survives format -> parse unchanged.
 * 198 values is cheap enough that sampling would be a false economy. */
static void test_mv_roundtrip_exhaustive(void)
{
    for (uint16_t v = DENON_MV_HALF_DB_MIN; v <= DENON_MV_HALF_DB_MAX; v++) {
        char buf[DENON_MV_BUF_LEN];
        uint16_t back = 0xFFFF;

        char msg[80];
        snprintf(msg, sizeof(msg), "exhaustive roundtrip half_db=%u", v);

        TEST_ASSERT_EQUAL_INT_MESSAGE(DENON_CODEC_OK,
            denon_mv_format(v, buf, sizeof(buf)), msg);
        TEST_ASSERT_EQUAL_INT_MESSAGE(DENON_CODEC_OK,
            denon_mv_parse(buf, &back), msg);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(v, back, msg);
    }
}

/* Defensive: the codec must never read past the NUL or crash on junk. */
static void test_mv_parse_hostile_input(void)
{
    uint16_t out = 0;

    TEST_ASSERT_EQUAL_INT(DENON_CODEC_MALFORMED, denon_mv_parse(NULL, &out));
    TEST_ASSERT_EQUAL_INT(DENON_CODEC_MALFORMED, denon_mv_parse("MV52", NULL));
    TEST_ASSERT_EQUAL_INT(DENON_CODEC_NOT_MV,    denon_mv_parse("", &out));
    TEST_ASSERT_EQUAL_INT(DENON_CODEC_NOT_MV,    denon_mv_parse("M", &out));
    TEST_ASSERT_EQUAL_INT(DENON_CODEC_MALFORMED, denon_mv_parse("MV", &out));

    /* A long run of digits must be rejected on digit count, not overflow. */
    TEST_ASSERT_EQUAL_INT(DENON_CODEC_MALFORMED,
                          denon_mv_parse("MV999999999999", &out));
}

static void test_mv_format_rejects_small_buffer(void)
{
    char small[3];
    TEST_ASSERT_EQUAL_INT(DENON_CODEC_MALFORMED,
                          denon_mv_format(104, small, sizeof(small)));
    TEST_ASSERT_EQUAL_INT(DENON_CODEC_MALFORMED,
                          denon_mv_format(104, NULL, DENON_MV_BUF_LEN));
}

static void test_mv_display(void)
{
    char buf[8];

    denon_mv_to_display(0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("0.0", buf);

    denon_mv_to_display(105, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("52.5", buf);

    denon_mv_to_display(196, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("98.0", buf);
}

/* ------------------------------------------------------------------ */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mv_parse_table);
    RUN_TEST(test_mv_format_table);
    RUN_TEST(test_mv_roundtrip_table);
    RUN_TEST(test_mv_roundtrip_exhaustive);
    RUN_TEST(test_mv_parse_hostile_input);
    RUN_TEST(test_mv_format_rejects_small_buffer);
    RUN_TEST(test_mv_display);
    return UNITY_END();
}
