/*
 * Host test for view_model_now_playing.c -- Phase 5 Tier 1. The full pure
 * builder against fixtures, including the artwork/playback track_id
 * staleness check (Phase 5 plan §1).
 */
#include <string.h>

#include "unity.h"

#include "fake_time_source.h"
#include "view_model_now_playing.h"

static fake_time_source_t g_fake;
static time_source_t g_clock;

void setUp(void)
{
    g_fake.now_us = 1000000000;
    g_clock = fake_time_source_get(&g_fake);
}

void tearDown(void) {}

static playback_state_t make_playing_track(void)
{
    playback_state_t pb = { 0 };
    pb.meta.status     = LINK_STATUS_OK;
    pb.meta.updated_us = g_fake.now_us;
    strncpy(pb.title, "Fake Empire", sizeof(pb.title) - 1);
    strncpy(pb.artist, "The National", sizeof(pb.artist) - 1);
    strncpy(pb.album, "Boxer", sizeof(pb.album) - 1);
    strncpy(pb.track_id, "3JOVTQ5h8HGFnDdp4VT3MP", sizeof(pb.track_id) - 1);
    pb.position_ms = 63500;
    pb.duration_ms = 127000;
    pb.is_playing  = true;
    return pb;
}

static artwork_state_t make_matching_artwork(const playback_state_t *pb)
{
    artwork_state_t art = { 0 };
    art.meta.status            = LINK_STATUS_OK;
    art.handle.slot             = 1;
    art.handle.generation      = 7;
    art.dominant_color_rgb565 = 0x1234;
    strncpy(art.track_id, pb->track_id, sizeof(art.track_id) - 1);
    return art;
}

static denon_state_t make_denon(void)
{
    denon_state_t denon = { 0 };
    denon.meta.status      = LINK_STATUS_OK;
    denon.volume_half_db  = 105;   /* 52.5 dB */
    return denon;
}

static void test_normal_playing_track(void)
{
    playback_state_t pb = make_playing_track();
    artwork_state_t art = make_matching_artwork(&pb);
    denon_state_t denon = make_denon();

    now_playing_vm_t vm = view_model_now_playing_build(&pb, &art, &denon, g_clock, false);

    TEST_ASSERT_EQUAL_STRING("Fake Empire", vm.title);
    TEST_ASSERT_EQUAL_STRING("The National", vm.artist);
    TEST_ASSERT_EQUAL_STRING("Boxer", vm.album);
    TEST_ASSERT_EQUAL_STRING("1:03", vm.elapsed);
    /* position_ms=63500, duration_ms=127000 -- exactly the halfway point,
     * so remaining (127000-63500=63500ms) formats identically to elapsed. */
    TEST_ASSERT_EQUAL_STRING("-1:03", vm.remaining);
    TEST_ASSERT_EQUAL_UINT16(500, vm.progress_permille);
    TEST_ASSERT_TRUE(vm.is_playing);
    TEST_ASSERT_FALSE(vm.pending);
    TEST_ASSERT_EQUAL(TONE_LIVE, vm.tone);
    TEST_ASSERT_EQUAL_HEX16(0x1234, vm.accent_rgb565);
    TEST_ASSERT_EQUAL_STRING("52.5", vm.volume_display);
    TEST_ASSERT_EQUAL(TONE_LIVE, vm.volume_tone);
}

static void test_pending_flag_passes_through(void)
{
    playback_state_t pb = make_playing_track();
    artwork_state_t art = make_matching_artwork(&pb);
    denon_state_t denon = make_denon();

    now_playing_vm_t vm = view_model_now_playing_build(&pb, &art, &denon, g_clock, true);
    TEST_ASSERT_TRUE(vm.pending);
}

static void test_no_artwork_handle_falls_back_to_default_accent(void)
{
    playback_state_t pb = make_playing_track();
    artwork_state_t art = { 0 };   /* {0,0} handle -- "no artwork" */
    denon_state_t denon = make_denon();

    now_playing_vm_t vm = view_model_now_playing_build(&pb, &art, &denon, g_clock, false);
    TEST_ASSERT_EQUAL_HEX16(0x8410, vm.accent_rgb565);
}

static void test_artwork_for_a_different_track_falls_back_to_default_accent(void)
{
    /* A held, genuinely-acquired handle -- but for the PREVIOUS track,
     * not the one playing now. The two services publish independently;
     * this is exactly the race #1 in Phase 5 plan §1 exists to guard
     * against, at the view-model layer. */
    playback_state_t pb = make_playing_track();
    artwork_state_t art = make_matching_artwork(&pb);
    strncpy(art.track_id, "some_other_track_id_entirely", sizeof(art.track_id) - 1);
    denon_state_t denon = make_denon();

    now_playing_vm_t vm = view_model_now_playing_build(&pb, &art, &denon, g_clock, false);
    TEST_ASSERT_EQUAL_HEX16(0x8410, vm.accent_rgb565);
}

static void test_empty_title_falls_back_to_album(void)
{
    playback_state_t pb = make_playing_track();
    pb.title[0] = '\0';
    artwork_state_t art = make_matching_artwork(&pb);
    denon_state_t denon = make_denon();

    now_playing_vm_t vm = view_model_now_playing_build(&pb, &art, &denon, g_clock, false);
    TEST_ASSERT_EQUAL_STRING("Boxer", vm.title);
}

static void test_empty_title_and_album_falls_back_to_unknown_track(void)
{
    playback_state_t pb = make_playing_track();
    pb.title[0] = '\0';
    pb.album[0] = '\0';
    artwork_state_t art = make_matching_artwork(&pb);
    denon_state_t denon = make_denon();

    now_playing_vm_t vm = view_model_now_playing_build(&pb, &art, &denon, g_clock, false);
    TEST_ASSERT_EQUAL_STRING("Unknown Track", vm.title);
}

static void test_stale_playback_link_status_reflected_in_tone(void)
{
    playback_state_t pb = make_playing_track();
    pb.meta.status = LINK_STATUS_STALE;
    artwork_state_t art = make_matching_artwork(&pb);
    denon_state_t denon = make_denon();

    now_playing_vm_t vm = view_model_now_playing_build(&pb, &art, &denon, g_clock, false);
    TEST_ASSERT_EQUAL(TONE_STALE, vm.tone);
}

static void test_denon_tone_is_independent_of_playback_tone(void)
{
    playback_state_t pb = make_playing_track();
    artwork_state_t art = make_matching_artwork(&pb);
    denon_state_t denon = make_denon();
    denon.meta.status = LINK_STATUS_UNAVAILABLE;

    now_playing_vm_t vm = view_model_now_playing_build(&pb, &art, &denon, g_clock, false);
    TEST_ASSERT_EQUAL(TONE_LIVE, vm.tone);           /* playback is still fine */
    TEST_ASSERT_EQUAL(TONE_OFFLINE, vm.volume_tone);  /* denon is not */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_normal_playing_track);
    RUN_TEST(test_pending_flag_passes_through);
    RUN_TEST(test_no_artwork_handle_falls_back_to_default_accent);
    RUN_TEST(test_artwork_for_a_different_track_falls_back_to_default_accent);
    RUN_TEST(test_empty_title_falls_back_to_album);
    RUN_TEST(test_empty_title_and_album_falls_back_to_unknown_track);
    RUN_TEST(test_stale_playback_link_status_reflected_in_tone);
    RUN_TEST(test_denon_tone_is_independent_of_playback_tone);
    return UNITY_END();
}
