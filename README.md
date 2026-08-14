# denon-spotify-controller

Wall-mounted Spotify + Denon AVR-X3800H controller.

**Hardware:** Waveshare **ESP32-S3-Touch-LCD-4, Rev4.0** (ESP32-S3-WROOM-1 N16R8) — 480×480
ST7701 on the parallel RGB bus, GT911 capacitive touch, CH32V003 IO expander.
**The revision matters.** Pin assignments and the IO-expander chip differ between revisions;
this code targets Rev4.0 and `bsp_pins.h` is the single place that encodes it.
Vendor wiki: <https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4>
(schematics, the panel's own ST7701 init sequence, and the IO-expander protocol all live
there — [docs/MODULES.md](docs/MODULES.md) records which parts of it turned out to be wrong
for this board revision, which cost most of Phase 0's bring-up time.)

**Status: Phases 0–5 complete, Phase 6 screens complete. Parked, working, on real
hardware.** Five screens, real Spotify OAuth pairing from the panel itself, real playback
and Denon control, real OTA with signed images and anti-rollback. **A personal dev/test
device, not a product** — see [docs/PRODUCTION_DEFERRED.md](docs/PRODUCTION_DEFERRED.md).

---

## Picking this up again

Start here if you are returning cold, or are a model reading this for the first time.

1. **Read [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) §1 first** — the layering rules are
   mechanically enforced (`tools/check_includes.py`), so violating them fails the build
   rather than merely being frowned upon. Then [docs/ROADMAP.md](docs/ROADMAP.md) for what
   each phase actually shipped versus what it originally planned; the two differ often, and
   the differences are documented deliberately.
2. **`python tools/run_all_checks.py --root .`** should print **12/12**. If it doesn't, fix
   that before anything else.
3. **The comments are the real design record.** Most non-obvious decisions in this codebase
   are explained at their call site with the evidence behind them — usually a real hardware
   symptom, often the source file in ESP-IDF or LVGL that was read to settle it. If a
   comment seems disproportionately long, it is because something took a long time to find.
   Prefer correcting them over deleting them.
4. **Nothing here is speculative infrastructure.** Every abstraction has a real caller. The
   overlay slot in `ui_router` was built with its first modal, not before it.

### Traps that cost real time

- **`sdkconfig` is gitignored and derived from `sdkconfig.defaults`.** Editing a default does
  *nothing* to an existing build — delete `sdkconfig` (or use `menuconfig`) to pick it up.
- **The SDL harness holds its own `.exe` open.** Relinking while it runs fails with
  `Permission denied`; close the window first.
- **Occasional build failures with no compiler diagnostic** (a random ESP-IDF file "FAILED"
  with no `error:` line) are the local toolchain being interrupted, not your code. Re-run
  `idf.py build`; it resumes and completes.
- **`uxTaskGetStackHighWaterMark()` returns BYTES on ESP-IDF**, not words as the vanilla
  FreeRTOS header documents. The diagnostics log says `bytes` for that reason.

### Where it was left

Working and verified on hardware. The last things touched, with what remains genuinely
unverified, are in [docs/ROADMAP.md](docs/ROADMAP.md)'s Phase 6 section — the short version:
Spotify pairing from the panel works end to end, but its three *rejection* paths (stale
link, clock-not-yet-synced, wrong session state) have never been exercised on hardware, and
one album cover that never decodes remains unexplained — see
[components/svc_artwork/artwork_service_fetch.c](components/svc_artwork/artwork_service_fetch.c)
for the two candidate causes and the one log line that would tell them apart.

---

## What is actually here

| | |
|---|---|
| **Architecture docs** | [docs/](docs/) — 16 documents, 11 ADRs |
| **Enforcement** | 12 gates via [tools/run_all_checks.py](tools/run_all_checks.py), dependency-free Python, also wired into `idf.py build` |
| **Gate self-test** | [tools/test_gates.py](tools/test_gates.py) — proves the gates fail when violated |
| **Host tests** | [test/host/](test/host/) — 53 Unity test files: codecs, FSMs, view models, policy, parsers |
| **SDL UI harness** | [test/ui_harness/](test/ui_harness/) — the real `ui_router`/screens/widgets against LVGL+SDL2, no hardware needed |
| **Firmware** | [components/](components/) — bring-up, connectivity, Denon, Spotify, artwork, and the full UI, all hardware-verified |

## Quick start

```bash
# Architecture gates — needs only Python 3, no pip install
python tools/run_all_checks.py --root .

# Prove the gates actually enforce (mutates a scratch clone, not your tree)
python tools/test_gates.py

# Host tests — needs a C compiler and CMake, no ESP-IDF
# (Linux/macOS: the -D flag is unnecessary; see docs/DEVELOPMENT.md for why
#  Windows needs it — CMake defaults to MSVC's cl.exe, usually not installed.)
cmake -S test/host -B build_host -G Ninja -DCMAKE_C_COMPILER=clang
cmake --build build_host --parallel
ctest --test-dir build_host --output-on-failure

# SDL UI harness — real screens on the desktop, no hardware.
# Fixtures: normal, stale, reconnecting, unavailable, adversarial-utf8,
# pale-cover, long-title-marquee, no-active-playback, pair, pair-nonet, pair-connected
cmake -S test/ui_harness -B build_ui_harness
cmake --build build_ui_harness
./build_ui_harness/ui_harness pair

# Firmware — needs ESP-IDF v5.5+ and hardware
idf.py set-target esp32s3
idf.py menuconfig     # Spotify client ID, debug consoles, pairing ports
                      # (NOT WiFi credentials -- those come from the provisioning portal)
idf.py build flash monitor
```

Full setup (what to install, exact winget package IDs on Windows, why the plain CMake
invocation fails on a fresh machine) is in [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

## The gates

Every architectural claim that can be mechanically checked, is. The rest is just prose that
decays.

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
| `ota_vectors` | manifest/rollback vectors, same two-language oracle idea | SECURITY |
| `fw_version_header` | the generated version header matches `fw_version.json` | PLATFORM |
| `test_gates` | that all of the above actually fail when violated | — |

[CI](.github/workflows/ci.yml) runs four independent jobs on every push and PR: the gates
(Python only), host tests, **host tests again under ASan + UBSan**, and a full ESP-IDF
firmware build — which generates an *ephemeral* dev signing key rather than using a
committed one, and re-runs the LVGL symbol boundary check against the real linked binary.

## Why the Denon MV codec is the first thing tested

`MV52` is 52.0 dB. `MV525` is 52.5 dB, not 525. `MVMAX 98` is a *maximum report*, not a
level — a naive `atoi(s+2)` on it yields 0. Getting any of these wrong sends a wrong volume
to an amplifier driving real speakers ([RISKS.md](docs/RISKS.md) R13).

So it is written twice: once in C, once in Python
([tools/denon_mv_oracle.py](tools/denon_mv_oracle.py)), from the protocol rather than from
each other. Both are checked against one shared vector table. A wrong expected value has to
be wrong identically in two languages to survive.

## What real hardware bring-up actually took

[RISKS.md R1](docs/RISKS.md) named the risk correctly from day one — the ST7701 is on the
parallel RGB bus, so panel DMA reads the framebuffer from PSRAM continuously in competition
with WiFi DMA, TLS, and JPEG decode — but the dominant real symptom, once actual hardware and
the full application existed, was something the Phase 0 spike had no way to exercise: a
periodic full-system diagnostics scan that briefly froze the entire scheduler.

Getting from "nothing on screen" to a stable picture took nine separate, independently
confirmed root causes on real hardware — wrong IO-expander chip and protocol, wrong pin
assignments, a generic rather than the panel's own ST7701 init sequence, memory budgets that
didn't fit real measured RAM, an LVGL PSRAM pool that was designed but never actually wired
up, and that scheduler freeze, among others. The full account — every measurement, every
fallback tried and reverted, and all nine root causes — is in
[docs/DISPLAY_PIPELINE.md](docs/DISPLAY_PIPELINE.md), split out of the risk register because
it is the most expensive knowledge in this repo.

## Layout

```
docs/          architecture, ADRs, risk register, roadmap, production-deferred notes
tools/         CI gates (Python 3 stdlib only), asset/font generation, signing, MV oracle
main/          composition root — app_wiring_*.c wires every component together
               pair_http.c: the Spotify pairing page, served only while its screen is open
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

## The screens

Fixed order, never wraps. Now Playing is the boot screen.

```
Receiver  ←  Now Playing  →  Connection  →  System
                                              └── Pair Spotify (overlay, from the Spotify row)
```

Layouts and the real mechanisms behind them are in
[docs/UI_SCREENS.md](docs/UI_SCREENS.md); the layering, widget catalogue, and theme tokens
are in [docs/UI_ARCHITECTURE.md](docs/UI_ARCHITECTURE.md).

## Not here yet, on purpose

`svc_power` (dim/sleep/wake), `svc_input` gesture recognition, the motion pass, and the
remaining widget catalogue (`w_picker`, `w_icon_toggle`, `w_volume_dial`) are the unbuilt
part of Phase 6 — see [docs/ROADMAP.md](docs/ROADMAP.md).

Track favoriting was **cut**, not deferred: no Spotify favorite/save endpoint is wired, and
the control was removed from the design rather than left as a stub.

Manufacturing, factory test, Secure Boot / flash encryption, and fleet operations are
deliberately parked, not missing — see
[docs/PRODUCTION_DEFERRED.md](docs/PRODUCTION_DEFERRED.md). **One item there is a real
prerequisite before this device goes anywhere but a personal wall:** the pairing page's
accepted LAN exposure, recorded in [docs/adr/0011](docs/adr/0011-on-device-pair-page-over-console-paste.md).

## Licence

[Apache-2.0](LICENSE). Chosen over MIT for the explicit patent grant, which is
the clause that matters for firmware anyone might put near a product.

The fonts are not covered by it. `tools/font_sources/` holds Archivo, IBM Plex
Mono and two Noto faces under the SIL Open Font License, each with its licence
text alongside it, and the generated `components/ui/assets/fonts/ui_font_*.c`
are derived from them. The OFL travels with the files, as it requires.
