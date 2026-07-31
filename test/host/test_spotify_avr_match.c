/*
 * Host test for spotify_avr_match: every case found across three review
 * passes on this project's Phase 4 plan -- id match, unique name-match
 * self-heal, old-id-still-present ambiguity, multiple-name-match
 * ambiguity, null active id, and nothing pinned/active.
 */

#include "unity.h"

#include "spotify_avr_match.h"

void setUp(void) {}
void tearDown(void) {}

static void test_id_match_is_yes_no_self_heal(void)
{
    spotify_avr_match_outcome_t out = spotify_avr_match_resolve(
        "dev-1", "Denon AVR-X3800H", "dev-1", "Denon AVR-X3800H", NULL, 0);
    TEST_ASSERT_EQUAL(SPOTIFY_AVR_MATCH_YES, out.result);
    TEST_ASSERT_NULL(out.self_heal_device_id);
}

static void test_id_match_ignores_name_difference(void)
{
    /* id is the fast path -- doesn't even need the name to agree. */
    spotify_avr_match_outcome_t out = spotify_avr_match_resolve(
        "dev-1", "Some Renamed Speaker", "dev-1", "Denon AVR-X3800H", NULL, 0);
    TEST_ASSERT_EQUAL(SPOTIFY_AVR_MATCH_YES, out.result);
}

static void test_unique_name_match_with_old_id_gone_self_heals(void)
{
    spotify_device_ref_t devices[] = {
        { "dev-2-new", "Denon AVR-X3800H" },
        { "dev-3", "Kitchen Speaker" },
    };
    spotify_avr_match_outcome_t out = spotify_avr_match_resolve(
        "dev-2-new", "Denon AVR-X3800H", "dev-2-old", "Denon AVR-X3800H",
        devices, 2);
    TEST_ASSERT_EQUAL(SPOTIFY_AVR_MATCH_YES, out.result);
    TEST_ASSERT_NOT_NULL(out.self_heal_device_id);
    TEST_ASSERT_EQUAL_STRING("dev-2-new", out.self_heal_device_id);
}

static void test_name_match_case_insensitive(void)
{
    spotify_device_ref_t devices[] = {
        { "dev-2-new", "denon avr-x3800h" },
    };
    spotify_avr_match_outcome_t out = spotify_avr_match_resolve(
        "dev-2-new", "DENON AVR-X3800H", "dev-2-old", "Denon AVR-X3800H",
        devices, 1);
    TEST_ASSERT_EQUAL(SPOTIFY_AVR_MATCH_YES, out.result);
}

static void test_old_id_still_present_is_ambiguous(void)
{
    /* The old pinned id shows up elsewhere in the list -- it might still be
     * a real, distinct device. Do not self-heal. */
    spotify_device_ref_t devices[] = {
        { "dev-2-old", "Some Other Device" },
        { "dev-2-new", "Denon AVR-X3800H" },
    };
    spotify_avr_match_outcome_t out = spotify_avr_match_resolve(
        "dev-2-new", "Denon AVR-X3800H", "dev-2-old", "Denon AVR-X3800H",
        devices, 2);
    TEST_ASSERT_EQUAL(SPOTIFY_AVR_MATCH_UNKNOWN, out.result);
    TEST_ASSERT_NULL(out.self_heal_device_id);
}

static void test_multiple_name_matches_is_ambiguous(void)
{
    spotify_device_ref_t devices[] = {
        { "dev-a", "Speaker" },
        { "dev-b", "Speaker" },
    };
    spotify_avr_match_outcome_t out = spotify_avr_match_resolve(
        "dev-a", "Speaker", "dev-old", "Speaker", devices, 2);
    TEST_ASSERT_EQUAL(SPOTIFY_AVR_MATCH_UNKNOWN, out.result);
}

static void test_name_match_without_device_list_is_unknown(void)
{
    /* Can't disambiguate uniqueness without the full list -- conservative. */
    spotify_avr_match_outcome_t out = spotify_avr_match_resolve(
        "dev-2-new", "Denon AVR-X3800H", "dev-2-old", "Denon AVR-X3800H",
        NULL, 0);
    TEST_ASSERT_EQUAL(SPOTIFY_AVR_MATCH_UNKNOWN, out.result);
}

static void test_null_active_id_falls_back_to_name(void)
{
    /* Spotify's docs allow a null/absent device id in some restricted-
     * session responses. */
    spotify_device_ref_t devices[] = {
        { NULL, "Denon AVR-X3800H" },
    };
    spotify_avr_match_outcome_t out = spotify_avr_match_resolve(
        NULL, "Denon AVR-X3800H", "dev-old", "Denon AVR-X3800H", devices, 1);
    TEST_ASSERT_EQUAL(SPOTIFY_AVR_MATCH_YES, out.result);
    /* self_heal_device_id would itself be NULL here (the matched entry's
     * own id is null) -- the caller must not persist a NULL/empty id. */
    TEST_ASSERT_NULL(out.self_heal_device_id);
}

static void test_neither_id_nor_name_match_is_no(void)
{
    spotify_avr_match_outcome_t out = spotify_avr_match_resolve(
        "dev-9", "Living Room Sonos", "dev-1", "Denon AVR-X3800H", NULL, 0);
    TEST_ASSERT_EQUAL(SPOTIFY_AVR_MATCH_NO, out.result);
}

static void test_nothing_pinned_is_unknown(void)
{
    spotify_avr_match_outcome_t out = spotify_avr_match_resolve(
        "dev-1", "Denon AVR-X3800H", NULL, NULL, NULL, 0);
    TEST_ASSERT_EQUAL(SPOTIFY_AVR_MATCH_UNKNOWN, out.result);
}

static void test_no_active_device_is_unknown(void)
{
    spotify_avr_match_outcome_t out = spotify_avr_match_resolve(
        NULL, NULL, "dev-1", "Denon AVR-X3800H", NULL, 0);
    TEST_ASSERT_EQUAL(SPOTIFY_AVR_MATCH_UNKNOWN, out.result);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_id_match_is_yes_no_self_heal);
    RUN_TEST(test_id_match_ignores_name_difference);
    RUN_TEST(test_unique_name_match_with_old_id_gone_self_heals);
    RUN_TEST(test_name_match_case_insensitive);
    RUN_TEST(test_old_id_still_present_is_ambiguous);
    RUN_TEST(test_multiple_name_matches_is_ambiguous);
    RUN_TEST(test_name_match_without_device_list_is_unknown);
    RUN_TEST(test_null_active_id_falls_back_to_name);
    RUN_TEST(test_neither_id_nor_name_match_is_no);
    RUN_TEST(test_nothing_pinned_is_unknown);
    RUN_TEST(test_no_active_device_is_unknown);
    return UNITY_END();
}
