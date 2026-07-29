# 0004 — Interpolate playback progress locally

Status: Accepted
Date: 2026-07-28

## Context

The Spotify Web API has no push channel. `GET /v1/me/player` returns `progress_ms`, and the
only way to track playback position is to ask repeatedly. Rate limits are a rolling window
and are not published precisely, so polling fast enough to animate a progress bar directly
(say 4 Hz) is both wasteful and likely to earn a 429.

But a progress bar that steps once every few seconds is instantly recognisable as a polled
web UI. Continuous, smooth motion is a large part of what makes a Sonos or Apple Music
interface feel like a product rather than a dashboard. This detail carries more perceived
quality per line of code than anything else in the UI.

## Decision

Poll slowly; render continuously.

On each successful poll, `svc_spotify` stores a fix:

```c
typedef struct {
    uint32_t progress_ms;    /* as reported by Spotify        */
    int64_t  sampled_at_us;  /* esp_timer monotonic clock     */
    bool     is_playing;
} playback_fix_t;
```

The view model computes, every frame:

```
displayed_ms = fix.progress_ms + (is_playing ? (now_us - fix.sampled_at_us)/1000 : 0)
```

Polling stays at 5 s while playing and awake. The progress bar runs at 60 fps.

Corrections on each new fix are applied as a short ease (`THEME_MOTION_QUICK`), never as a
jump. Drift over a 5 s window is a few milliseconds of crystal error — invisible — so
corrections are only ever meaningful after a seek, a track change, or a stall, which are
exactly the cases where a brief animated catch-up looks intentional.

The monotonic clock (`esp_timer_get_time`) is used deliberately, not wall time: an SNTP
step correction must not make the progress bar jump.

`time_source_t` is injected, so this logic is host-tested deterministically without waiting
in real time.

## Consequences

**Good.** A 5 s poll renders as smooth 60 fps motion. Roughly 12 API calls per minute
instead of 240, which keeps us far clear of any rate limit and leaves headroom for the
force-polls after user intents.

**Good.** Remaining-time, scrub position, and progress all derive from one interpolated
value, so they cannot disagree with each other.

**Cost.** The displayed position can be wrong for up to 5 s if the user seeks from another
device. In practice this is the *only* visible artifact, and the correction ease makes it
read as a deliberate transition.

**Cost.** Pausing must freeze interpolation precisely at the moment the pause is confirmed,
or the bar keeps creeping. Interaction with optimistic UI needs care: an optimistic pause
freezes the bar immediately, and if the server disagrees, interpolation resumes from the
corrected fix.

## Alternatives rejected

**Poll fast enough to animate directly (4–10 Hz).** Rejected: near-certain rate limiting,
significant power and radio cost on a 24/7 device, and it puts constant network traffic
behind a display whose PSRAM bandwidth is already the primary risk (R1).

**Step the bar once per poll, no interpolation.** Rejected purely on feel. It is the single
most obvious tell of a DIY controller, and it undermines the stated design goal more than
any other individual choice would.

**Interpolate from wall-clock time.** Rejected because an SNTP step correction would produce
a visible jump or a stall in the progress bar.

**Run a local timer that free-runs and is only reset on track change.** Rejected because it
accumulates error across pauses, seeks, and network stalls with no mechanism to notice. The
fix-plus-offset form is self-correcting by construction.
