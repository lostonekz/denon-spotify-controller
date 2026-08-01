# denon-spotify-controller

Wall-mounted Spotify + Denon AVR-X3800H controller.
ESP32-S3-Touch-LCD-4 (N16R8, 480×480 ST7701 on the parallel RGB bus, GT911 touch).

**Status: Phases 0–5 shipped, real hardware.** This is a working device today — real
Spotify OAuth pairing, real playback control, real Denon AVR control over telnet/HTTP, a
full LVGL UI (Art-forward Now Playing screen: crossfading artwork, adaptive scrim, marquee
title, optimistic transport controls), real OTA with signed images and anti-rollback, all
running continuously on the actual panel. **Currently a personal dev/test device, not a
product in progress** — see [docs/PRODUCTION_DEFERRED.md](docs/PRODUCTION_DEFERRED.md) for
what that means and what's parked.

---

## What is actually here

| | |
|---|---|
| **Architecture docs** | [docs/](docs/) — 13 documents, 10 ADRs |
| **Enforcement** | 7 architecture gates in [tools/](tools/), all dependency-free Python, plus target-build-time gates (signing, MV/OTA vector oracles, pin/partition checks) wired into `idf.py build` |
| **Gate self-test** | [tools/test_gates.py](tools/test_gates.py) — proves the gates fail when violated |
| **Host tests** | [test/host/](test/host/) — 50 Unity test files: codecs, FSMs, view models, policy, parsers |
| **SDL UI harness** | [test/ui_harness/](test/ui_harness/) — the real `ui_router`/screens/widgets running against LVGL+SDL2 on the dev machine, no hardware needed |
| **Firmware** | [components/](components/) — bring-up, connectivity, Denon, Spotify, artwork, and the full UI, all real and hardware-verified |

## Quick start

```bash
# Architecture gates — needs only Python 3, no pip install
python tools/run_all_checks.py --root .

# Prove the gates actually enforce (mutates a scratch clone, not your tree)
python tools/test_gates.py

# Host tests — needs a C compiler and CMake, no ESP-IDF
# (Linux/macOS: the two -D flags below are unnecessary; see docs/DEVELOPMENT.md
#  for why Windows needs them explicitly — the short version is that CMake
#  defaults to looking for MSVC's cl.exe, which usually isn't installed.)
cmake -S test/host -B build_host -G Ninja -DCMAKE_C_COMPILER=clang
cmake --build build_host --parallel
ctest --test-dir build_host --output-on-failure

# Firmware — needs ESP-IDF v5.5+ and hardware
idf.py set-target esp32s3
idf.py menuconfig     # WiFi credentials, Spotify client ID, feature flags
idf.py build flash monitor
```

Full setup instructions (what to install, exact winget package IDs on
Windows, why the plain CMake invocation fails on a fresh machine) are in
[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

## The gates

Every architectural claim that can be mechanically checked, is. The rest is
just prose that decays.

| Gate | Enforces | Doc |
|---|---|---|
| `check_includes` | layer boundaries; the ui→hal glue exception at header granularity | ARCHITECTURE §1.2, §1.4 |
| `check_limits` | 400-line ceiling on source **and docs** | project rules |
| `check_lvgl_boundary` | no `lv_*` symbol outside `components/ui/` | ADR-0008 |
| `check_release` | dev key can never ship; signing can never be off in dev | SECURITY §1 |
| `check_partitions` | overlaps, alignment, A/B symmetry, `nvs_keys` reserved | PLATFORM §1 |
| `check_sdkconfig` | the R1 mitigations and OTA/eFuse policy, as assertions | MEMORY §3, SECURITY |
| `check_pins` | GPIO double-assignment, reserved and strapping pins | MODULES |
| `denon_mv_oracle` | the MV boundary table, via an independent implementation | RISKS R13 |
| `test_gates` | that all of the above actually fail when violated | — |

## Why the Denon MV codec is the first thing tested

`MV52` is 52.0 dB. `MV525` is 52.5 dB, not 525. `MVMAX 98` is a *maximum
report*, not a level — a naive `atoi(s+2)` on it yields 0. Getting any of these
wrong sends a wrong volume to an amplifier driving real speakers
([RISKS.md](docs/RISKS.md) R13).

So it is written twice: once in C, once in Python
([tools/denon_mv_oracle.py](tools/denon_mv_oracle.py)), from the protocol rather
than from each other. Both are checked against one shared vector table. A wrong
expected value has to be wrong identically in two languages to survive.

## What real hardware bring-up actually took

[RISKS.md R1](docs/RISKS.md) named the risk correctly from day one — the ST7701 is on the
parallel RGB bus, so panel DMA reads the framebuffer from PSRAM continuously in competition
with WiFi DMA, TLS, and JPEG decode — but the dominant real symptom once actual hardware and
the full application existed turned out to be something the original Phase 0 spike had no
way to exercise: a periodic full-system diagnostics scan that briefly froze the entire
scheduler. Getting from "nothing on screen" to a stable picture took nine separate,
independently-confirmed root causes on real hardware — wrong IO-expander chip and protocol,
wrong pin assignments, a generic vs. the panel's own real ST7701 init sequence, memory
budgets that didn't fit real measured RAM, an LVGL PSRAM pool that was designed but never
actually wired up, and that scheduler freeze, among others. Full account in RISKS.md R1's
own Phase 5 Tier 7 update.

## Layout

```
docs/          architecture, ADRs, risk register, roadmap, production-deferred notes
tools/         CI gates (Python 3 stdlib only), asset/font generation, signing, MV oracle
main/          composition root — app_wiring_*.c wires every component together
components/
  bsp_board/     every GPIO in the project, exactly once; CH32V003 IO expander bring-up
  hal_display/   hal_touch/  hal_backlight/    real ST7701/GT911/CH32V003-PWM drivers
  ui/            ui_app, ui_router, view_model, screens, widgets, theme (LVGL, layer "ui" only)
  svc_denon/     telnet/HTTP dual transport, MV codec, command coalescing
  svc_spotify/   OAuth PKCE, adaptive polling, playback intents
  svc_artwork/   fetch, JPEG decode, dominant-color palette
  svc_ota/       manifest check, download, verify, health-gated rollback
  net_wifi/ net_http/ net_time/     connectivity, TLS arbiter, SNTP
  svc_storage/ svc_settings/ svc_logging/ svc_diagnostics/ svc_scheduler/ svc_supervisor/
  core_*/        event bus, state store, FSM engine, pool allocators, shared types
test/host/     Unity tests — run on the dev machine, no hardware
test/ui_harness/  the real ui/ component tree running against LVGL+SDL2 on the dev machine
```

## Not here yet, on purpose

Denon screen, Settings, modals/pickers, `svc_power` (dim/sleep/wake), gesture recognition,
and the remaining widget catalogue are Phase 6 — see
[docs/ROADMAP.md](docs/ROADMAP.md). Track favoriting and progress-bar scrub aren't built
because no Spotify endpoint exists for either. Manufacturing, factory test, Secure Boot/flash
encryption, and fleet operations are deliberately parked, not missing — see
[docs/PRODUCTION_DEFERRED.md](docs/PRODUCTION_DEFERRED.md).
