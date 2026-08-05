#include "fixtures.h"

#include <string.h>

#include "denon_intent_types.h"
#include "fixture_artwork_pixels.h"

static const char *const FIXTURE_NAMES[UI_HARNESS_FIXTURE_COUNT] = {
    [UI_HARNESS_FIXTURE_NORMAL]              = "normal",
    [UI_HARNESS_FIXTURE_STALE]               = "stale",
    [UI_HARNESS_FIXTURE_RECONNECTING]        = "reconnecting",
    [UI_HARNESS_FIXTURE_UNAVAILABLE]         = "unavailable",
    [UI_HARNESS_FIXTURE_ADVERSARIAL_UTF8]    = "adversarial-utf8",
    [UI_HARNESS_FIXTURE_PALE_COVER]          = "pale-cover",
    [UI_HARNESS_FIXTURE_LONG_TITLE_MARQUEE]  = "long-title-marquee",
    [UI_HARNESS_FIXTURE_NO_ACTIVE_PLAYBACK]  = "no-active-playback",
    [UI_HARNESS_FIXTURE_PAIR]                = "pair",
    [UI_HARNESS_FIXTURE_PAIR_NO_NET]         = "pair-nonet",
    [UI_HARNESS_FIXTURE_PAIR_CONNECTED]      = "pair-connected",
};

bool ui_harness_fixture_presents_pair_overlay(ui_harness_fixture_t fixture)
{
    return fixture == UI_HARNESS_FIXTURE_PAIR ||
           fixture == UI_HARNESS_FIXTURE_PAIR_NO_NET ||
           fixture == UI_HARNESS_FIXTURE_PAIR_CONNECTED;
}

/* Deliberately malformed: a real multi-byte prefix followed by a bare 0xE2
 * (the lead byte of a 3-byte UTF-8 sequence) with no continuation bytes
 * before the terminating NUL -- the "truncated UTF-8 tail" case Tier 1's
 * view_model_text.c host tests already cover in isolation; this fixture is
 * for Tier 6 to exercise the same case through a real rendered screen. */
/* String-literal breaks after \x9E and \xAD are required, not stylistic --
 * hex escapes are greedy and 'e'/'d' are themselves valid hex digits, so
 * "\x9Eeir"/"\xADldu" written as one literal would swallow those letters
 * into the escape (0x9EE / 0xADl.. is not a valid single-byte value) and
 * fail to compile ("hex escape sequence out of range"), a real error this
 * file hit once already. */
static const char ADVERSARIAL_TITLE[] = "\xC3\x93lafur \xC3\x9E" "eir Sk\xC3\xAD" "ldu \xE2";

static const char LONG_TITLE[] =
    "This Is An Absurdly Long Track Title That Will Definitely Need To Marquee-Scroll";

/* Generated once, on first use -- fixture_artwork_pixels_*() fill a
 * caller-owned buffer rather than owning storage themselves, and this is
 * that storage. 320*320*2 bytes each; trivial for a desktop harness. */
static uint16_t s_pixels_normal[ARTWORK_PIXEL_WIDTH * ARTWORK_PIXEL_HEIGHT];
static uint16_t s_pixels_pale[ARTWORK_PIXEL_WIDTH * ARTWORK_PIXEL_HEIGHT];
static bool     s_pixels_generated = false;

static void ensure_pixels_generated(void)
{
    if (s_pixels_generated) {
        return;
    }
    fixture_artwork_pixels_normal(s_pixels_normal);
    fixture_artwork_pixels_pale(s_pixels_pale);
    s_pixels_generated = true;
}

bool ui_harness_fixture_from_name(const char *name, ui_harness_fixture_t *out)
{
    for (int i = 0; i < UI_HARNESS_FIXTURE_COUNT; i++) {
        if (strcmp(name, FIXTURE_NAMES[i]) == 0) {
            *out = (ui_harness_fixture_t)i;
            return true;
        }
    }
    return false;
}

const char *ui_harness_fixture_name(ui_harness_fixture_t fixture)
{
    return FIXTURE_NAMES[fixture];
}

static void set_playback(core_state_t *state, link_status_t status, const char *title,
                          const char *artist, const char *album, const char *track_id,
                          bool is_playing, uint32_t position_ms, uint32_t duration_ms)
{
    playback_state_t pb = { 0 };
    strncpy(pb.title, title, sizeof(pb.title) - 1);
    strncpy(pb.artist, artist, sizeof(pb.artist) - 1);
    strncpy(pb.album, album, sizeof(pb.album) - 1);
    strncpy(pb.track_id, track_id, sizeof(pb.track_id) - 1);
    strncpy(pb.device_id, "fixture-device", sizeof(pb.device_id) - 1);
    strncpy(pb.device_name, "Living Room", sizeof(pb.device_name) - 1);
    pb.device_is_avr           = DEVICE_IS_AVR_YES;
    pb.device_supports_volume = true;
    pb.position_ms               = position_ms;
    pb.duration_ms               = duration_ms;
    pb.is_playing                 = is_playing;
    core_state_write(state, DOMAIN_PLAYBACK, status, &pb, sizeof(pb));
}

static void set_artwork(core_state_t *state, link_status_t status, uint32_t track_id_ord,
                         const char *track_id, uint32_t dominant_rgb565)
{
    artwork_state_t art = { 0 };
    if (track_id_ord != 0) {
        art.handle.slot       = track_id_ord;
        art.handle.generation = 1;
        strncpy(art.track_id, track_id, sizeof(art.track_id) - 1);
    }
    art.dominant_color_rgb565 = dominant_rgb565;
    core_state_write(state, DOMAIN_ARTWORK, status, &art, sizeof(art));
}

static void set_denon_normal(core_state_t *state)
{
    denon_state_t d = {
        .power_on          = true,
        .muted              = false,
        .volume_half_db      = 96,   /* 48.0 dB -- denon_codec.h's 0..197 range, a
                                        * plausible mid-volume value; `ui` never
                                        * decodes this itself (plan §6) */
        /* DENON_INPUT_NET (not the CD/other-input case) so the Receiver
         * screen's Spotify card actually renders selected against every
         * fixture below, each of which sets a real playback_state_t too --
         * a more useful default demo state than "neither card selected". */
        .input_id             = DENON_INPUT_NET,
        .listening_mode_id      = 0,
    };
    core_state_write(state, DOMAIN_DENON, LINK_STATUS_OK, &d, sizeof(d));
}

/* 192.168.1.23 in ip_info.ip.addr's own layout: network byte order held in a
 * little-endian word, so the FIRST octet is the LOW byte. Written the long
 * way here on purpose -- a hardcoded 0x1701A8C0 would silently stop matching
 * the comment if anyone edited it. */
static void set_net(core_state_t *state, link_status_t status, bool have_ip)
{
    net_state_t net = { 0 };
    net.link_up = true;   /* deliberately true even in the no-network fixture: this
                              field is a write-once latch on target and never goes
                              false, which is exactly the trap the pair VM must not
                              fall into. The fixture reproduces that shape. */
    if (have_ip) {
        net.ipv4 = 192u | (168u << 8) | (1u << 16) | (23u << 24);
    }
    net.rssi_dbm = -48;
    core_state_write(state, DOMAIN_NET, status, &net, sizeof(net));
}

static void set_spotify(core_state_t *state, spotify_session_summary_t session)
{
    spotify_state_t sp = { 0 };
    sp.session_status = session;
    sp.authenticated   = session == SPOTIFY_SESSION_ACTIVE;
    core_state_write(state, DOMAIN_SPOTIFY, LINK_STATUS_OK, &sp, sizeof(sp));
}

void ui_harness_fixture_apply(core_state_t *state, ui_harness_fixture_t fixture, const uint16_t **out_pixels)
{
    ensure_pixels_generated();
    set_denon_normal(state);
    *out_pixels = s_pixels_normal;

    switch (fixture) {
    case UI_HARNESS_FIXTURE_NORMAL:
        set_playback(state, LINK_STATUS_OK, "Reflections", "Kiasmos", "Blurred EP",
                     "5x7yQz3k", true, 87000, 214000);
        set_artwork(state, LINK_STATUS_OK, 1, "5x7yQz3k", 0x3A6A);
        break;

    case UI_HARNESS_FIXTURE_STALE:
        set_playback(state, LINK_STATUS_STALE, "Reflections", "Kiasmos", "Blurred EP",
                     "5x7yQz3k", true, 87000, 214000);
        set_artwork(state, LINK_STATUS_STALE, 1, "5x7yQz3k", 0x3A6A);
        break;

    case UI_HARNESS_FIXTURE_RECONNECTING:
        set_playback(state, LINK_STATUS_RECONNECTING, "Reflections", "Kiasmos", "Blurred EP",
                     "5x7yQz3k", true, 87000, 214000);
        set_artwork(state, LINK_STATUS_RECONNECTING, 1, "5x7yQz3k", 0x3A6A);
        break;

    case UI_HARNESS_FIXTURE_UNAVAILABLE:
        set_playback(state, LINK_STATUS_UNAVAILABLE, "Reflections", "Kiasmos", "Blurred EP",
                     "5x7yQz3k", true, 87000, 214000);
        set_artwork(state, LINK_STATUS_UNAVAILABLE, 1, "5x7yQz3k", 0x3A6A);
        break;

    case UI_HARNESS_FIXTURE_ADVERSARIAL_UTF8:
        set_playback(state, LINK_STATUS_OK, ADVERSARIAL_TITLE, "\xC3\x93lafur Arnalds", "Living Room Songs",
                     "adv0001", true, 12000, 198000);
        set_artwork(state, LINK_STATUS_OK, 2, "adv0001", 0x2965);
        break;

    case UI_HARNESS_FIXTURE_PALE_COVER:
        set_playback(state, LINK_STATUS_OK, "Weightless", "Marconi Union", "Weightless",
                     "pale0001", true, 45000, 480000);
        set_artwork(state, LINK_STATUS_OK, 3, "pale0001", 0xFFDF);
        *out_pixels = s_pixels_pale;
        break;

    case UI_HARNESS_FIXTURE_LONG_TITLE_MARQUEE:
        set_playback(state, LINK_STATUS_OK, LONG_TITLE, "A Band With An Equally Long Name",
                     "An Album Title That Also Runs On", "long0001", true, 5000, 300000);
        set_artwork(state, LINK_STATUS_OK, 4, "long0001", 0x4A28);
        break;

    case UI_HARNESS_FIXTURE_NO_ACTIVE_PLAYBACK:
        /* Tier -1's fix: NO_ACTIVE_DEVICE now publishes an empty snapshot
         * (not a bare early return), and handle_track_change() releases a
         * held artwork handle on an empty-track update instead of leaving
         * stale artwork on screen. This fixture is that resulting state,
         * not the transition itself -- nothing in Tier 5 replays the
         * "was playing, then stopped" sequence, since nothing subscribes
         * to react to it yet (Tier 6). */
        set_playback(state, LINK_STATUS_OK, "", "", "", "", false, 0, 0);
        set_artwork(state, LINK_STATUS_OK, 0, "", 0);
        *out_pixels = NULL;
        break;

    /* The pairing fixtures share Now Playing's normal state -- what varies is
     * only DOMAIN_NET/DOMAIN_SPOTIFY, which is all the pair overlay reads. */
    case UI_HARNESS_FIXTURE_PAIR:
        set_playback(state, LINK_STATUS_OK, "Reflections", "Kiasmos", "Blurred EP",
                     "5x7yQz3k", true, 87000, 214000);
        set_artwork(state, LINK_STATUS_OK, 1, "5x7yQz3k", 0x3A6A);
        set_net(state, LINK_STATUS_OK, true);
        set_spotify(state, SPOTIFY_SESSION_PAIRING);   /* the QR-visible state */
        break;

    case UI_HARNESS_FIXTURE_PAIR_NO_NET:
        set_playback(state, LINK_STATUS_OK, "Reflections", "Kiasmos", "Blurred EP",
                     "5x7yQz3k", true, 87000, 214000);
        set_artwork(state, LINK_STATUS_OK, 1, "5x7yQz3k", 0x3A6A);
        set_net(state, LINK_STATUS_UNAVAILABLE, true);   /* stale IP still present */
        set_spotify(state, SPOTIFY_SESSION_PAIRING);
        break;

    case UI_HARNESS_FIXTURE_PAIR_CONNECTED:
        set_playback(state, LINK_STATUS_OK, "Reflections", "Kiasmos", "Blurred EP",
                     "5x7yQz3k", true, 87000, 214000);
        set_artwork(state, LINK_STATUS_OK, 1, "5x7yQz3k", 0x3A6A);
        set_net(state, LINK_STATUS_OK, true);
        set_spotify(state, SPOTIFY_SESSION_ACTIVE);   /* -> the confirm-to-re-pair state */
        break;

    case UI_HARNESS_FIXTURE_COUNT:
    default:
        break;
    }
}
