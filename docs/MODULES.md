# Directory Structure and Module Responsibilities

Deliverables §2-3. Split out of [ARCHITECTURE.md](ARCHITECTURE.md), which keeps the
principles, layering, enforcement, and task model.

This is the "where does this code go, and what is it allowed to do" reference. The layering
rules it implements are in [ARCHITECTURE.md](ARCHITECTURE.md) §1.

---

## 1. Directory structure

```
denon-spotify-controller/
├── CMakeLists.txt
├── partitions.csv
├── sdkconfig.defaults                  # shared, committed
├── sdkconfig.defaults.esp32s3          # SoC-specific, committed
├── dependencies.lock                   # pinned component versions, committed
├── tools/
│   ├── arch_rules.json                 # allowed inter-component edges
│   ├── run_all_checks.py               # runs all 12 gates; also an ALL target in CMakeLists
│   ├── check_*.py                      # the gates themselves (includes, limits, pins, ...)
│   ├── test_gates.py                   # proves each gate fails when violated
│   ├── denon_mv_oracle.py              # independent MV impl -- see RISKS.md R13
│   ├── ota_manifest_oracle.py          # same idea, for OTA manifests
│   ├── sign_release.py  gen_manifest.py  gen_trust_anchor.py  gen_fw_version_header.py
│   └── gen_assets.py                   # fonts/icons -> LVGL C arrays (needs Node/lv_font_conv)
├── docs/
│   ├── 15 documents -- ARCHITECTURE.md is the map; DEVELOPMENT.md is where to start
│   └── adr/                            # 11 decision records
├── main/
│   ├── CMakeLists.txt
│   ├── main.c                          # 12 lines: app_start() and nothing else
│   ├── app_wiring_*.c                  # the composition root, split 7 ways
│   ├── volume_coordinator.c            # adr/0007 retargeting policy (host-tested)
│   └── pair_http.c                     # the Spotify pairing page (adr/0011)
├── components/
│   │
│   ├── bsp_board/                      # THE board file. Pins, IO expander, rails.
│   │   ├── include/bsp_board.h
│   │   ├── bsp_board.c
│   │   ├── bsp_pins.h                  # every GPIO number in the project, once
│   │   └── bsp_io_expander.c           # CH32V003 custom-firmware expander behind a tiny API
│   │                                     # (NOT a CH422G/TCA9554 -- an early guess, wrong;
│   │                                     # see bsp_io_expander.h's own top comment)
│   │
│   ├── hal_display/                    # esp_lcd RGB panel + real ST7701 init sequence;
│   │                                     # 1 framebuffer + LV_DISPLAY_RENDER_MODE_PARTIAL,
│   │                                     # not 2 + full-frame -- see RISKS.md R1
│   ├── hal_touch/                      # GT911 -> normalised touch events, dual-address
│   │                                     # probe (no INT pin exists on this board)
│   ├── hal_backlight/                  # binary on/off via the IO expander's real PWM
│   │                                     # register (REG_PWM) -- not LEDC, this board has
│   │                                     # no dedicated backlight GPIO at all
│   │
│   ├── core_eventbus/                  # topic pub/sub over FreeRTOS queues
│   ├── core_state/                     # snapshot store, per-domain versioning
│   ├── core_fsm/                       # table-driven FSM engine, host-testable
│   ├── core_pool/                      # fixed-block allocators, slot pools, ring buffers
│   ├── core_types/                     # shared POD types: track_t, denon_state_t, ...
│   │
│   ├── svc_storage/                    # NVS wrapper, encrypted namespace, migrations
│   ├── svc_settings/                   # typed settings + change events + write coalescing
│   ├── svc_logging/                    # esp_log hook -> ring buffer -> optional UDP
│   ├── svc_diagnostics/                # counters, watermarks, health snapshot
│   ├── svc_scheduler/                  # deferred/periodic work on a shared worker
│   ├── svc_supervisor/                 # boot graph, readiness, restart policy (LIFECYCLE.md)
│   │
│   ├── net_wifi/                       # connection FSM, provisioning SoftAP, backoff
│   ├── net_http/                       # request/response, retry policy, TLS arbiter
│   ├── net_time/                       # SNTP + monotonic clock for progress interpolation
│   │
│   ├── svc_ota/                        # manifest check, download, verify, rollback health
│   │
│   ├── svc_spotify/
│   │   ├── include/                    # 16 headers; spotify_service.h is the service API,
│   │   ├── spotify_service.c           # task, lifecycle, policy
│   │   ├── spotify_auth.c              # PKCE, refresh, token rotation
│   │   ├── spotify_api.c               # endpoint calls, rate limiting
│   │   ├── spotify_parse.c             # JSON -> core_types  (pure, host-tested)
│   │   └── spotify_poll_policy.c       # adaptive interval  (pure, host-tested)
│   │
│   ├── svc_denon/
│   │   ├── include/denon_service.h
│   │   ├── denon_service.c             # task, state, command queue + coalescing
│   │   ├── denon_transport.h           # the injected interface
│   │   ├── denon_telnet.c              # port 23, push events, exclusive-link handling
│   │   ├── denon_http.c                # port 8080 fallback
│   │   ├── denon_codec.c               # "MV525" <-> 52.5 dB  (pure, host-tested)
│   │   └── denon_discovery.c           # SSDP only (mDNS trimmed -- see NETWORKING.md §1.4)
│   │
│   ├── svc_artwork/
│   │   ├── artwork_service.c           # fetch task, slot lifecycle
│   │   ├── artwork_cache.c             # generation-tagged, single-owner slot pool (not refcounted)
│   │   ├── artwork_decode.c            # JPEG -> RGB565 at 1/2 scale (vendored TJpgDec)
│   │   ├── artwork_service_fetch.c     # the fetch/decode/publish pass (split for the 400-line gate)
│   │   └── artwork_palette.c           # dominant colour extraction  (pure, host-tested)
│   │
│   │
│   └── ui/                             # 5 screens + 1 overlay, 16 widgets, view models,
│                                       #   ui_router (incl. the overlay slot), ui_app.
│                                       #   Layering: UI_ARCHITECTURE.md; layouts: UI_SCREENS.md
│
│   NOT PRESENT: svc_power/ and svc_input/ are Phase 6 work that was never started.
│   They have responsibility rows below because the design exists; the directories do not.
│
└── test/
    ├── host/                           # Unity, runs on the dev PC in CI. Fast.
    │   ├── test_*.c                     # 53 suites: codecs, FSMs, view models, policy, parsers
    │   └── fixtures/                   # captured real API/telnet payloads
    ├── ui_harness/                     # the real ui/ tree against LVGL+SDL2 on the desktop
    └── target/                         # on-device: flash, PSRAM bandwidth, panel timing
```

**Why `bsp_pins.h` exists:** every GPIO number in the project appears exactly once, in one
file. Waveshare revises these boards. When the pinout changes, one file changes.

---

## 2. Module responsibilities

Each module gets one paragraph. If it needs two, it's doing too much.

### Core

| Module | Responsibility | Explicitly NOT responsible for |
|---|---|---|
| `core_eventbus` | Deliver fixed-size POD events from publishers to subscriber queues. Topic filtering, overflow accounting. | Payload lifetime, ordering across topics, delivery guarantees |
| `core_state` | Hold the authoritative snapshot of each domain. Serve coherent copies to readers. Version + timestamp each domain. | Deciding *what* the state should be |
| `core_fsm` | Run table-driven state machines: `(state, event) -> (action, next)`. Log every transition. | Knowing about any specific domain |
| `core_pool` | Fixed-block allocation, slot pools with generation handles, SPSC ring buffers. | Anything domain-specific |
| `core_types` | POD definitions shared across the boundary: `track_t`, `playback_t`, `denon_state_t`, `intent_t`. Header-only. **Treated as public API** — every addition is reviewed, because this is the classic shared-domain dumping ground and its erosion is how layering quietly dies. If only two modules need a type, it belongs to one of them, not here. | Behaviour; anything used by fewer than three modules |

### Services

| Module | Responsibility |
|---|---|
| `svc_supervisor` | **Owns system lifecycle.** Holds the boot dependency graph, declares when each subsystem is ready, applies the restart policy for a failed service, drives degraded-mode and fatal-state decisions, and owns the OTA health gate verdict. See §3.1. |
| `svc_settings` | Typed get/set over a versioned schema. Debounce writes (2 s) to protect flash. Emit `EVT_SETTING_CHANGED`. Run migrations on schema bump. |
| `svc_storage` | NVS namespaces, encrypted namespace for secrets, blob helpers. The only module that touches NVS. |
| `svc_logging` | Install an `esp_log` vprintf hook. Keep the last ~8 KB in a RAM ring. Optionally mirror to UDP syslog. Survive into the coredump. |
| `svc_diagnostics` | Aggregate health: uptime, per-pool heap watermark, per-task stack high-water, RSSI, API error counters, reconnect counts, last N faults. Owns **coredump retention and export** (§3.2). Feeds the hidden diagnostics screen and `svc_supervisor`. |
| `svc_scheduler` | One shared low-priority worker task for deferred and periodic jobs, so we don't spawn a task per chore. |
| `net_wifi` | Connection FSM, credential storage via `svc_storage`, SoftAP provisioning portal, exponential backoff with jitter, RSSI reporting. |
| `net_http` | HTTP(S) request execution with an injected retry/timeout policy. Owns the **TLS arbiter** ([MEMORY.md](MEMORY.md) §4) that caps concurrent handshakes. |
| `net_time` | SNTP sync (required before any TLS validation) plus a monotonic microsecond clock used for progress interpolation. |
| `svc_ota` | Poll a manifest over TLS, download to the inactive slot, verify SHA-256, reboot, and confirm-or-rollback based on a health gate. |
| `svc_spotify` | Own the Spotify session: OAuth PKCE, token refresh + rotation, adaptive polling of `/v1/me/player`, intent execution, optimistic-state reconciliation. |
| `svc_denon` | Own the AVR link: transport selection (telnet/HTTP), command queue with coalescing, parse push events, maintain `denon_state_t`. |
| `svc_artwork` | On track change, fetch the 640 px image, decode at ½ scale to RGB565, derive a 60 px thumb and a dominant colour, publish a generation-tagged, single-owner slot handle (not refcounted). |
| `svc_power` **(not built)** | Activity/idle timing, dim → sleep → wake transitions, backlight ramps, wake-on-playback, night schedule. |
| `svc_input` **(not built)** | Turn raw GT911 reports into gestures (tap, drag, swipe, long-press) and then into `intent_t`. Debounce and reject palm/ghost touches. |

See **[LIFECYCLE.md](LIFECYCLE.md)** for the boot dependency graph, readiness model,
restart policy, degraded-mode rules, and coredump retention and export.

### HAL / BSP

**Board:** Waveshare ESP32-S3-Touch-LCD-4 (N16R8) —
<https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4>. That wiki is the source for the
schematic, the panel's own ST7701 init sequence, and the CH32V003 expander's register map,
and is worth opening before touching anything in this section.

Treat it as a starting point, not as truth: Phase 0 lost most of its bring-up time to
details that did not match this board revision — the IO expander turned out to be a
different chip speaking a different protocol than assumed, several pin assignments were
wrong, and the generic ST7701 init sequence produced no picture where the panel's own
sequence worked. [RISKS.md](RISKS.md) R1's Phase 5 Tier 7 update lists all nine confirmed
root causes. `bsp_board` is the only place any of that knowledge is allowed to live.

| Module | Responsibility |
|---|---|
| `bsp_board` | Bring up I²C, the CH32V003 IO expander, power rails, and reset lines in the correct order (one combined LCD+touch reset/power-up sequence, not independent per-driver pulses). Publish a board descriptor. The only file that knows this is a Waveshare board. |
| `hal_display` | Real ST7701 init command sequence (not the driver's generic default), `esp_lcd_rgb_panel` config (1 framebuffer, `LV_DISPLAY_RENDER_MODE_PARTIAL` — see [RISKS.md](RISKS.md) R1), bounce-buffer allocation, VSYNC callback. |
| `hal_touch` | GT911 over I²C, dual-address probe (0x5D then 0x14) — the real board wires no INT pin at all, so there's no interrupt-driven read and no address-strapping. |
| `hal_backlight` | Binary on/off via the IO expander's own PWM register (real dimming is possible on this hardware — see [UI_ARCHITECTURE.md](UI_ARCHITECTURE.md) §7 — just not exposed as a feature yet). |

No IMU on this board (`hal_imu` doesn't exist as a component) — confirmed against the real
vendor hardware reference (`BSP_CAPS_IMU=0`), not left unbuilt by choice.

---
