# Roadmap

Sequenced to retire risk early and to get a *usable* device in hand quickly, because a
device you live with reveals design problems that no amount of review does.

**This is currently a personal dev/test device, not a product in progress.** Production-track
work (manufacturing, factory test, Secure Boot/flash encryption, RMA/fleet operations) is
parked in [PRODUCTION_DEFERRED.md](PRODUCTION_DEFERRED.md), not deleted — see that file for
why and for what moves back here if that changes.

---

## Phase 0 — Bring-up and risk spike

**Goal: answer R1 before the architecture is frozen.** Throwaway code, no structure.

- Blink, UART, WiFi association. Confirm N16R8 and that octal PSRAM is running at ≥ 80 MHz.
- `bsp_board`: I²C, IO expander, reset lines, power-up ordering.
- ST7701 init over `esp_lcd_rgb_panel`; a moving gradient at 60 fps.
- GT911 touch reporting coordinates.
- **The bandwidth spike (R1):** 60 fps gradient *while* pulling a 5 MB HTTPS download,
  decoding a 640×640 JPEG in a loop, and writing NVS. Instrument dropped frames and tearing.
- Backlight PWM ramp; read the internal temperature sensor.

**Exit criteria:** measured frame rate under combined load, and a written decision on
framebuffer count, refresh rate, and bounce-buffer size. If it tears, the memory plan
changes *now*, not in Phase 5.

**Met.** Measured on real hardware under real combined load (including network traffic
actually transferring, not silently idle — an earlier pass got this wrong, see
[RISKS.md](RISKS.md) R1): 6.7–7.1% dropped frames. Decision: 2 framebuffers, 60 Hz, 20-row
(38 KiB) bounce buffers, WiFi/LWIP preferring PSRAM — the original settings, kept after five
fallbacks (larger buffers, three refresh rates, JPEG task priority, moving WiFi off PSRAM)
all left the rate unchanged or within noise. Budget accepted at 10%. Full reasoning and
numbers in [RISKS.md](RISKS.md) R1 and [MEMORY.md](MEMORY.md) §1. **The framebuffer-count
part of this decision was later reversed in Phase 5 Tier 7**, once the real full application
(not this phase's own spike) existed — see [RISKS.md](RISKS.md) R1's own Phase 5 Tier 7
update for why, and for a second, unrelated root cause this phase's spike had no way to
exercise. **The refresh-rate part was also later revisited, twice** — Tier 7 retried 30 Hz
against that same real application and found it worse, then a post-Tier-7 performance sweep
found that result was itself confounded by a bug fixed in the same tier, retried 30 Hz again
clean, and settled there for the PSRAM headroom. Current setting: 30 Hz. Neither revision
changes this phase's own 6.7–7.1%/10%-budget finding, which was never about a specific
refresh rate.

---

## Phase 1 — Skeleton and foundations

No features. This is the substrate everything else sits on, and retrofitting it is
expensive.

- `core_eventbus` (per-topic policies + injected release hooks), `core_state`, `core_fsm`,
  `core_pool`, `core_types`
- `svc_supervisor`: boot dependency graph, readiness model, restart policy
  ([LIFECYCLE.md](LIFECYCLE.md)) — built early because retrofitting lifecycle ownership after
  five services already improvise their own is the expensive order
- `svc_storage`, `svc_settings` (schema + migration + debounced writes), `svc_logging`,
  `svc_diagnostics` (including coredump retention and export)
- `app_start` / `app_wiring` composition root; all tasks created, all pools allocated
- Host test harness (Unity) running in CI, with the first tests on the FSM engine and store
- `tools/check_includes.py`, `check_limits.py` wired into CI

**Exit criteria:** device boots, all tasks running, diagnostics reports heap and stack
watermarks over UART, CI green, layering violations fail the build.

**Met.** All of Tier 0–3 built and verified: `core_types`/`core_pool`/`core_fsm`/
`core_eventbus`/`core_state`, then `svc_storage`/`svc_scheduler`/`svc_logging`/
`svc_settings`/`svc_diagnostics`/`svc_supervisor`, then the `app_wiring`/`app_start`
composition root — which replaced and fully removed the Phase 0 spike, per
`arch_rules.json`'s own note that its presence past this point is a bug. 13 Unity suites
pass on host, the 9-gate local suite is green, and a clean ESP-IDF rebuild produces zero
compiler warnings. Confirmed on real hardware: boot completes, `svc_supervisor` reports
all 5 services `READY`, and `svc_diagnostics` reports real heap/PSRAM/per-task-stack
numbers over UART every 5 s — the coredump-retention path also found and correctly
recorded a genuine leftover dump from earlier bring-up, an unplanned proof against real
data rather than a fake. Two real gate bugs found while implementing (the `core` layer
had no self-edge, so no core module could include another core module's header; this
doc's own `arch_rules.yml` references were stale — the gate reads `.json`) are fixed.
Coredump *export* (USB/HTTP, pairing PIN) is deferred to Phase 2+/5+ — its prerequisites
(`net_http`, a fault screen) don't exist yet; retention and UART reporting, which the exit
criteria actually asks for, are done. `kv_store_if_t` lives in `core_types`, not
`svc_storage`, so sibling services can depend on the interface without a forbidden
`svc`→`svc` edge; the same constraint is why `svc_settings`/`svc_diagnostics` never call
`svc_scheduler` directly and instead expose a plain `_tick()` that `app_wiring` schedules.

---

## Phase 2 — Connectivity and the update path

- `net_wifi` connection FSM with backoff + jitter
- SoftAP provisioning portal (**stage 1 only** — Spotify OAuth is Phase 4, see
  [NETWORKING.md §1.2](NETWORKING.md))
- `net_time` (SNTP), `net_http` with the TLS arbiter and injected policies
- `svc_ota`: manifest fetch, **manifest signature verification**, `security_version`
  anti-rollback, SHA-256, image signature, health gate, rollback
- Signing tooling: dev key, manifest generator, `tools/sign_release.py`
- **Measured internal-SRAM table** under WiFi + TLS load, feeding [MEMORY.md](MEMORY.md) §1

**Exit criteria — three independent proofs, all unattended:**

1. A deliberately-broken image is flashed via OTA and the device **rolls itself back**.
2. An image signed with the **wrong key is rejected** before the slot is marked bootable.
3. An image with a **lower `security_version` is refused**.

The recovery path is the feature, not the update path. All three are tested before anything
depends on OTA, and the signature path is exercised on every developer build thereafter —
which is the entire point of [adr/0006](adr/0006-staged-security.md).

---

## Phase 3 — Denon service (before Spotify, deliberately)

Denon is local, needs no OAuth, and gives instant push feedback. It exercises the entire
intent → service → state → event → UI path with a fraction of Spotify's setup cost, so the
core architecture gets validated against a real integration early and cheaply.

- `denon_codec.c` with an exhaustive host test table (including the `MV` half-step cases)
- `denon_telnet.c`, `denon_http.c`, transport selection and fallback
- Command queue with coalescing and rate limiting
- Discovery, capability probe, volume safety clamp
- A crude debug UI — text on screen, no theme

  **Implemented instead as a UART debug console** (`esp_console` REPL, `CONFIG_DSC_DENON_DEBUG_CONSOLE`),
  a deliberate deviation from this line: no `hal_display`/LVGL bring-up exists yet (Phase 5
  scope), and none of this phase's actual exit criteria need an on-screen UI to verify. The
  real on-screen Denon screen is still Phase 6, as originally planned.

**Exit criteria:** volume, power, mute, input, and listening mode all work with sub-100 ms
feedback over telnet, degrade cleanly to HTTP when the link is stolen, and the safety clamp
holds under a fuzzed intent stream.

**Verified against a real AVR-X3800H:** command round-trips (power/input/volume/mute/mode,
including the AVR's own echo), and the safety clamp holding under a real fuzzed volume-intent
burst (requests up to the protocol's own 98.5 dB max all clamped to `volume_limit_max` before
reaching the wire) — both via a scripted boot-time self-test
(`CONFIG_DSC_DENON_SELFTEST`), added after scripting interactive console input over this
board's native USB-CDC port proved unreliable from Windows. **Not verified**: the HTTP-fallback
trigger itself. A second telnet client (opened directly, and again while actively sending a
command) did not disrupt the ESP32's existing session at all — the AVR appears to broadcast
state to every connected socket rather than enforcing one client, contradicting
[adr/0003](adr/0003-denon-dual-transport.md)'s assumption. The fallback FSM logic is still
correct and exhaustively host-tested; only the real-world trigger condition couldn't be
reproduced against this specific unit/firmware.

---

## Phase 4 — Spotify service and artwork

- `spotify_auth.c`: PKCE, QR handoff, rotated-token persistence

  **Implemented instead as a manual code paste over the UART debug console** (`spotify pair`
  prints the authorize URL; `spotify code <redirect_url>` parses the pasted callback URL and
  validates `state` before any exchange) — a deliberate deviation, same reasoning Phase 3
  gave for its own UART debug console: no `hal_display`/LVGL bring-up exists yet (Phase 5
  scope) to render a QR code against. See
  [adr/0009](adr/0009-manual-code-paste-over-bounce-page.md).
- `spotify_api.c`, `spotify_parse.c` (fixture-driven host tests), `spotify_poll_policy.c`
- `svc_artwork`: fetch, ½-scale decode, slot pool with generation handles, palette
  extraction
- Progress interpolation against the monotonic clock

**Exit criteria:** playback state is accurate, transport controls work, artwork appears
within ~1.5 s of a track change, and the device survives a 48 h run across multiple token
refreshes without losing auth.

---

## Phase 5 — UI system

The theme and widget layer, built once, properly.

- `theme/` tokens, generated fonts and icons, `tools/gen_assets.py`
- `ui_app`, `ui_router`, the pager, screen lifecycle
- `view_model` + host tests
- Widget catalogue
- Now Playing screen, fully realised
- The SDL host harness, so UI iteration is seconds not minutes

**Exit criteria:** Now Playing is genuinely finished — smooth artwork transitions, dynamic
accent, interpolated progress, marquee, stale-state treatment.

**Met, with real, named exceptions — not silently shipped half-working.** Artwork
transitions (an immediate switch to a neutral placeholder the instant the track changes,
then an instant pop to the new artwork once it's decoded — not the animated crossfade
originally planned; a real hardware finding, see below), per-track accent (from artwork
luma), interpolated progress, marquee, and STALE/OFFLINE treatment are all real, on real
hardware. The animated crossfade was tried and removed: animating the artwork's opacity
forced LVGL's software renderer through a real alpha-compositing layer, measured at 337ms
for a single frame — well over R10's 100ms stall budget — so the instant-pop version above
shipped instead ([RISKS.md](RISKS.md) R1 / [UI_ARCHITECTURE.md](UI_ARCHITECTURE.md) §4).
Explicitly deferred, not forgotten: progress-bar
scrub (no SEEK intent or endpoint exists anywhere in this project — `spotify_intent.h`
documents this directly), the volume badge is read-only (`w_volume_sheet`, the tap target for
it, is Phase 6), and the heart/favorite control was cut outright, not deferred (no
favorite/save endpoint exists to wire it to). See [UI_ARCHITECTURE.md](UI_ARCHITECTURE.md)
§4/§6 for the full, real widget catalogue and screen layout actually built (Art-forward, a
deliberate departure from this doc's original "Balanced" card-layout description) and for one
real, still-open follow-up (the art card's corners are square, not the intended rounded, due
to a real LVGL rendering bug, not a design change).

This phase also absorbed real hardware bring-up work that Phase 0's own bullet list never
named (`bsp_board`/`hal_display`/`hal_touch`) — a deliberate scope decision (Phase 0's own
bring-up was explicitly throwaway, fully removed before Phase 1), not a gap. That bring-up
needed real, substantial correction once actual hardware existed: wrong IO-expander chip and
protocol, wrong RGB/I²C pin assignments, the display driver's generic default ST7701 init
sequence instead of this real panel's own, and several memory-budget and scheduling bugs
invisible to any simulator or spike test. Full account in [RISKS.md](RISKS.md) R1 and
`components/bsp_board/include/bsp_pins.h`'s own top comment.

---

## Phase 6 — Remaining screens and polish

- Denon screen, Settings, modals and pickers, provisioning and re-pair screens
- Motion pass: every transition reviewed against the motion tokens
- `svc_power`: dim/sleep/wake FSM, wake-on-touch consumption, wake-on-playback, night mode
- `svc_input` gesture recognition and touch-target audit

**Exit criteria:** no unstyled screens, no instant backlight steps, no dead-end states.

---

## Phase 7 — Hardening and soak

- **7-day soak**: heap slope per capability, LVGL pool fragmentation, stack watermarks, NVS
  write count, temperature, reconnect counts
- **Fault injection matrix**, each run unattended and each expected to self-recover:

| Injected fault | Expected behaviour |
|---|---|
| WiFi AP powered off mid-playback | `DEGRADED`, stale content retained, auto-reconnect |
| AVR rebooted | link re-established, state re-synced, no stuck UI |
| Telnet socket stolen by another client | falls back to HTTP, promotes back within 60 s |
| Spotify token revoked | `REAUTH_REQUIRED` reported (`spotify status` until Phase 6 has a screen), re-pair via `spotify pair`/`spotify code`, no crash, no rollback |
| Spotify returns 429 sustained | backoff honoured, UI stays responsive |
| SPIFFS corrupted | erased and rebuilt, artwork re-fetched |
| Power cut mid-NVS-write | settings intact or defaulted, never garbage |
| Power cut mid-OTA | current slot still boots |
| Brownout during panel init | clean reboot, no half-initialised display |
| Wrong-key / lower-`security_version` image offered | rejected, current slot untouched |

- Coredump retention and manual export path verified end to end
- Enclosure, mounting, thermal check in the real wall location

**Exit criteria:** 7 days unattended with a flat heap slope, ≥ 25 % stack headroom on every
task, and clean recovery from every fault in the matrix.

---

## Phase 7b / Phase 8 — parked

This project is a personal dev/test device for the time being, not a product in progress —
the production profile dry run and production-readiness work that used to sit here are
parked, in full, in [PRODUCTION_DEFERRED.md](PRODUCTION_DEFERRED.md). Nothing was decided
against; it's out of the active roadmap until that changes.

---

## Future expansion

Roughly in order of value per unit of effort. The architecture is built so none of these
require restructuring.

**Near — mostly new screens over existing services**
- Zone 2 / Zone 3 control (the X3800H has them; `denon_state_t` is already per-zone shaped)
- Spotify queue view, and playlist/album browse with an on-screen keyboard for search
- Recently-played and favourites as one-tap shortcuts
- Per-input display profiles (different accent/layout for TV vs music)

**Medium — a new service, unchanged everywhere else**
- HEOS protocol on port 1255 for native transport and multi-room grouping
- MQTT / Home Assistant bridge: publish state, accept intents. The intent bus already exists,
  so this is a transport adapter, not a feature.
- Non-Spotify sources via AirPlay or the AVR's own now-playing metadata, so the screen stays
  useful when Spotify isn't the source
- Lyrics (time-synced) on the Now Playing screen
- Spotify seek/scrub — no SEEK intent kind or endpoint exists today (§1.3); `w_progress_bar`
  is display-only pending this
- Track favoriting — no save/favorite endpoint exists today; the heart control was cut from
  `w_transport` entirely rather than shipped as a dead tap target (Phase 5)
- A fast pre-decode artwork palette/accent path — today's accent color and pale-cover scrim
  adjustment both wait on the one full JPEG decode (avg 660 ms / max ~1.8 s, RISKS.md R1);
  a cheap low-res pre-pass could shrink the placeholder-to-artwork window
- `svc_spotify` polish: `reauth_recommended` is set one refresh cycle later than it needs to
  be; `spotify set-avr-device` accepts an arbitrary device-id string without checking it
  against a non-blank name, so a malformed manual pin could persist a blank-name entry

**Longer — new hardware or new capability**
- mmWave presence sensing for wake-on-approach. Meaningfully better than wake-on-touch —
  the screen is already lit when you arrive. The `svc_power` wake-source abstraction is
  designed with this in mind.
- Knock-to-wake via an IMU — **needs new hardware**, not "cheap, none needed" as this line
  previously said: the real board (confirmed directly against the vendor's own hardware
  reference, Phase 5 Tier 7) has no IMU fitted at all (`BSP_CAPS_IMU=0`); the QMI8658 this
  line used to name was never actually on this board revision
- Multiple panels in different rooms sharing config, with one acting as provisioner
- Ambient / photo screensaver mode with a clock
- Matter or HomeKit exposure as a media controller
- A physical rotary encoder for volume — the one interaction a touchscreen genuinely does
  worse than hardware
- Root-cause the real LVGL compositing-layer hang found in Phase 5 Tier 7 (`w_art_card`'s
  rounded corners currently disabled because of it — [UI_ARCHITECTURE.md](UI_ARCHITECTURE.md)
  §4), or move corner-rounding into the artwork pixel buffer itself instead of relying on
  LVGL's own `clip_corner`

---

## Sequencing notes

- **Denon before Spotify** (Phase 3 before 4) is deliberate. It validates the architecture
  against a real integration without spending a week on OAuth first.
- **A debug UI in Phase 3, the real UI in Phase 5.** Building the polished UI against
  services that don't exist yet means designing against imagined data. Building it after
  means the view model is shaped by what the services actually produce.
- **OTA rollback is proven in Phase 2**, before anything depends on it. The recovery path
  must be trustworthy before the device goes on a wall.
- **The host test harness and SDL UI harness are built early.** Both feel like detours in
  week two and both pay for themselves before week six.
