/*
 * Canned PLAYBACK/ARTWORK/DENON core_state_t snapshots -- Phase 5 plan
 * (Tier 5): "normal track, STALE/RECONNECTING/UNAVAILABLE cases,
 * adversarial-UTF-8 title, pale-cover pixel buffer, long-title-needs-marquee
 * case, a no-active-playback transition (exercising Tier -1's clear-on-empty
 * fix)." Nothing in Tier 5 itself reads these back out (view_model glue is
 * Tier 6) -- this is the data Tier 6 iterates against, ready in advance.
 */
#ifndef UI_HARNESS_FIXTURES_H
#define UI_HARNESS_FIXTURES_H

#include <stdbool.h>
#include <stdint.h>

#include "core_state.h"

typedef enum {
    UI_HARNESS_FIXTURE_NORMAL = 0,
    UI_HARNESS_FIXTURE_STALE,
    UI_HARNESS_FIXTURE_RECONNECTING,
    UI_HARNESS_FIXTURE_UNAVAILABLE,
    UI_HARNESS_FIXTURE_ADVERSARIAL_UTF8,
    UI_HARNESS_FIXTURE_PALE_COVER,
    UI_HARNESS_FIXTURE_LONG_TITLE_MARQUEE,
    UI_HARNESS_FIXTURE_NO_ACTIVE_PLAYBACK,
    /* The three below present the Spotify pair overlay at startup. They are
     * the only fixtures that seed DOMAIN_NET/DOMAIN_SPOTIFY -- and the only
     * practical way to point a real phone camera at this project's QR code
     * before flashing a build that contains it. */
    UI_HARNESS_FIXTURE_PAIR,
    UI_HARNESS_FIXTURE_PAIR_NO_NET,
    UI_HARNESS_FIXTURE_PAIR_CONNECTED,
    UI_HARNESS_FIXTURE_COUNT,
} ui_harness_fixture_t;

/* True for the three PAIR_* fixtures above -- main.c presents the overlay
 * when this is set, rather than hardcoding the id list at the call site. */
bool ui_harness_fixture_presents_pair_overlay(ui_harness_fixture_t fixture);

/* Case-sensitive, exact match against ui_harness_fixture_name()'s own
 * strings. False (out untouched) if name matches none of them. */
bool ui_harness_fixture_from_name(const char *name, ui_harness_fixture_t *out);
const char *ui_harness_fixture_name(ui_harness_fixture_t fixture);

/* Seeds state's PLAYBACK/ARTWORK/DENON domains for the named case via real
 * core_state_write() calls (the same entry point every real service uses),
 * and sets *out_pixels to the pixel buffer the fake artwork provider should
 * serve for this fixture's artwork handle -- wire it into
 * fake_artwork_pixel_provider_set_pattern() (fixture_artwork_pixels.h's own
 * canned buffers; NULL means "no artwork," matching an empty
 * artwork_handle_t"). */
void ui_harness_fixture_apply(core_state_t *state, ui_harness_fixture_t fixture, const uint16_t **out_pixels);

#endif /* UI_HARNESS_FIXTURES_H */
