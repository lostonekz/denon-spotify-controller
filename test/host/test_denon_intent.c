/*
 * Host test for denon_intent_to_cmd(): the intent -> outgoing wire command
 * path, including docs/ROADMAP.md Phase 3's own exit criterion -- "the
 * safety clamp holds under a fuzzed intent stream." Runs on the dev
 * machine. No ESP32.
 */

#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "denon_codec.h"
#include "denon_codec_tables.h"
#include "denon_intent.h"

void setUp(void) {}
void tearDown(void) {}

#define DEFAULT_LIMIT 140   /* 70.0 dB */

static intent_t build_intent(intent_domain_t domain, uint16_t kind, const denon_intent_payload_t *payload)
{
    intent_t intent;
    memset(&intent, 0, sizeof(intent));
    intent.domain = domain;
    intent.kind   = kind;
    memcpy(intent.payload, payload, sizeof(*payload));
    return intent;
}

static void test_wrong_domain_rejected(void)
{
    denon_intent_payload_t p = { .power_on = true };
    intent_t intent = build_intent(INTENT_DOMAIN_PLAYBACK, DENON_INTENT_SET_POWER, &p);

    char cmd[DENON_CMDQ_CMD_BUF_LEN];
    bool is_absolute;
    denon_cmd_absolute_class_t cls;
    denon_intent_result_t r = denon_intent_to_cmd(&intent, DEFAULT_LIMIT, cmd, sizeof(cmd), &is_absolute, &cls);

    TEST_ASSERT_EQUAL_INT(DENON_INTENT_RESULT_WRONG_DOMAIN, r);
}

static void test_unknown_kind_rejected(void)
{
    denon_intent_payload_t p = { 0 };
    intent_t intent = build_intent(INTENT_DOMAIN_DENON, 99, &p);

    char cmd[DENON_CMDQ_CMD_BUF_LEN];
    bool is_absolute;
    denon_cmd_absolute_class_t cls;
    denon_intent_result_t r = denon_intent_to_cmd(&intent, DEFAULT_LIMIT, cmd, sizeof(cmd), &is_absolute, &cls);

    TEST_ASSERT_EQUAL_INT(DENON_INTENT_RESULT_UNKNOWN_KIND, r);
}

static void test_set_power(void)
{
    denon_intent_payload_t p_on  = { .power_on = true };
    denon_intent_payload_t p_off = { .power_on = false };
    intent_t i_on  = build_intent(INTENT_DOMAIN_DENON, DENON_INTENT_SET_POWER, &p_on);
    intent_t i_off = build_intent(INTENT_DOMAIN_DENON, DENON_INTENT_SET_POWER, &p_off);

    char cmd[DENON_CMDQ_CMD_BUF_LEN];
    bool is_absolute;
    denon_cmd_absolute_class_t cls;

    TEST_ASSERT_EQUAL_INT(DENON_INTENT_RESULT_OK,
        denon_intent_to_cmd(&i_on, DEFAULT_LIMIT, cmd, sizeof(cmd), &is_absolute, &cls));
    TEST_ASSERT_EQUAL_STRING("PWON", cmd);
    TEST_ASSERT_FALSE_MESSAGE(is_absolute, "power must never be coalesced");

    TEST_ASSERT_EQUAL_INT(DENON_INTENT_RESULT_OK,
        denon_intent_to_cmd(&i_off, DEFAULT_LIMIT, cmd, sizeof(cmd), &is_absolute, &cls));
    TEST_ASSERT_EQUAL_STRING("PWSTANDBY", cmd);
}

static void test_set_mute(void)
{
    denon_intent_payload_t p = { .muted = true };
    intent_t intent = build_intent(INTENT_DOMAIN_DENON, DENON_INTENT_SET_MUTE, &p);

    char cmd[DENON_CMDQ_CMD_BUF_LEN];
    bool is_absolute;
    denon_cmd_absolute_class_t cls;

    TEST_ASSERT_EQUAL_INT(DENON_INTENT_RESULT_OK,
        denon_intent_to_cmd(&intent, DEFAULT_LIMIT, cmd, sizeof(cmd), &is_absolute, &cls));
    TEST_ASSERT_EQUAL_STRING("MUON", cmd);
    TEST_ASSERT_FALSE(is_absolute);
}

static void test_set_input(void)
{
    denon_intent_payload_t p = { .input_id = DENON_INPUT_CD };
    intent_t intent = build_intent(INTENT_DOMAIN_DENON, DENON_INTENT_SET_INPUT, &p);

    char cmd[DENON_CMDQ_CMD_BUF_LEN];
    bool is_absolute;
    denon_cmd_absolute_class_t cls;

    TEST_ASSERT_EQUAL_INT(DENON_INTENT_RESULT_OK,
        denon_intent_to_cmd(&intent, DEFAULT_LIMIT, cmd, sizeof(cmd), &is_absolute, &cls));
    TEST_ASSERT_EQUAL_STRING("SICD", cmd);
    TEST_ASSERT_FALSE(is_absolute);
}

static void test_set_input_invalid_id_rejected(void)
{
    denon_intent_payload_t p = { .input_id = (uint8_t)DENON_INPUT_COUNT };
    intent_t intent = build_intent(INTENT_DOMAIN_DENON, DENON_INTENT_SET_INPUT, &p);

    char cmd[DENON_CMDQ_CMD_BUF_LEN];
    bool is_absolute;
    denon_cmd_absolute_class_t cls;

    TEST_ASSERT_EQUAL_INT(DENON_INTENT_RESULT_UNKNOWN_KIND,
        denon_intent_to_cmd(&intent, DEFAULT_LIMIT, cmd, sizeof(cmd), &is_absolute, &cls));
}

static void test_set_listening_mode(void)
{
    denon_intent_payload_t p = { .listening_mode_id = DENON_MODE_PURE_DIRECT };
    intent_t intent = build_intent(INTENT_DOMAIN_DENON, DENON_INTENT_SET_LISTENING_MODE, &p);

    char cmd[DENON_CMDQ_CMD_BUF_LEN];
    bool is_absolute;
    denon_cmd_absolute_class_t cls;

    TEST_ASSERT_EQUAL_INT(DENON_INTENT_RESULT_OK,
        denon_intent_to_cmd(&intent, DEFAULT_LIMIT, cmd, sizeof(cmd), &is_absolute, &cls));
    TEST_ASSERT_EQUAL_STRING("MSPURE DIRECT", cmd);
    TEST_ASSERT_FALSE(is_absolute);
}

static void test_set_volume_under_limit_passes_through(void)
{
    denon_intent_payload_t p = { .volume_half_db = 101 };   /* 50.5 dB -- exercises the half-step digit too */
    intent_t intent = build_intent(INTENT_DOMAIN_DENON, DENON_INTENT_SET_VOLUME, &p);

    char cmd[DENON_CMDQ_CMD_BUF_LEN];
    bool is_absolute;
    denon_cmd_absolute_class_t cls;

    TEST_ASSERT_EQUAL_INT(DENON_INTENT_RESULT_OK,
        denon_intent_to_cmd(&intent, DEFAULT_LIMIT, cmd, sizeof(cmd), &is_absolute, &cls));
    TEST_ASSERT_EQUAL_STRING("MV505", cmd);
    TEST_ASSERT_TRUE_MESSAGE(is_absolute, "volume must coalesce, not queue every drag step");
    TEST_ASSERT_EQUAL_INT(DENON_CMD_CLASS_MV, cls);
}

/* The R13 case, exercised through the real intent path, not just the
 * codec's own unit test: a request at the protocol's own maximum (98.5 dB)
 * must still come out clamped to the configured limit. */
static void test_set_volume_over_limit_clamped(void)
{
    denon_intent_payload_t p = { .volume_half_db = DENON_MV_HALF_DB_MAX };
    intent_t intent = build_intent(INTENT_DOMAIN_DENON, DENON_INTENT_SET_VOLUME, &p);

    char cmd[DENON_CMDQ_CMD_BUF_LEN];
    bool is_absolute;
    denon_cmd_absolute_class_t cls;

    TEST_ASSERT_EQUAL_INT(DENON_INTENT_RESULT_OK,
        denon_intent_to_cmd(&intent, DEFAULT_LIMIT, cmd, sizeof(cmd), &is_absolute, &cls));

    uint16_t parsed = 0xFFFF;
    TEST_ASSERT_EQUAL_INT(DENON_CODEC_OK, denon_mv_parse(cmd, &parsed));
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(DEFAULT_LIMIT, parsed, "clamp must hold even at the protocol's own max");
}

/* Exit criterion, docs/ROADMAP.md Phase 3: "the safety clamp holds under a
 * fuzzed intent stream." A deterministic LCG (no OS rand() dependency,
 * reproducible across runs) drives thousands of SET_VOLUME intents across
 * the full uint16_t range -- including values with no possible legitimate
 * origin (a coalescing bug, a corrupted payload) -- through the real
 * intent path, asserting every single resulting command decodes to a
 * value at or below the limit. */
static void test_fuzzed_volume_intent_stream_never_exceeds_limit(void)
{
    static const uint16_t limits[] = { 0, 1, 70, 140, DENON_MV_HALF_DB_MAX };
    uint32_t lcg_state = 0x2A2A2A2Au;

    for (size_t li = 0; li < sizeof(limits) / sizeof(limits[0]); li++) {
        uint16_t limit = limits[li];

        for (int i = 0; i < 5000; i++) {
            lcg_state = lcg_state * 1664525u + 1013904223u;   /* Numerical Recipes LCG */
            uint16_t requested = (uint16_t)(lcg_state >> 16);   /* full uint16_t range, including >MAX */

            denon_intent_payload_t p = { .volume_half_db = requested };
            intent_t intent = build_intent(INTENT_DOMAIN_DENON, DENON_INTENT_SET_VOLUME, &p);

            char cmd[DENON_CMDQ_CMD_BUF_LEN];
            bool is_absolute;
            denon_cmd_absolute_class_t cls;
            denon_intent_result_t r =
                denon_intent_to_cmd(&intent, limit, cmd, sizeof(cmd), &is_absolute, &cls);

            char msg[64];
            snprintf(msg, sizeof(msg), "requested=%u limit=%u", requested, limit);
            TEST_ASSERT_EQUAL_INT_MESSAGE(DENON_INTENT_RESULT_OK, r, msg);

            uint16_t parsed = 0xFFFF;
            TEST_ASSERT_EQUAL_INT_MESSAGE(DENON_CODEC_OK, denon_mv_parse(cmd, &parsed), msg);
            uint16_t effective_limit = limit < DENON_MV_HALF_DB_MAX ? limit : DENON_MV_HALF_DB_MAX;
            TEST_ASSERT_TRUE_MESSAGE(parsed <= effective_limit, msg);
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_wrong_domain_rejected);
    RUN_TEST(test_unknown_kind_rejected);
    RUN_TEST(test_set_power);
    RUN_TEST(test_set_mute);
    RUN_TEST(test_set_input);
    RUN_TEST(test_set_input_invalid_id_rejected);
    RUN_TEST(test_set_listening_mode);
    RUN_TEST(test_set_volume_under_limit_passes_through);
    RUN_TEST(test_set_volume_over_limit_clamped);
    RUN_TEST(test_fuzzed_volume_intent_stream_never_exceeds_limit);
    return UNITY_END();
}
