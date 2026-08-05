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
ui_router       the 4-screen pager, tap-only navigation, screen lifecycle
                (no gesture handling — see §3)
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
/* view_model_now_playing.h — the real, current struct (not the illustrative one
 * this doc originally sketched before any screen existed — text fields are sized
 * from the real source, playback_state_t's own PLAYBACK_*_MAX, not an arbitrary 96) */
typedef struct {
    char      title[PLAYBACK_TITLE_MAX];
    char      artist[PLAYBACK_ARTIST_MAX];
    char      album[PLAYBACK_ALBUM_MAX];
    char      elapsed[VM_ELAPSED_MAX];        /* "3:07"   */
    char      remaining[VM_REMAINING_MAX];    /* "-1:24"  */
    uint16_t  progress_permille;              /* 0..1000, already interpolated */
    uint32_t  duration_ms;                    /* raw value — w_progress_bar needs it to
                                                  convert a drag position into a seek target */
    bool      is_playing;
    bool      pending;                        /* optimistic intent in flight */
    char      volume_display[VM_VOLUME_DISPLAY_MAX];   /* "52.5" — Denon's own volume */
    uint16_t  volume_half_db_raw;              /* seeds the real volume slider's position */
    bool      has_active_device;
} now_playing_vm_t;
```

No `tone`/`accent` fields — both were built, then removed once real hardware and user
feedback overtook the design that needed them ([UI_THEME.md](UI_THEME.md) §5.2). Every other screen has its own equally
small pure `*_vm_t` and builder (`denon_vm_t`/`connection_vm_t`/`system_vm_t`).

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

Four peer screens on a horizontal pager. **Navigation is tap-only, via page-edge tabs** —
not swipe, and not page dots; both were built and then removed on real-hardware feedback
(see below).

```
      ◄─ tab ─►       ◄─ tab ─►        ◄─ tab ─►
┌──────────────┐ ┌──────────────┐ ┌──────────────┐ ┌──────────────┐
│    DENON     │ │ NOW PLAYING  │ │  CONNECTION  │ │    SYSTEM    │
│  (Receiver)  │ │  (default)   │ │              │ │              │
└──────────────┘ └──────────────┘ └──────────────┘ └──────────────┘
```

Order is fixed and never wraps. Now Playing is the default/boot screen. **System screens**
(boot · provisioning · ota-applying · fatal) aren't built; nothing triggers one yet.

**One overlay slot exists**, built with its first real caller (the Spotify pair screen,
[adr/0011](adr/0011-on-device-pair-page-over-console-paste.md)) rather than speculatively.
Deliberately one slot, not a stack — nothing here needs a modal over a modal, and a depth-N
stack would be four more states to get wrong for no caller. Reached by tapping the System
screen's Spotify row; `ui_router_register_overlay()` must be called *after* all four screens,
because `register_screen()` re-parks both page-edge tabs at child index −1 on every call.

**The overlay is a child of the pager, not `lv_display_get_layer_top()`** — a render-cost
decision, not a stylistic one. `lv_refr_get_top_obj()` looks for a fully-opaque covering
object only *within the active screen*, and uses it to skip drawing everything underneath. An
opaque, full-size pager child wins that search, so the four screens below are never traversed.
Anything in `layer_top` cannot: `lv_refr` draws the top/sys layers *in addition to* a full
active-screen pass. Under §2's partial-render single-draw-buffer path that is the difference
between one screen's worth of drawing and two. The culling depends on the overlay root staying
genuinely opaque — `bg_opa: COVER`, `radius: 0`, no transform, **no scrim**. Adding a
translucent scrim later would silently hand the cost back.

**Navigation — three real reversals, all driven by hardware, all worth recording:**

1. **Swipe/free-drag paging is gone.** Under this board's render path (§2), a continuous
   drag-scroll of two full-width screens redraws a large dirty rect on essentially every
   touch-move tick — the most expensive pattern available here, and the direct cause of a
   real "too slow" report. Replaced by tap-only page-edge tabs (`w_page_edge_tab`).
2. **The 180 ms slide that replaced it is also gone.** Even a bounded, one-shot tween tiled
   several full-width redraw passes per animated frame and looked choppy on real hardware.
   `ui_router_go_to_screen()` is now an instant jump (`LV_ANIM_OFF`).
3. **`w_page_dots` is deleted, not deferred.** The edge tabs already encode both position and
   direction, so a separate indicator was redundant clutter.

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

- **All four screens and the overlay are created once at registration and never destroyed.**
  Paging must be instant and allocation-free, and nothing here is created on demand. That
  matters most for the overlay: its QR canvas is a ~7.7 KB draw buffer, and this board's
  history includes LVGL allocation failures under UI-task memory pressure. Paying it at boot
  turns a possible mid-session hang into a deterministic `ui_app_create()` failure.
- `ui_router_update()` only forwards to the screen currently being *looked at*. An off-screen
  screen's widgets would still do real invalidation/layout work if updated, before LVGL ever
  clips the drawing.
- `mount`/`unmount`/`destroy` are no-op bodies on all four *screens*: subscriptions live in
  each screen's own `view_model_*_apply_t` glue (driven by `ui_app_task`), not in the screen
  vtable. **The pair overlay is the first real use of these hooks** — its `mount()` submits
  `INTENT_DOMAIN_PAIRING`/START and its `unmount()` submits STOP, which is precisely what
  bounds the pairing HTTP listener's lifetime to the screen's. The contract still stands for
  any future screen that starts an animation or timer — `unmount` **must** stop everything it
  started; a leaked `lv_anim` on a hidden screen is a silent frame-rate tax that is miserable
  to find later.

---

## 4. Widget catalogue

Reusable, parameterised, no screen-specific branching inside them.

Originally a 14-widget catalogue written for a "Balanced" card-layout mock. Phase 5 built
Art-forward instead and only the widgets that layout actually uses; Phase 6 added its own as
its three real screens landed. Several of the original 14 turned out to answer a design that
no longer exists — those are marked cut, not pending.

**Built, Phase 5 (Now Playing):**

| Widget | Purpose |
|---|---|
| `w_art_card` | Full-bleed album art + integrated scrim, placeholder state. **No animated crossfade** — a real finding, not the original design: `w_art_card_set_artwork()` drops to the neutral placeholder the *instant* the currently-playing track id changes (from `playback_state_t`, which updates immediately), independent of whether the new artwork has finished fetching/decoding yet (that can lag up to ~1.8s, RISKS.md R1); once it does, the new image pops in at full opacity directly, not an animated fade — an earlier animated opacity transition (TRANSP→COVER over 600ms) forced LVGL's software renderer through a real alpha-compositing layer for every frame, measured at 337ms for a single frame on real hardware, well over R10's 100ms stall budget. Filed as a real follow-up: either root-cause LVGL's layer-alloc cost directly, or fade via a cheaper mechanism that doesn't force a layer. **Square corners, not rounded** — a separate real finding: with rounded corners (`clip_corner`) enabled, LVGL's own compositing-layer allocator (`lv_draw_layer_alloc_buf`) never returns on this real hardware/LVGL-version combination, confirmed stuck inside `lv_tlsf.c`'s own block search, not failing or asserting, at every pool size tried. Root cause not found; `clip_corner` is off as the working fix. The scrim itself is a flat, solid, per-track-adaptive opacity, not the vertical gradient this section originally described (§6) — RGB565's 16-bit color depth made a smooth opacity ramp visibly band on real hardware. |
| `w_marquee_label` | Text that gently scrolls only when it overflows, pauses at each end |
| `w_progress_bar` | Interpolated progress + elapsed/remaining, **with real drag-to-seek** — the "display-only, nothing to wire a drag to" note this row used to carry is obsolete: real-hardware feedback asked for a working scrubber, so `PLAYBACK_INTENT_SEEK` was built end to end (`playback_intent_types.h`, `spotify_intent.c`, `spotify_api.c`'s `PUT .../seek`). Commits once on release, not per touch-move sample — a real Spotify call has none of the local coalescing Denon's cmdq gives volume drags for free. `VALUE_CHANGED` previews the drag (timestamps derived from the dragged position, no intent); `RELEASED` submits. Styled identically to `w_volume_slider` — see [UI_THEME.md](UI_THEME.md) §5.4's `THEME_TRACK_H` note |
| `w_transport` | Prev / play-pause / next, with the pending-state treatment. Real drawn triangles (`lv_draw_triangle`), not symbol-font glyphs. **No heart/favorite** — no Spotify save/favorite endpoint exists anywhere in this project's own endpoint list, so a heart icon would be a permanently-dead tap target |
| `w_volume_slider` | The real, live-dragging Denon volume control (§6). Submits `INTENT_DOMAIN_MULTI` per [adr/0007](adr/0007-denon-volume-is-primary.md), never a raw Denon intent — that would bypass the Spotify-retarget resolver |
| `w_stroke_label` | Black-stroked overlay text, legible against unpredictable artwork. Art-forward has no scrim behind the title/artist block, so the stroke is the only thing carrying contrast there |
| `w_toast` | Transient bottom message, auto-dismiss, queued — `TOPIC_TOAST`'s real consumer |
| `w_page_edge_tab` | The real navigation control (§3) — tap-only, router-owned, shared across all four screens |

**`clip_corner` is the hazard, not `radius`** — worth stating, since `w_art_card`'s entry
reads like a blanket ban on rounded corners and isn't one. Per `lv_refr.c`, the ARGB8888
`lv_draw_layer_create()` that never returned is reached *only* when `clip_corner` is set
**and** the object has children; plain `radius` never goes near it, and all three tracks have
round ends without trouble. `radius` does cost the fast path (`lv_draw_sw_fill.c` single-blends
only at `radius == 0`, else per-draw `lv_malloc` + row-by-row masking, and at 14px tall no
fast middle section remains) — measured, and affordable (§8, [RISKS.md](RISKS.md) R10).

**Built, Phase 6 (Receiver / Connection / System):**

| Widget | Purpose |
|---|---|
| `w_power_glyph` | Ring+notch power glyph, tri-state colour (gray off / green on+healthy / amber on+link-uncertain). Built in Phase 5 for a Now Playing header that was then removed; kept for exactly this caller, and now real |
| `w_power_row` | Full-card tap target wrapping `w_power_glyph` + a state label + a decorative switch-look indicator. Both tap surfaces converge on the one real `w_power_glyph_toggle()` — never a duplicated toggle path |
| `w_source_status_card` | Read-only "which source/transport is active" card, in a row of two. Used twice: Receiver's Spotify/TV-audio pair, Connection's Telnet/HTTP pair (via a no-icon variant). Deliberately **not** tappable — see below |
| `w_info_row` | `label ......... value`, with an optional leading status dot and tone-coloured value. Every row on Connection and System |
| `w_volume_cap_slider` | The volume safety-cap control. Amber, not green — it constrains rather than plays. Submits `INTENT_DOMAIN_SETTINGS`, the first real user of that domain (§4b) |
| `w_qr` | A QR code on a white card, wrapping LVGL's vendored `lv_qrcode`. Used only by the pair overlay. The white card *is* the quiet zone — `lv_qrcode`'s own option leaves only the integer-division remainder, and a dark quiet zone is the most common reason a rendered code won't scan. Guards by reading the canvas size back, because `lv_qrcode_set_size()` fails silently and leaves the constructor's 130 px buffer in place, and a canvas narrower than the module count renders a blank white square with `LV_RESULT_OK` and no log |

`w_source_status_card` has no click handler at all, on purpose. On the Receiver screen the
power toggle already fully determines `input_id` as a binary choice (on → `DENON_INPUT_NET`,
off → `DENON_INPUT_TV`), so a second independently-tappable input control could only fight
it. This also avoids re-entering a known hazard: an earlier tappable multi-row picker
(`w_device_picker`) was built and then fully reverted after a real hardware hang traced to
per-object `lv_style_t` allocation in a loop.

**Cut, not deferred:**

| Widget | Why |
|---|---|
| `w_backdrop` | Not needed under Art-forward — the art card *is* the backdrop |
| `w_page_dots` | Built, then deleted — the edge tabs already encode position and direction (§3) |
| `w_status_pill` | Built, never called. Its STALE/OFFLINE signal moved to the System screen's tone-coloured rows — a better home than a pill over artwork. Kept in-tree, no caller |
| `w_list_row` | Superseded by `w_info_row` — the same idea, built against a real screen |

**Still genuinely unbuilt** (each waits on a feature that doesn't exist yet, not on UI work):
`w_icon_toggle` (shuffle/repeat/mute), `w_volume_dial` + `w_volume_sheet` (a modal volume
sheet — the overlay slot now exists (§3), but nothing has asked for this control),
`w_picker` (input/listening-mode pickers).

Each widget owns a small `*_cfg_t` and exposes `create(parent, cfg)` plus `set_*` updaters.
No widget reads global state or the event bus — everything arrives through its setters.
That is what makes them reusable and what keeps duplicated UI code from reappearing.

### 4b. How a widget changes a *setting*

Every other UI control submits an intent a service consumes directly. The volume-cap slider
is the first that changes a stored **setting**, and that path did not exist:
`INTENT_DOMAIN_SETTINGS` had been declared in `intent_types.h` since Phase 1 but had no
payload shape and no dispatch case anywhere — a repo-wide search confirmed it was dead.

```
w_volume_cap_slider ──INTENT_DOMAIN_SETTINGS──► dispatch_one_intent()   (app_wiring_ticks.c)
                                                        │  settings_set_u8()
                                                        ▼
                       svc_denon_set_volume_limit() ◄── TOPIC_SETTINGS relay (already existed)
```

- **`ui` never touches `svc_settings`.** It can't — `tools/arch_rules.json` gives `ui` no
  `svc` edge. The intent's shape lives in `core_types` (`settings_intent_types.h`), reachable
  by both `ui` (to build it) and `main` (to apply it) — the same "moved, not duplicated"
  treatment `denon_intent_types.h` already got.
- **Only the submit side was new.** The relay already existed, so a setting written this way
  lands everywhere a console-written one would.

---

## 5. Theme

Colour, type, metrics, and motion tokens — moved to **[UI_THEME.md](UI_THEME.md)** when this
file outgrew the 400-line limit (`tools/check_limits.py`). The rule that matters here: no hex
colour, pixel size, or duration appears outside `theme/`.

---

## 6. The screens themselves

Layouts, per-screen behaviour, and the real findings behind each — moved to
**[UI_SCREENS.md](UI_SCREENS.md)** when this file outgrew the 400-line limit
(`tools/check_limits.py`). Four screens: Now Playing, Receiver, Connection, System.

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
  Tier 3's bring-up concluded binary-only because it assumed the IO expander was a CH422G
  with just a digital enable pin. Tier 7 found the real chip is a CH32V003 with a genuine PWM
  register (`REG_PWM`, 0–255, inverted) — confirmed against Waveshare's own reference source.
  `hal_backlight.c` still exposes only on/off, because nothing needs more yet: the backlight
  is fixed-on throughout `ACTIVE`/`DIM`/`NIGHT`, and those states get their calmer feel from
  content (animation reduction, monochrome, hidden transport). A scope decision, not a
  hardware limit — `svc_power` can build real ramping against that PWM register when
  dim/sleep/wake actually gets designed. `THEME_MOTION_AMBIENT` ([UI_THEME.md](UI_THEME.md)) accordingly applies
  only to backdrop drift, not backlight.
- Wake on touch is handled in `svc_input` **before** the gesture recogniser: the first
  touch after sleep wakes the panel and is *consumed*, never delivered as a tap. Otherwise
  a user brushing the screen skips a track.
- Wake on playback: if Spotify starts playing while asleep (someone hit play elsewhere), the
  panel fades up. This is why polling continues at 30 s during sleep.

---

## 8. Testing the UI

- **View model and formatters** are pure C with no LVGL dependency → full host tests, one
  suite per module (`test_view_model_now_playing`, `_denon`, `_connection`, `_system`,
  `_format`, `_text`, `_tone`, `_progress`, plus `test_theme_tokens`) — not the single
  `test_view_model.c` this line originally named. This is the majority of the logic that can
  actually be wrong.
- **Screens and widgets** get a `ui_harness` build (`test/ui_harness/`) running LVGL against
  SDL2 on the dev machine, linking the *same* router/screen/widget/view-model sources ESP-IDF
  compiles, against fake `core_state`/`core_eventbus` fixtures. Two honest caveats: glue that
  touches ESP-IDF directly can't link on host (`ui_app.c`, and `view_model_system_apply.c` —
  the harness substitutes `fake_system_apply.c`), and **this project's real UI bugs have
  consistently been found on hardware, not here** — the compositing-layer hang, the
  render-cost measurements, the corner artifact, the boot-order bugs. Treat a clean harness
  run as "it builds and doesn't crash", not as visual verification.
- **On target**, `DOMAIN_UI` also carries `max_loop_us` — peak UI-task iteration per publish
  window (a peak, so it resets each window, unlike its neighbours). It exists because the
  600 ms stall assertion is a poor early-warning system; see [RISKS.md](RISKS.md) R10 for the
  reasoning and the measured reference points.
- **On target**, frame and dropped-frame counters run continuously (`DOMAIN_UI`) and are
  reported over UART by `svc_diagnostics` every 5 s. There is no on-device diagnostics
  screen; the System screen (§6c) shows link health and identifiers, not frame counters.
