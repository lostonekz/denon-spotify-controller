# UI Architecture

480 × 480 square panel. LVGL 9.x. One task, core 1, never blocks.
Companion to [ARCHITECTURE.md](ARCHITECTURE.md).

---

## 1. Layering

```
ui_app          owns lv_display, the root object, the event subscription,
                and the UI task loop. Knows no screen internals. (356 lines
                today, not the ~150 originally estimated here — most of the
                growth is real-hardware-finding comments, e.g. the LVGL PSRAM
                pool/draw-buffer sizing history, not added logic.)
    │
ui_router       screen stack, transitions, back/gesture handling, lifecycle
    │
view_model      domain state ──► display-ready values (the translation layer)
    │
screens/        composition only. No formatting, no business rules, no drawing.
    │
widgets/        reusable, parameterised, self-contained
    │
theme/          tokens: colour, type, metrics, motion. No literals above this line.
    │
assets/         generated fonts + icons (C arrays)
```

**The view model is the load-bearing piece.** Without it, every screen grows its own copy
of "format 187000 ms as 3:07", "clamp volume to the safe range", "pick a colour for the
STALE state". Three screens later that logic has drifted and the product feels incoherent.

```c
/* view_model.c — the only place domain data becomes display data */
typedef struct {
    char      title[96];          /* pre-truncated, UTF-8 safe */
    char      artist[96];
    char      album[96];
    char      elapsed[8];         /* "3:07"   */
    char      remaining[8];       /* "-1:24"  */
    uint16_t  progress_permille;  /* 0..1000, already interpolated */
    bool      is_playing;
    bool      pending;            /* optimistic intent in flight */
    tone_t    tone;               /* LIVE | STALE | OFFLINE */
    lv_color_t accent;            /* derived from album art */
} now_playing_vm_t;
```

Screens receive a `*_vm_t` and do nothing but assign it to widgets. That makes screens
trivial, and it makes the interesting logic host-testable with no LVGL and no hardware.

---

## 2. The UI task

```
for (;;) {
    drain_event_queue(0);          /* zero timeout — never blocks */
    apply_pending_vm_updates();
    uint32_t next = lv_timer_handler();
    vTaskDelay(pdMS_TO_TICKS(MIN(next, 16)));
}
```

Hard rules, enforced by review and by a debug-build assertion on loop duration:

- No socket, NVS, I²C (beyond touch), or filesystem call from this task, ever.
- No `lv_obj_create` in a hot path. Widgets are created at screen mount and then *updated*.
- Any handler that would take > 2 ms posts an intent and returns.
- LVGL is single-threaded here. No other task may touch an `lv_*` API. Services publish
  events; the UI task consumes them. This is why the event bus copies by value.

---

## 3. Screens and navigation

Three peer screens on a horizontal pager, plus modals and system screens.

```
   ◄── swipe ──►            ◄── swipe ──►
┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│  NOW PLAYING │    │    DENON     │    │   SETTINGS   │
│   (default)  │    │              │    │              │
└──────────────┘    └──────────────┘    └──────────────┘
        ▲                   ▲                   ▲
        └───────── page indicator dots ─────────┘

Modals (slide up, dismiss by swipe-down):
   volume sheet · input picker · listening-mode picker · re-pair QR · confirm

System screens (router-owned, preempt everything):
   boot · provisioning · reauth · ota-applying · fatal
```

Three top-level destinations, reachable by one swipe, no hierarchy to remember. That is the
Sonos/Tesla model and it is right for a wall device that gets glanced at, not browsed.

**Lifecycle contract** — every screen implements the same vtable:

```c
typedef struct {
    lv_obj_t *(*create)   (screen_ctx_t *ctx);   /* build the tree      */
    void      (*mount)    (screen_ctx_t *ctx);   /* subscribe, animate in  */
    void      (*update)   (screen_ctx_t *ctx, const void *vm);
    void      (*unmount)  (screen_ctx_t *ctx);   /* unsubscribe, stop anims */
    void      (*destroy)  (screen_ctx_t *ctx);
} screen_if_t;
```

- Now Playing and Denon are **created once at boot and never destroyed** — swiping between
  them must be instant and allocation-free.
- Settings and modals are created on demand and destroyed on exit, from the LVGL pool.
- `unmount` **must** stop every animation and timer it started. A leaked `lv_anim` on a
  hidden screen is a silent frame-rate tax that is miserable to find later.

---

## 4. Widget catalogue

Reusable, parameterised, no screen-specific branching inside them.

This was originally a 14-widget catalogue written for a "Balanced" card-layout mock. Phase 5
built Art-forward instead (§6) and only the 7 widgets that layout actually uses; the
remaining 7 are real Phase 6 scope, not cut. `w_backdrop` isn't separately built at all under
Art-forward — the full-bleed art card *is* the backdrop, so it folds into `w_art_card`.

**Built, Phase 5:**

| Widget | Purpose |
|---|---|
| `w_art_card` | Full-bleed album art + integrated scrim, placeholder state. **No animated crossfade** — a real finding, not the original design: `w_art_card_set_artwork()` drops to the neutral placeholder the *instant* the currently-playing track id changes (from `playback_state_t`, which updates immediately), independent of whether the new artwork has finished fetching/decoding yet (that can lag up to ~1.8s, RISKS.md R1); once it does, the new image pops in at full opacity directly, not an animated fade — an earlier animated opacity transition (TRANSP→COVER over 600ms) forced LVGL's software renderer through a real alpha-compositing layer for every frame, measured at 337ms for a single frame on real hardware, well over R10's 100ms stall budget. Filed as a real follow-up: either root-cause LVGL's layer-alloc cost directly, or fade via a cheaper mechanism that doesn't force a layer. **Square corners, not rounded** — a separate real finding: with rounded corners (`clip_corner`) enabled, LVGL's own compositing-layer allocator (`lv_draw_layer_alloc_buf`) never returns on this real hardware/LVGL-version combination, confirmed stuck inside `lv_tlsf.c`'s own block search, not failing or asserting, at every pool size tried. Root cause not found; `clip_corner` is off as the working fix. The scrim itself is a flat, solid, per-track-adaptive opacity, not the vertical gradient this section originally described (§6) — RGB565's 16-bit color depth made a smooth opacity ramp visibly band on real hardware. |
| `w_marquee_label` | Text that gently scrolls only when it overflows, pauses at each end |
| `w_progress_bar` | Interpolated progress + elapsed/remaining. **Display-only, no scrub-on-drag** — `spotify_intent.h` has no SEEK intent kind and `NETWORKING.md` §1.3's endpoint list has no seek endpoint; there is nothing to wire a drag gesture to yet |
| `w_transport` | Prev / play-pause / next, with the pending-state treatment. **No heart/favorite** — no Spotify save/favorite endpoint exists anywhere in this project's own endpoint list, so a heart icon would be a permanently-dead tap target |
| `w_toast` | Transient bottom message, auto-dismiss, queued — `TOPIC_TOAST`'s real consumer |
| `w_status_pill` | Small connectivity/link indicator; the honest STALE/OFFLINE surface |
| `w_page_dots` | Pager position indicator |

**Phase 6, not yet built:**

| Widget | Purpose |
|---|---|
| `w_backdrop` | Not needed under Art-forward — see above |
| `w_icon_toggle` | Shuffle, repeat, mute — off / on / on-with-variant (repeat-one) |
| `w_volume_dial` | Arc drag control; used for both Spotify and Denon volume |
| `w_volume_sheet` | Modal with dial, numeric dB, mute, safety-limit indication |
| `w_list_row` | Label + value + optional chevron/toggle. Every settings and picker row. |
| `w_picker` | Modal single-select list (inputs, listening modes, enum settings) |
| `w_qr` | QR rendering for provisioning and Spotify re-pair |

Each widget owns a small `*_cfg_t` and exposes `create(parent, cfg)` plus `set_*` updaters.
No widget reads global state or the event bus — everything arrives through its setters.
That is what makes them reusable and what keeps duplicated UI code from reappearing.

---

## 5. Theme

### 5.1 Tokens, never literals

No hex colour, pixel size, or duration appears outside `theme/`. Screens and widgets refer
to semantic roles only.

```c
THEME_COLOR_BG              /* #0A0A0C  near-black, slightly cool          */
THEME_COLOR_SURFACE         /* #141417  cards                              */
THEME_COLOR_SURFACE_HIGH    /* #1F1F24  raised / pressed                   */
THEME_COLOR_TEXT_PRIMARY    /* #F5F5F7                                     */
THEME_COLOR_TEXT_SECONDARY  /* #A0A0A8                                     */
THEME_COLOR_TEXT_TERTIARY   /* #6A6A72                                     */
THEME_COLOR_ACCENT          /* dynamic — from album art                    */
THEME_COLOR_ACCENT_MUTED
THEME_COLOR_STALE           /* the honest "data is old" tint               */
THEME_COLOR_DANGER
```

Semantic naming is what makes a light theme, a night theme, or a rebrand a one-file change
three years from now.

### 5.2 Dynamic accent — the premium detail

`artwork_palette.c` extracts a dominant colour during JPEG decode (it already has the
downscaled thumbnail, so this is nearly free):

1. Downsample to 32 × 32.
2. Histogram in HSV, discard near-black/near-white/low-saturation bins.
3. Take the modal hue, boost saturation, then **clamp luminance** so text contrast against
   it always clears 4.5:1.
4. Emit `accent` + `accent_muted` on `TOPIC_ARTWORK`.

The accent drives the progress bar, active toggles, and the backdrop tint. The whole panel
subtly takes on the colour of the album currently playing. It is the single cheapest thing
we can do that makes the device feel designed rather than assembled — and step 3 is what
keeps it from also making the device unreadable.

### 5.3 Typography

Four sizes, no more. A 480 px panel read from across a room needs large type and ruthless
hierarchy.

| Role | Size | Weight |
|---|---|---|
| Track title | 40 px | Semibold |
| Artist | 26 px | Regular |
| Album / meta | 18 px | Regular |
| Labels / time | 15 px | Medium |

### 5.3b Glyph coverage and fallback

Fonts are generated at build time by `tools/gen_assets.py` with a **restricted glyph set**.
A full Unicode font at 40 px is megabytes, so coverage is a real decision with a memory cost.

**Decision: Latin-1 Supplement + Latin Extended-A**, plus the punctuation that actually
appears in track metadata (curly quotes, en/em dash, ellipsis, ×, ♭/♯). Roughly 250 glyphs
across four sizes. This covers essentially all Western European catalogue — Björk,
Sigur Rós, Beyoncé, Motörhead, Mylène Farmer, Antônio Carlos Jobim — which matches the
intended library.

**Not covered:** Cyrillic, Greek, Hebrew, Arabic, and CJK. Including CJK is the expensive
fork: thousands of glyphs, far too large to embed at these sizes, and it would force runtime
glyph paging from a flash partition — which reopens [adr/0005](adr/0005-assets-in-app-image.md)
and adds a font cache to the memory budget. Deliberately deferred, not overlooked.

**Fallback is mandatory, not optional.** Spotify metadata is user-generated and international
regardless of listening habits — one Japanese bonus track or a Cyrillic remix credit will
appear eventually. The rule:

- Unrepresentable glyphs render as **U+FFFD (replacement character)**, never as `.notdef`
  boxes or blank space, and never as garbled multi-byte fragments.
- The **UTF-8 decoder is the safety-critical part.** Truncating a multi-byte sequence at a
  buffer boundary must not produce a partial codepoint. `view_model` truncation is
  codepoint-aware, and this is host-tested with deliberately adversarial strings.
- A string that is *entirely* unrepresentable falls back to a meaningful alternative
  (album name, or "Unknown Track") rather than rendering as a row of replacement characters,
  which looks broken in a way an honest fallback does not.
- `svc_diagnostics` counts unrepresentable-glyph events. A rising count is the signal that
  the glyph set no longer matches the user's actual library — which is how we would know to
  revisit this decision with evidence rather than by guessing.

### 5.4 Metrics and motion

```c
THEME_RADIUS_CARD      20 px      THEME_SPACE_XS   4      THEME_SPACE_L   24
THEME_RADIUS_SHEET     28 px      THEME_SPACE_S    8      THEME_SPACE_XL  32
THEME_RADIUS_PILL      999        THEME_SPACE_M    16
THEME_TOUCH_MIN        56 px      /* a wall device is touched imprecisely */

THEME_MOTION_INSTANT   100 ms  ease-out    /* press feedback        */
THEME_MOTION_QUICK     200 ms  ease-out    /* toggles, toasts       */
THEME_MOTION_STANDARD  320 ms  ease-in-out /* screen transitions    */
THEME_MOTION_SLOW      600 ms  ease-in-out /* unused — see below     */
THEME_MOTION_AMBIENT  1200 ms  linear      /* backdrop drift        */
```

All animation goes through `lv_anim`. No manual per-frame timers, no `lv_refr_now()`.
Motion is what separates this from a dashboard, so it gets tokens and consistency rather
than per-site tuning.

**`THEME_MOTION_SLOW` is currently unused** — it was defined for the artwork crossfade
(§4's `w_art_card` entry), but that animation was removed on real hardware: animating the
art image's opacity forced LVGL's software renderer through a real alpha-compositing layer,
measured at 337ms for a single frame, well over R10's 100ms stall budget. The artwork switch
is instant now (placeholder on track change, a direct opacity pop once decode completes), no
`lv_anim` involved. Left defined rather than removed, in case a cheaper crossfade mechanism
(not forced through LVGL's layer-compositing path) replaces the current instant pop later.

---

## 6. Now Playing — the screen that matters

**Art-forward, not the card layout below's original "Balanced" design** — full-bleed artwork
with overlaid controls, chosen explicitly over this section's own original mock before any
Phase 5 build work started. This is what actually shipped:

```
┌────────────────────────────────────────────┐
│ (status pill, if not LIVE)   Living Room   │  top overlay: active Spotify Connect
│                                            │  device name (playback_state_t.device_name),
│                                            │  read-only — not the clock this section
│                                            │  originally mocked; see prose below
│    ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓  │
│    ▓▓▓▓▓▓▓  full-bleed album art  ▓▓▓▓▓▓▓  │  square corners, not rounded (§4)
│    ▓▓▓▓▓▓▓  + flat adaptive scrim ▓▓▓▓▓▓▓  │  solid opacity, not a gradient (§4)
│    ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓  │
│                                            │
│      Track Title Goes Here                 │  hero, marquee if long
│      Artist Name                           │  secondary
│      ──────────●─────────────────          │  accent-tinted, display-only (no scrub, §4)
│      1:24                        -2:41     │  tertiary
│         ◀◀        ⏸        ▶▶      -18dB   │  56 px targets, no heart (§4);
│                                            │  read-only Denon volume badge, no tap handler
│                  ● ○ ○                     │  page dots
└────────────────────────────────────────────┘
```

- Artwork drops to a neutral placeholder the *instant* the currently-playing track id
  changes (from `playback_state_t`, which updates immediately) — text/metadata update on the
  same event, so nothing lags. The new artwork pops in (no animation, §4) once its own
  `DOMAIN_ARTWORK` update, matching that same track id, actually arrives — that can take up
  to ~1.8s (RISKS.md R1), which is why these are driven by two separate track ids, not one:
  a real bug, found live, had the placeholder wait for the artwork-side id too, so the old
  cover visibly lingered under the new track's title/artist for that whole window.
- The progress bar animates continuously from local interpolation, corrected smoothly on
  each poll — never a visible jump.
- The device-name label (top-right) is `playback_state_t.device_name`, live, read-only. A
  tap-to-switch device picker (choose which Spotify Connect device is playing) was built and
  confirmed working on real hardware, then explicitly reverted at the user's request — the
  label is not wired to anything.
- The volume badge is `denon_state_t`'s own volume (Denon is the primary volume control,
  ADR-0007) — live, read-only. Tapping it does nothing in Phase 5; `w_volume_sheet` (a modal)
  is Phase 6.
- When `tone == STALE` or `OFFLINE`, a `w_status_pill` appears top-left. Content stays
  visible — we degrade legibly instead of blanking.
- Play/pause/prev/next are optimistic: the tap dims/disables the row immediately (not a full
  per-field state flip), then reconciles against a real `INTENT_ACK_EVT_ACCEPTED`/`REJECTED`
  or a 3 s abandon timeout, per §6.3 of [EVENTS_AND_STATE.md](EVENTS_AND_STATE.md).

---

## 7. Power-aware rendering

`svc_power` publishes state; the UI adapts through content, not backlight level — see below
for why the backlight itself has only one real transition to make.

| Power state | Backlight | UI behaviour |
|---|---|---|
| `ACTIVE` | on | full 60 fps, all animation |
| `DIM` (60 s idle) | on | ambient animation only; transport hidden |
| `ASLEEP` (300 s idle) | off | LVGL timers paused, no rendering at all |
| `NIGHT` | on | monochrome clock/track, no accent colour |

- **Backlight dimming is real, contrary to an earlier finding in this section** — Phase 5
  Tier 3's original hal_backlight bring-up concluded the backlight was binary-only because it
  assumed the IO expander was a CH422G with only a digital enable pin. Phase 5 Tier 7 real-
  hardware work found the real chip is a CH32V003 with a genuine PWM register (`REG_PWM`,
  0–255, inverted: 0x00 is full brightness, 0xFF is off) — confirmed by reading Waveshare's
  own official reference source directly, not assumed. Real dimming is possible on this
  hardware. `hal_backlight.c` still only exposes binary on/off today, because Phase 5's own
  scope doesn't need more than that (the backlight is fixed-on throughout `ACTIVE`/`DIM`/
  `NIGHT`, matching the table above); `DIM`/`NIGHT` still achieve their calmer feel entirely
  through content (animation reduction, monochrome rendering, hidden transport), not a
  backlight change. This is a scope decision for Phase 5, not a hardware limitation —
  Phase 6's `svc_power` is free to build real backlight ramping against this same PWM
  register when dim/sleep/wake actually gets designed; nothing here promises it will.
  `THEME_MOTION_AMBIENT` (§5.4) still doesn't apply to backlight in Phase 5, only to backdrop
  drift, since no backlight animation exists to apply it to yet.
- Wake on touch is handled in `svc_input` **before** the gesture recogniser: the first
  touch after sleep wakes the panel and is *consumed*, never delivered as a tap. Otherwise
  a user brushing the screen skips a track.
- Wake on playback: if Spotify starts playing while asleep (someone hit play elsewhere), the
  panel fades up. This is why polling continues at 30 s during sleep.

---

## 8. Testing the UI

- **View model and formatters** are pure C with no LVGL dependency → full host tests
  (`test/host/test_view_model.c`). This is the majority of the logic that can actually be
  wrong.
- **Screens and widgets** get a `ui_harness` build that runs LVGL against the SDL/Linux
  driver on the dev machine, so layout and motion can be iterated in seconds instead of
  flash cycles. Worth the setup cost within the first week — the alternative is a two-minute
  round trip for every 4-pixel adjustment, which is how UI polish silently gets abandoned.
- **On target**, an FPS and dropped-frame counter is always running and visible on the
  diagnostics screen.
