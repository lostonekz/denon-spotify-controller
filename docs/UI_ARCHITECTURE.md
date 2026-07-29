# UI Architecture

480 × 480 square panel. LVGL 9.x. One task, core 1, never blocks.
Companion to [ARCHITECTURE.md](ARCHITECTURE.md).

---

## 1. Layering

```
ui_app          owns lv_display, the root object, the event subscription,
                and the UI task loop. ~150 lines. Knows no screen internals.
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

| Widget | Purpose |
|---|---|
| `w_art_card` | Album art with rounded corners, shadow, crossfade between two artwork slots, placeholder state |
| `w_backdrop` | Full-bleed blurred album art + scrim behind everything |
| `w_marquee_label` | Text that gently scrolls only when it overflows, pauses at each end |
| `w_progress_bar` | Interpolated progress + elapsed/remaining, scrub-on-drag |
| `w_transport` | Prev / play-pause / next, with the pending-state treatment |
| `w_icon_toggle` | Shuffle, repeat, mute — off / on / on-with-variant (repeat-one) |
| `w_volume_dial` | Arc drag control; used for both Spotify and Denon volume |
| `w_volume_sheet` | Modal with dial, numeric dB, mute, safety-limit indication |
| `w_list_row` | Label + value + optional chevron/toggle. Every settings and picker row. |
| `w_picker` | Modal single-select list (inputs, listening modes, enum settings) |
| `w_toast` | Transient bottom message, auto-dismiss, queued |
| `w_status_pill` | Small connectivity/link indicator; the honest STALE/OFFLINE surface |
| `w_qr` | QR rendering for provisioning and Spotify re-pair |
| `w_page_dots` | Pager position indicator |

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
THEME_MOTION_SLOW      600 ms  ease-in-out /* artwork crossfade     */
THEME_MOTION_AMBIENT  1200 ms  linear      /* backdrop drift, dim   */
```

All animation goes through `lv_anim`. No manual per-frame timers, no `lv_refr_now()`.
Motion is what separates this from a dashboard, so it gets tokens and consistency rather
than per-site tuning.

---

## 6. Now Playing — the screen that matters

```
┌────────────────────────────────────────────┐
│  ▓▓▓ blurred artwork backdrop + scrim ▓▓▓  │
│                                            │
│         ┌──────────────────────┐           │
│         │                      │           │
│         │    album artwork     │  300 px   │
│         │    rounded 20 px     │  card     │
│         │                      │           │
│         └──────────────────────┘           │
│                                            │
│      Track Title Goes Here                 │  40 px, marquee if long
│      Artist Name                           │  26 px, secondary
│                                            │
│      ──────────●─────────────────          │  accent-tinted, scrubbable
│      1:24                        -2:41     │  15 px, tertiary
│                                            │
│       ⤨       ◀◀     ⏸     ▶▶      ↻      │  56 px targets
│                                            │
│                  ● ○ ○                     │  page dots
└────────────────────────────────────────────┘
```

- Artwork crossfades over 600 ms on track change; the backdrop and accent colour cross-fade
  with it. Nothing ever pops.
- The progress bar animates continuously from local interpolation, corrected smoothly on
  each poll — never a visible jump.
- Volume is not on this screen. A swipe up (or a hardware-adjacent gesture) raises the
  volume sheet. Keeping the primary screen calm is the whole point.
- When `tone == STALE`, text drops to secondary and a `w_status_pill` appears. Content
  stays visible. We degrade legibly instead of blanking.

---

## 7. Power-aware rendering

`svc_power` publishes state; the UI adapts rather than just dimming the backlight:

| Power state | Backlight | UI behaviour |
|---|---|---|
| `ACTIVE` | user setting | full 60 fps, all animation |
| `DIM` (60 s idle) | 15 % | ambient animation only; transport hidden |
| `ASLEEP` (300 s idle) | 0 %, panel off | LVGL timers paused, no rendering at all |
| `NIGHT` | 3 % | monochrome clock/track, no accent colour |

- Backlight changes **always ramp** (`THEME_MOTION_AMBIENT`, gamma-corrected). An instant
  backlight step is the single most "cheap DIY" thing a device can do.
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
