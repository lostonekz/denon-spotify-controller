# UI Theme

Colour, type, metrics, and motion tokens. Companion to
[UI_ARCHITECTURE.md](UI_ARCHITECTURE.md) (layering, widgets, navigation, testing) and
[UI_SCREENS.md](UI_SCREENS.md) (what each screen actually looks like).

Split out of UI_ARCHITECTURE.md when it outgrew this project's own 400-line limit
(`tools/check_limits.py`) — the same reason several `.c` files here are split, and the same
move already made for the screens.

---
## 5. Theme

### 5.1 Tokens, never literals

No hex colour, pixel size, or duration appears outside `theme/`. Screens and widgets refer
to semantic roles only.

The real, current token list (`theme_colors.h`) — these are the OKLCH-derived values the
Art-forward redesign computed via `theme_tokens_oklch_to_rgb565()`, not hand-picked hex:

```c
THEME_COLOR_BG              /* #090C0A  near-black, slightly cool          */
THEME_COLOR_SURFACE         /* #141417  cards                              */
THEME_COLOR_SURFACE_HIGH    /* #1F1F24  raised / pressed                   */
THEME_COLOR_TEXT_PRIMARY    /* #F5F5F7                                     */
THEME_COLOR_TEXT_SECONDARY  /* #A0A0A8                                     */
THEME_COLOR_TEXT_TERTIARY   /* #6A6A72                                     */
THEME_COLOR_TEXT_ONIMAGE    /* #E4E4E4  overlay text over full-bleed art   */
THEME_COLOR_TEXT_STROKE     /* #000000  w_stroke_label's outline           */
THEME_COLOR_ACCENT_LIVE     /* #4ADD8C  healthy/live green                 */
THEME_COLOR_STALE           /* #C9A227  the honest "data is old" tint      */
THEME_COLOR_DANGER          /* #D9534F  offline / rejected                 */
```

Semantic naming is what makes a light theme, a night theme, or a rebrand a one-file change
three years from now.

### 5.2 Dynamic accent — built, then removed from the UI

`artwork_palette.c` extracts a dominant colour during JPEG decode (it already has the
decoded buffer, so this is nearly free): downsample, histogram, discard
near-black/near-white/low-saturation bins, take the modal hue, boost saturation, clamp
luminance for contrast, and publish it as `artwork_state_t.dominant_color_rgb565`.

**Nothing in the UI consumes it today.** The accent had exactly two consumers — the progress
bar's fill colour and the old scrim's contrast logic — and the Art-forward redesign removed
both (the scrub bar is flat white per the design spec, "no scrim, no gradient, no vignette…
traded away for the art"). `now_playing_vm_t.accent_rgb565` is gone with them. The
extraction itself stays: it's real, general-purpose `core_state` data with its own
service-level existence, and a UI-only redesign has no business deleting it upstream. If a
future screen wants a per-track accent, the data is already there.

### 5.3 Typography

The real, current font set (`theme_fonts.h`). Two families — Archivo for reading, IBM Plex
Mono for numbers that change in place, so digits don't jitter:

| Token | Size / weight | Used for |
|---|---|---|
| `ui_font_display` | 44 / 700 | reserved — no current caller |
| `ui_font_hero` | 38 / 700 | Now Playing's track title |
| `ui_font_title` | 30 / 600 | System's verdict headline |
| `ui_font_body` | 20 / 400 | Now Playing's artist line, toasts |
| `ui_font_label` | 16 / 500 | row labels/values, card titles |
| `ui_font_meta` | 13 / 500 mono | captions, section headers, subtitles, timestamps |
| `ui_font_meta_lg` | 15 / 500 mono | volume readouts |

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
  (album name, or "--") rather than rendering as a row of replacement characters,
  which looks broken in a way an honest fallback does not.
- `svc_diagnostics` counts unrepresentable-glyph events. A rising count is the signal that
  the glyph set no longer matches the user's actual library — which is how we would know to
  revisit this decision with evidence rather than by guessing.

### 5.4 Metrics and motion

```c
THEME_RADIUS_CARD      20 px      THEME_SPACE_XS   4      THEME_SPACE_L   24
THEME_RADIUS_SHEET     28 px      THEME_SPACE_S    8      THEME_SPACE_XL  32
                                  THEME_SPACE_M    16
THEME_TOUCH_MIN        56 px      /* a wall device is touched imprecisely */
THEME_TRACK_H          14 px      /* drawn height of every draggable track  */

THEME_MOTION_INSTANT   100 ms  ease-out    /* press feedback        */
THEME_MOTION_QUICK     200 ms  ease-out    /* toggles, toasts       */
THEME_MOTION_STANDARD  180 ms  ease-in-out /* unused — see below    */
THEME_MOTION_SLOW      600 ms  ease-in-out /* unused — see below    */
THEME_MOTION_AMBIENT  1200 ms  linear      /* backdrop drift        */
```

No `THEME_RADIUS_PILL` — a fully round shape uses LVGL's own `LV_RADIUS_CIRCLE` directly; a
token aliasing an LVGL constant adds a name without adding a decision. All three draggable
tracks (scrub, volume, volume cap) do exactly that.

`THEME_TRACK_H` *is* a decision, so it gets a token: "every draggable track looks and feels
the same" is a stated requirement, and three copies of `14` would drift. Pair it with
`THEME_TOUCH_MIN` for the hit band — `(THEME_TOUCH_MIN - THEME_TRACK_H) / 2` of invisible
padding each side. The scrub bar was the one control quietly short of the 56px rule, its
padding a hand-picked `18` rather than derived; it isn't any more.

All animation goes through `lv_anim`. No manual per-frame timers, no `lv_refr_now()`.

**Two of the five motion tokens are currently unused, for the same real reason:** this
board's render path (`LV_DISPLAY_RENDER_MODE_PARTIAL`, a single small draw buffer) makes
full-screen animation genuinely expensive, and both animations that would have used them
were measured and removed rather than shipped choppy — `THEME_MOTION_SLOW` was the artwork
crossfade (337 ms for a single frame through an alpha-compositing layer, well over R10's
100 ms budget — §4's `w_art_card` entry), and `THEME_MOTION_STANDARD` was briefly the
page-transition slide (§3). Both are left defined in case a cheaper mechanism makes either
viable later. Motion is still what separates this from a dashboard — but not at the cost of
a visibly worse frame.

---
