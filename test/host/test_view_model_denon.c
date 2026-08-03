/*
 * Host test for view_model_denon.c -- the Receiver screen's pure builder.
 * Same Tier-1 shape as test_view_model_now_playing.c.
 */
#include <string.h>

#include "unity.h"

#include "denon_intent_types.h"
#include "view_model_denon.h"

void setUp(void) {}
void tearDown(void) {}

static denon_state_t make_denon(uint8_t input_id, bool power_on)
{
    denon_state_t denon = { 0 };
    denon.meta.status     = LINK_STATUS_OK;
    denon.meta.version      = 1;   /* a real core_state_write() always sets this >= 1 --
                                        every test below represents state that has actually
                                        arrived from svc_denon, distinct from the never-
                                        written (version==0) case test_has_real_state_false*
                                        covers explicitly */
    denon.power_on        = power_on;
    denon.volume_half_db = 105;   /* 52.5 dB */
    denon.input_id         = input_id;
    return denon;
}

static playback_state_t make_playback(device_is_avr_t device_is_avr, bool is_playing)
{
    playback_state_t pb = { 0 };
    pb.device_is_avr = device_is_avr;
    pb.is_playing       = is_playing;
    return pb;
}

static void test_power_label_on(void)
{
    denon_state_t     denon = make_denon(DENON_INPUT_NET, true);
    playback_state_t pb     = make_playback(DEVICE_IS_AVR_UNKNOWN, false);

    denon_vm_t vm = view_model_denon_build(&denon, &pb);
    TEST_ASSERT_TRUE(vm.power_on);
    TEST_ASSERT_TRUE(vm.has_real_state);
    TEST_ASSERT_EQUAL_STRING("The receiver is on", vm.power_label);
}

static void test_power_label_off(void)
{
    denon_state_t     denon = make_denon(DENON_INPUT_TV, false);
    playback_state_t pb     = make_playback(DEVICE_IS_AVR_UNKNOWN, false);

    denon_vm_t vm = view_model_denon_build(&denon, &pb);
    TEST_ASSERT_FALSE(vm.power_on);
    TEST_ASSERT_EQUAL_STRING("The receiver is off", vm.power_label);
}

static void test_has_real_state_false_shows_connecting_not_off(void)
{
    /* The exact real-hardware scenario: core_state_init()'s zero-memset,
     * before svc_denon's first core_state_write() ever runs (its real
     * discovery+connect sequence takes several real seconds) -- power_on
     * reads as false here, but this must NOT say "The receiver is off",
     * since that's a real, false claim about a receiver that just hasn't
     * been heard from yet. */
    denon_state_t     denon = { 0 };   /* meta.version == 0 -- never written */
    playback_state_t pb     = make_playback(DEVICE_IS_AVR_UNKNOWN, false);

    denon_vm_t vm = view_model_denon_build(&denon, &pb);
    TEST_ASSERT_FALSE(vm.has_real_state);
    TEST_ASSERT_EQUAL_STRING("Connecting...", vm.power_label);
    TEST_ASSERT_EQUAL_STRING("Connecting...", vm.net_subtitle);
}

static void test_net_selected_when_input_is_net(void)
{
    denon_state_t     denon = make_denon(DENON_INPUT_NET, true);
    playback_state_t pb     = make_playback(DEVICE_IS_AVR_UNKNOWN, false);

    denon_vm_t vm = view_model_denon_build(&denon, &pb);
    TEST_ASSERT_TRUE(vm.net_selected);
    TEST_ASSERT_FALSE(vm.tv_selected);
}

static void test_tv_selected_when_input_is_tv(void)
{
    denon_state_t     denon = make_denon(DENON_INPUT_TV, true);
    playback_state_t pb     = make_playback(DEVICE_IS_AVR_UNKNOWN, false);

    denon_vm_t vm = view_model_denon_build(&denon, &pb);
    TEST_ASSERT_FALSE(vm.net_selected);
    TEST_ASSERT_TRUE(vm.tv_selected);
}

static void test_neither_selected_for_other_input(void)
{
    /* A real, valid state: the receiver is on some other input entirely
     * (e.g. changed from its own remote) -- must not default either card
     * to selected. */
    denon_state_t     denon = make_denon(DENON_INPUT_CD, true);
    playback_state_t pb     = make_playback(DEVICE_IS_AVR_UNKNOWN, false);

    denon_vm_t vm = view_model_denon_build(&denon, &pb);
    TEST_ASSERT_FALSE(vm.net_selected);
    TEST_ASSERT_FALSE(vm.tv_selected);
}

static void test_net_subtitle_playing_when_avr_confirmed_and_playing(void)
{
    denon_state_t     denon = make_denon(DENON_INPUT_NET, true);
    playback_state_t pb     = make_playback(DEVICE_IS_AVR_YES, true);

    denon_vm_t vm = view_model_denon_build(&denon, &pb);
    TEST_ASSERT_EQUAL_STRING("HEOS \xc2\xb7 playing", vm.net_subtitle);
}

static void test_net_subtitle_paused_when_avr_confirmed_but_not_playing(void)
{
    denon_state_t     denon = make_denon(DENON_INPUT_NET, true);
    playback_state_t pb     = make_playback(DEVICE_IS_AVR_YES, false);

    denon_vm_t vm = view_model_denon_build(&denon, &pb);
    TEST_ASSERT_EQUAL_STRING("HEOS \xc2\xb7 paused", vm.net_subtitle);
}

static void test_net_subtitle_idle_when_avr_not_confirmed(void)
{
    /* input_id==NET alone doesn't prove Spotify is actually playing over
     * it -- never claim "playing" without device_is_avr confirming it too. */
    denon_state_t     denon = make_denon(DENON_INPUT_NET, true);
    playback_state_t pb     = make_playback(DEVICE_IS_AVR_NO, true);

    denon_vm_t vm = view_model_denon_build(&denon, &pb);
    TEST_ASSERT_EQUAL_STRING("HEOS \xc2\xb7 idle", vm.net_subtitle);
}

static void test_net_subtitle_when_not_selected(void)
{
    denon_state_t     denon = make_denon(DENON_INPUT_TV, true);
    playback_state_t pb     = make_playback(DEVICE_IS_AVR_YES, true);

    denon_vm_t vm = view_model_denon_build(&denon, &pb);
    TEST_ASSERT_EQUAL_STRING("HEOS", vm.net_subtitle);
}

static void test_volume_display_passthrough(void)
{
    denon_state_t     denon = make_denon(DENON_INPUT_NET, true);
    playback_state_t pb     = make_playback(DEVICE_IS_AVR_UNKNOWN, false);

    denon_vm_t vm = view_model_denon_build(&denon, &pb);
    TEST_ASSERT_EQUAL_STRING("52.5", vm.volume_display);
    TEST_ASSERT_EQUAL_UINT16(105, vm.volume_half_db_raw);
}

static void test_denon_tone_reused_from_link_status(void)
{
    denon_state_t     denon = make_denon(DENON_INPUT_NET, true);
    denon.meta.status         = LINK_STATUS_RECONNECTING;
    playback_state_t pb     = make_playback(DEVICE_IS_AVR_UNKNOWN, false);

    denon_vm_t vm = view_model_denon_build(&denon, &pb);
    TEST_ASSERT_EQUAL(TONE_STALE, vm.denon_tone);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_power_label_on);
    RUN_TEST(test_power_label_off);
    RUN_TEST(test_has_real_state_false_shows_connecting_not_off);
    RUN_TEST(test_net_selected_when_input_is_net);
    RUN_TEST(test_tv_selected_when_input_is_tv);
    RUN_TEST(test_neither_selected_for_other_input);
    RUN_TEST(test_net_subtitle_playing_when_avr_confirmed_and_playing);
    RUN_TEST(test_net_subtitle_paused_when_avr_confirmed_but_not_playing);
    RUN_TEST(test_net_subtitle_idle_when_avr_not_confirmed);
    RUN_TEST(test_net_subtitle_when_not_selected);
    RUN_TEST(test_volume_display_passthrough);
    RUN_TEST(test_denon_tone_reused_from_link_status);
    return UNITY_END();
}
