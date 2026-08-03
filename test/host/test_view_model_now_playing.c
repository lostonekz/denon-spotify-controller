/*
 * Host test for view_model_now_playing.c -- Phase 5 Tier 1. The full pure
 * builder against fixtures.
 *
 * Art-forward redesign: no longer takes artwork_state_t (its only
 * consumer, the old per-track accent_rgb565 field, was removed -- see
 * view_model_now_playing.h's own top comment) -- the accent-fallback tests
 * that used to live here are gone with it. tone/volume_tone/denon_tone/
 * power_on/device_name/show_bring_here coverage is gone too, for the same
 * reason: their only consumers (a header device-name label, a power glyph,
 * a "Bring Here" tap zone) were all removed live once pressing Play was
 * confirmed to already wake the AVR -- see view_model_now_playing.h's own
 * top comment.
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
    strncpy(pb.device_id, "31fnkj2s6xr4tqhq5zfcqu2xdafa", sizeof(pb.device_id) - 1);
    strncpy(pb.device_name, "Living Room", sizeof(pb.device_name) - 1);
    pb.position_ms = 63500;
    pb.duration_ms = 127000;
    pb.is_playing  = true;
    return pb;
}

static denon_state_t make_denon(void)
{
    denon_state_t denon = { 0 };
    denon.meta.status      = LINK_STATUS_OK;
    denon.volume_half_db  = 105;   /* 52.5 dB */
    denon.power_on              = true;
    return denon;
}

static void test_normal_playing_track(void)
{
    playback_state_t pb    = make_playing_track();
    denon_state_t     denon = make_denon();

    now_playing_vm_t vm = view_model_now_playing_build(&pb, &denon, g_clock, false);

    TEST_ASSERT_EQUAL_STRING("Fake Empire", vm.title);
    TEST_ASSERT_EQUAL_STRING("The National", vm.artist);
    TEST_ASSERT_EQUAL_STRING("Boxer", vm.album);
    TEST_ASSERT_EQUAL_STRING("1:03", vm.elapsed);
    /* position_ms=63500, duration_ms=127000 -- exactly the halfway point,
     * so remaining (127000-63500=63500ms) formats identically to elapsed. */
    TEST_ASSERT_EQUAL_STRING("-1:03", vm.remaining);
    TEST_ASSERT_EQUAL_UINT16(500, vm.progress_permille);
    TEST_ASSERT_EQUAL_UINT32(127000, vm.duration_ms);
    TEST_ASSERT_TRUE(vm.is_playing);
    TEST_ASSERT_FALSE(vm.pending);
    TEST_ASSERT_EQUAL_STRING("52.5", vm.volume_display);
    TEST_ASSERT_EQUAL_UINT16(105, vm.volume_half_db_raw);
    TEST_ASSERT_TRUE(vm.has_active_device);
}

static void test_pending_flag_passes_through(void)
{
    playback_state_t pb    = make_playing_track();
    denon_state_t     denon = make_denon();

    now_playing_vm_t vm = view_model_now_playing_build(&pb, &denon, g_clock, true);
    TEST_ASSERT_TRUE(vm.pending);
}

static void test_empty_title_falls_back_to_album(void)
{
    playback_state_t pb = make_playing_track();
    pb.title[0] = '\0';
    denon_state_t denon = make_denon();

    now_playing_vm_t vm = view_model_now_playing_build(&pb, &denon, g_clock, false);
    TEST_ASSERT_EQUAL_STRING("Boxer", vm.title);
}

static void test_empty_title_and_album_falls_back_to_unknown_track(void)
{
    playback_state_t pb = make_playing_track();
    pb.title[0] = '\0';
    pb.album[0] = '\0';
    denon_state_t denon = make_denon();

    now_playing_vm_t vm = view_model_now_playing_build(&pb, &denon, g_clock, false);
    TEST_ASSERT_EQUAL_STRING("Unknown Track", vm.title);
}

static void test_empty_device_id_means_no_active_device(void)
{
    playback_state_t pb = make_playing_track();
    pb.device_id[0] = '\0';
    denon_state_t denon = make_denon();

    now_playing_vm_t vm = view_model_now_playing_build(&pb, &denon, g_clock, false);
    TEST_ASSERT_FALSE(vm.has_active_device);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_normal_playing_track);
    RUN_TEST(test_pending_flag_passes_through);
    RUN_TEST(test_empty_title_falls_back_to_album);
    RUN_TEST(test_empty_title_and_album_falls_back_to_unknown_track);
    RUN_TEST(test_empty_device_id_means_no_active_device);
    return UNITY_END();
}
