# UI Screens

The four real screens, as actually built. Companion to
[UI_ARCHITECTURE.md](UI_ARCHITECTURE.md), which owns the layering, widget catalogue, theme
tokens, and the navigation mechanism between these screens.

Split out of that file when it outgrew this project's own 400-line limit
(`tools/check_limits.py`) — the same reason several `.c` files here are split. Layout and
per-screen behaviour is the part that changes most often, so it gets its own file.

Order is fixed and never wraps: **Receiver ← Now Playing → Connection → System.**
Now Playing is the default/boot screen.

---
## 6. Now Playing — the screen that matters

**Art-forward, not the card layout below's original "Balanced" design** — full-bleed artwork
with overlaid controls, chosen explicitly over this section's own original mock before any
Phase 5 build work started. This is what actually shipped:

```
┌────────────────────────────────────────────┐
│                                            │  no header at all — see prose below
│    ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓  │
│    ▓▓▓▓▓▓▓  full-bleed album art  ▓▓▓▓▓▓▓  │  square corners, not rounded
│    ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓  │  no scrim — black-stroked text instead
│    ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓  │
│                                            │
│      Track Title Goes Here                 │  hero 38/700, marquee if long
│      Artist Name                           │  20/400
│      ▓▓▓▓▓▓▓▓▓░░░░░░░░░░░░░░░░░            │  same height/fill/track as VOL below
│      1:24                        -2:41     │  follow the finger while scrubbing
│         ◀◀        ⏸        ▶▶              │  56 px targets, no heart
│      VOL ▓▓▓▓▓▓▓▓░░░░░░░░░░         52.5   │  real live-dragging Denon volume
└────────────────────────────────────────────┘
```

- **No header row.** The status pill, the device-name label, and a power glyph all lived up
  there at various points and were each removed live: pressing Play was confirmed to already
  wake the AVR, switch to the HEOS Music input, and resume playback there
  (`PLAYBACK_INTENT_RESUME_ON_AVR`), which made a standing header control redundant. One real
  consequence, named rather than hidden: **no STALE/OFFLINE health signal is visible on this
  screen at all.** That signal now lives on the System screen (below), which is a better home
  for it, but this screen genuinely doesn't carry it.
- Artwork drops to a neutral placeholder the *instant* the currently-playing track id
  changes (from `playback_state_t`, which updates immediately) — text/metadata update on the
  same event, so nothing lags. The new artwork pops in (no animation) once its own
  `DOMAIN_ARTWORK` update, matching that same track id, actually arrives — that can take up
  to ~1.8s (RISKS.md R1), which is why these are driven by two separate track ids, not one:
  a real bug, found live, had the placeholder wait for the artwork-side id too, so the old
  cover visibly lingered under the new track's title/artist for that whole window.
- The progress bar animates continuously from local interpolation, corrected smoothly on
  each poll — never a visible jump — and is a real seek control (UI_ARCHITECTURE.md §4).
  **While you scrub, the timestamps follow your finger** rather than continuing to report
  live playback: the one question a scrubber has to answer is "what time am I about to land
  on?", and until that was fixed it was the one thing the bar couldn't tell you. The seek
  itself still commits once, on release.
- **The scrub bar and the volume bar are deliberately identical** — same height
  (`THEME_TRACK_H`), fill colour, track translucency, outline, and rounded ends. The design
  spec had drawn the scrub bar 3px, flat white, opaque-tracked, because it was never meant to
  be touched; once it became a real drag control that made it read as a different class of
  thing from the control sitting directly beneath it.
- The volume row is `denon_state_t`'s own volume (Denon is the primary volume control,
  ADR-0007), live and draggable. Its screen position is deliberately pixel-identical to the
  Connection screen's own cap slider, so the control doesn't visibly jump when paging.
- A tap-to-switch Spotify Connect device picker was built, confirmed working on real
  hardware, and then explicitly reverted at the user's request. Nothing on this screen
  switches devices today.
- Play/pause/prev/next are optimistic: the tap dims/disables the row immediately (not a full
  per-field state flip), then reconciles against a real `INTENT_ACK_EVT_ACCEPTED`/`REJECTED`
  or a 3 s abandon timeout, per §6.3 of [EVENTS_AND_STATE.md](EVENTS_AND_STATE.md).

---

## 6b. Receiver and Connection

Both are Phase 6, both driven by `denon_state_t`.

```
   RECEIVER                            CONNECTION
┌────────────────────────┐   ┌────────────────────────────┐
│ AVR-X3800H             │   │ CONNECTION        ● Linked │
│ ┌────────────────────┐ │   │ ┌────────────────────────┐ │
│ │ ⏻  The receiver    │ │   │ │ Receiver  192.168.1.15 │ │
│ │    is on      [ ●] │ │   │ │ Round trip       38 ms │ │
│ └────────────────────┘ │   │ └────────────────────────┘ │
│ LISTENING TO           │   │ HOW WE TALK TO IT          │
│ ┌─────────┐┌─────────┐ │   │ ┌─────────┐┌─────────┐     │
│ │ Spotify ││ TV audio│ │   │ │ Telnet  ││ HTTP    │     │
│ │ HEOS ·  ││ HDMI ARC│ │   │ │ Port 23 ││ Fallback│     │
│ │ playing ││         │ │   │ └─────────┘└─────────┘     │
│ └─────────┘└─────────┘ │   │                            │
│                        │   │ Don't go past              │
│ VOL ▓▓▓▓░░░░░░   52.5  │   │ VOL ▓▓▓▓░░░░░░   35.0     │
└────────────────────────┘   └────────────────────────────┘
```

- **"Connecting…", not "The receiver is off."** A real bug found live: `core_state`'s
  zero-init makes a never-written `denon_state_t` read as `power_on = false` — indistinguishable
  from a real, confirmed-off receiver. `svc_denon`'s own discovery+connect takes several
  seconds after boot, so the screen confidently stated something false for that whole window.
  Both screens now check `denon_state_t.meta.version != 0` before claiming anything.
- **The Spotify card only claims "playing" when two domains agree** — `input_id == NET`
  *and* `device_is_avr == YES` *and* `is_playing`. `input_id` alone just means the receiver's
  selector is on that input, which it may have been since a previous session.
- **Neither card is selected** when the receiver is on any of the other ten inputs (someone
  used its own remote). A real state, not defaulted either way.
- **Round-trip latency is an honest approximation**, and labelled as one in the code: the gap
  between our last sent command and the next line received. The protocol has no request IDs,
  so true per-command correlation isn't available.

---

## 6c. System

The "is it me or the house?" screen. Health first, identifiers last.

```
┌────────────────────────────────────────────┐
│ Everything is working                      │  one computed verdict, in words
│                                            │
│ ● Wi-Fi                          -48 dBm   │  green / amber / red dot + value
│ ● Receiver                         38 ms   │
│ ● Spotify                        Renewing  │  amber while a token refresh is in flight
│                                            │
│ Firmware            207b069 · Aug  3 2026  │  no dot — identifiers, not health
│ Awake for                        6 d 4 h   │
│ Panel                             A1B2C3   │
└────────────────────────────────────────────┘
```

- The verdict is a single pure function of the three tones: all green → a positive phrase,
  anything else → one generic "needs attention". It deliberately doesn't try to enumerate
  every combination in prose.
- **Wi-Fi RSSI, firmware version/date, and the panel identifier are read via direct ESP-IDF
  calls** (`esp_wifi_sta_get_ap_info()`, `esp_ota_get_app_description()`, `esp_read_mac()`)
  in this screen's own target-only apply-glue — a legal `ui → idf` edge, the same category as
  `ui_app.c`'s existing `esp_timer_get_time()` call. No new `core_state` domain, no
  `app_wiring` relay. The pure builder itself only formats plain values it's handed, so it
  stays host-testable.
- **"Renewing" is a real state, not a cosmetic label.** `spotify_session_summary_t` gained
  `SPOTIFY_SESSION_REFRESHING`, set at the one point a proactive token refresh actually
  starts and cleared by the transitions that already ran. Previously this was collapsed into
  plain "Connected".
- The panel identifier is the low 3 bytes of the station MAC. No real serial number exists
  anywhere on this board — this is a stable, unique-per-device stand-in, not a fabricated
  string.

