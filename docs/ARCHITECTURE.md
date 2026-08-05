# Architecture

Wall-mounted Spotify + Denon AVR-X3800H controller.
Target: Waveshare ESP32-S3-Touch-LCD-4 (ESP32-S3 N16R8, 480x480 ST7701 RGB panel, GT911 touch).

| Doc | Deliverable | Covers |
|---|---|---|
| This file | 1, 4 | Principles, layering, DI, CI enforcement, task model |
| [MODULES.md](MODULES.md) | 2–3 | Directory tree, module responsibilities and non-responsibilities |
| [EVENTS_AND_STATE.md](EVENTS_AND_STATE.md) | 5–6 | Event bus, intents, backpressure, state store, FSMs |
| [MEMORY.md](MEMORY.md) | 7 | Memory budget, pools, PSRAM bandwidth, TLS arbiter |
| [PLATFORM.md](PLATFORM.md) | 8–9 | Flash layout, OTA, configuration |
| [NETWORKING.md](NETWORKING.md) | 10 | Connectivity FSM, provisioning, Spotify, Denon |
| [UI_ARCHITECTURE.md](UI_ARCHITECTURE.md) | 11 | UI layering, router, widget catalogue |
| [UI_SCREENS.md](UI_SCREENS.md) | 11 | Per-screen layout and behaviour (split out of the above) |
| [UI_THEME.md](UI_THEME.md) | 11 | Colour/type/metric/motion tokens (split out of the above) |
| [RISKS.md](RISKS.md) | 12 | Risk register |
| [ROADMAP.md](ROADMAP.md) | 13–14 | Delivery phases, future expansion |
| [SECURITY.md](SECURITY.md) | — | Threat model, build profiles, OTA trust chain, manufacturing |
| [LIFECYCLE.md](LIFECYCLE.md) | — | Boot graph, readiness, restart policy, coredumps |
| [DEVELOPMENT.md](DEVELOPMENT.md) | — | **Start here to build anything.** Toolchain setup, host tests, harness |
| [PRODUCTION_DEFERRED.md](PRODUCTION_DEFERRED.md) | — | What is parked rather than missing, and its prerequisites |
| [adr/](adr/) | — | Load-bearing decisions with rationale |

---

## 0. Project principles

### 0.1 No irreversible decisions on development units

Nothing may permanently foreclose an option **on a unit that is still being developed**.
Concretely:

- **No eFuse operations on development units. Ever.** No Secure Boot, no flash encryption,
  no JTAG disable. These are one-way and their failure mode is a scrap board. A dev board
  must stay USB-reflashable for its whole life.
- **Production units burn eFuses exactly once, at manufacturing, as the final step after
  factory test passes** — see [SECURITY.md](SECURITY.md) §7 and
  [adr/0006](adr/0006-staged-security.md). That is a deliberate, staffed, recoverable-by-
  scrapping-one-unit decision made against a manufacturing threat model. It is not the same
  act as burning a fuse on the bench, and this principle does not forbid it.
- **Reserve options rather than committing to them.** The `nvs_keys` partition exists, sized
  and offset correctly, unused in the development profile. Switching profiles is a config
  change, not a reflash. One sector buys a permanent option.
- **The partition table is the exception that proves the rule** — genuinely hard to change on
  a deployed unit, so it is sized with deliberate excess (6 MiB app slots) rather than
  tightly fitted to today's needs.
- Dependencies are pinned to exact versions, so an upgrade is always a deliberate,
  revertible act.

**Precedence.** Where §0.1 and the production profile appear to conflict, §0.1 governs
*development units* and [SECURITY.md](SECURITY.md) governs *production units*. There is no
overlap: no build can burn a fuse outside the manufacturing station, and the manufacturing
station never runs against a development unit. An earlier draft of this section said "no
eFuse operations, ever, in any build profile", which directly contradicted the production
profile — the contradiction was real and this is the resolution.

This is a *development* posture, not a security posture: it keeps the cost of being wrong
low, which is what lets us move quickly on everything else.

### 0.2 Security is built now and enabled in stages

This is a personal dev/test device — that's the only thing it is right now, not a product in
progress (production-track work is parked, not deleted; see
[PRODUCTION_DEFERRED.md](PRODUCTION_DEFERRED.md)). Even so, retrofitting security later if
that ever changes is the expensive case study everyone knows, and burning eFuses on a
hand-assembled board violates §0.1 regardless.

So the mechanisms are **built and always running**; only their trust anchor and enforcement
severity are staged:

> **Every security mechanism runs in every build. Only the trust anchor and the enforcement
> severity change between profiles.**

Signed images, anti-rollback, credential redaction, and setup-session hardening execute and
hard-fail in the default development build, against a dev key committed to this repo. The
production profile swaps in an offline release key and adds encrypted NVS, Secure Boot, and
flash encryption at manufacturing.

The point is that a security path which only runs in production has never been tested. This
structure makes going commercial a key swap rather than a redesign, and keeps §0.1 intact —
nothing is burned, nothing is foreclosed.

Full threat model, trust chain, key rotation, and manufacturing sequence in
[SECURITY.md](SECURITY.md) and [adr/0006](adr/0006-staged-security.md).

---

## 1. High-level architecture

### 1.1 The one rule everything else follows

**Intents flow down. State flows up. The two never touch directly.**

```
        ┌──────────────────────────────────────────────┐
        │                     UI                       │
        │        (LVGL, one task, core 1)              │
        └───────┬──────────────────────────▲───────────┘
                │ intent_t                 │ state snapshot
                ▼                          │
        ┌──────────────────────────────────┴───────────┐
        │                    CORE                      │
        │        event bus  ·  state store  ·  fsm     │
        └───────┬──────────────────────────▲───────────┘
                │ intent_t                 │ state patch
                ▼                          │
        ┌──────────────────────────────────┴───────────┐
        │                  SERVICES                    │
        │  spotify · denon · artwork · ota · power     │
        │        (own tasks, core 0)                   │
        └──────────────────┬───────────────────────────┘
                           ▼
        ┌──────────────────────────────────────────────┐
        │        NET  ·  STORAGE  ·  HAL  ·  BSP       │
        └──────────────────────────────────────────────┘
```

The UI never includes a service header. A service never includes an LVGL header.
Both include only `core/`. This is mechanically checkable — see §1.4.

### 1.2 Layers

Dependencies point **downward only**. No layer may include a header from a layer above it,
and no module may include a sibling's private header.

The `ui` row is narrower than it first appears, and deliberately so. The UI links
`hal_display` and `hal_touch` **only** for the LVGL glue — flush callback, tick, input read.
It may not read backlight level, temperature, IMU, board revision, or any other hardware
fact directly; those arrive through `core_state` like everything else. Otherwise hardware
detail leaks upward into presentation and a board revision ripples into screen code. The
include linter encodes this as a symbol-level exception, not a blanket `ui → hal_*` edge.

| Layer | Owns | May depend on |
|---|---|---|
| `app` | Composition root, DI wiring, lifetime | everything |
| `ui` | Rendering, gestures, presentation | `core`, LVGL, and `hal_display`/`hal_touch` **glue only** |
| `core` | Event bus, state store, FSM engine, pools | nothing but stdlib + FreeRTOS |
| `svc_*` | Domain logic, protocol state, policy | `core`, `net_*`, `storage` |
| `net_*` | Sockets, TLS, HTTP, SNTP, WiFi | `core`, ESP-IDF |
| `hal_*` / `bsp` | Panel, touch, backlight, IMU, pins | ESP-IDF drivers |

### 1.3 Dependency injection in C

No ambient global state: every module is constructed with an explicit config struct carrying
its dependencies, and nothing reaches for a singleton to find its collaborators.

**The shape is a caller-allocated concrete struct, not an opaque handle.** An earlier draft
of this section showed a `spotify_service_h` typedef and `esp_err_t` create/start/destroy
functions; that API was never built. The real one — and `svc_denon`, `svc_artwork`, `ui_app`
all match it — is:

```c
/* svc_spotify/include/spotify_service.h — the real thing */
typedef struct {
    net_http_t      *http;          /* injected, concrete */
    net_time_t      *time;
    core_eventbus_t *bus;
    core_state_t    *state_store;
    time_source_t    clock;         /* injected vtable — swappable for host tests */
    http_policy_t    http_policy;   /* retry/timeout policy, by value */
    kv_store_if_t   *secure_kv;     /* injected vtable */
    spotify_service_state_cfg_t state_cfg;
} svc_spotify_cfg_t;

bool svc_spotify_create(svc_spotify_t *s, const svc_spotify_cfg_t *cfg);
void svc_spotify_suspend(svc_spotify_t *s);   /* used during OTA */
void svc_spotify_resume(svc_spotify_t *s);
```

The caller owns the storage: `main/app_ctx.h` embeds `svc_denon_t`, `svc_spotify_t`,
`ui_app_t` **by value**. That is what makes the whole system's memory footprint a single
statically-sized struct rather than a graph of heap allocations.

Anything with more than one plausible implementation is injected as a **vtable interface**,
so host tests can substitute a fake:

- `denon_transport_if_t` — telnet / HTTP / fake
- `kv_store_if_t` — NVS / in-memory
- `time_source_t` — esp_timer / controllable fake
- `intent_submit_provider_t` — the real intent queue / a recording fake

HTTP is deliberately *not* on that list: it is a concrete `net_http_t *` plus a
`http_policy_t` value, because the seam that mattered turned out to be retry/timeout policy
rather than the client itself.

**Two honest exceptions to "no global state",** both file-scope statics rather than shared
globals, and both because their owner is genuinely a singleton with one caller:
`main/pair_http.c` (the HTTP server handle and its request buffers — `esp_http_server` runs
every handler on one task, so there is no reentrancy to protect against) and
`svc_diagnostics.c`'s cached task handles. Neither is reachable from another module.

Every service is created by `app_wiring.c` in dependency order and stored in a single
`app_ctx_t` on the main task's stack-adjacent static context. That struct is the *only*
place composite ownership lives.

### 1.4 Enforcing the boundaries

Architecture that isn't enforced decays within a year. `python tools/run_all_checks.py
--root .` runs **12 gates**, all cheap and all Python-stdlib-only — and it is wired into
`CMakeLists.txt` as an `ALL` target, so **a gate failure fails `idf.py build`**, not just
CI. (The one exception is `lvgl_boundary`, skipped there because it needs the linked
objects; CI runs it separately, post-link.)

The numbered list below explains the *reasoning* behind the layering gates rather than
enumerating all twelve; the full list is `GATES` in `tools/run_all_checks.py`, and README
tabulates it.

1. **Include linter** — `tools/check_includes.py` walks `components/*/`, parses `#include`
   lines, and fails on any edge violating the layer table. Allowed edges declared in
   `tools/arch_rules.json`.
2. **Component isolation** — each `CMakeLists.txt` declares a minimal
   `REQUIRES`/`PRIV_REQUIRES`. If `ui` doesn't list `svc_spotify`, the include physically
   cannot compile. CMake is the primary enforcement; the linter catches the rest.
3. **LVGL symbol scan** — `nm` every object file outside `components/ui/`; any undefined
   reference matching `^lv_` fails the build. This is what actually enforces the no-LVGL-lock
   decision ([adr/0008](adr/0008-own-the-lvgl-glue.md)); the CMake boundary is necessary but
   a symbol scan is what proves it. Debug builds additionally assert the calling task is `ui`
   on entry to the glue.
4. **Production build gate** — `tools/check_release.py` fails a production build that
   contains the dev trust anchor, has assertions or debug logging enabled, ships an unsigned
   OTA root, or has `CONFIG_ESP_CONSOLE` on a shipping profile. The dev key is committed
   deliberately ([SECURITY.md](SECURITY.md) §4), so *proving it cannot ship* is a required
   gate rather than a nicety.
5. **Restartability pairing** — any service whose descriptor claims `restart != REBOOT` must
   have a create/teardown loop test ([LIFECYCLE.md](LIFECYCLE.md) §1.2). Note this one is
   **not** a gate script, unlike the other four: it is enforced by a host test
   (`test/host/test_supervisor_restart.c`) and by review, so it fails `ctest` rather than
   `run_all_checks.py`. Listed here because it is the same kind of promise.
6. **File length** — `tools/check_limits.py` fails on any `.c`/`.h` over 400 lines.

Gates 3, 4 and 5 exist because the corresponding architectural claims are otherwise
unverifiable assertions — which is the failure mode this whole section is guarding against.

---

## 2. Directory structure and module responsibilities

Split into its own document — see **[MODULES.md](MODULES.md)** for the full component tree
and the responsibility table (including what each module is explicitly *not* responsible
for, which is the more useful half).

Shape: `main/` holds the composition root — plus the few things that legitimately need
both a service and an IDF edge (`volume_coordinator.c`, `pair_http.c`, the debug consoles
and selftests, each gated by its own Kconfig flag where it is a dev tool). Everything else
is an ESP-IDF component under
`components/`, prefixed by layer — `core_*`, `svc_*`, `net_*`, `hal_*`, `bsp_*`, `ui`.
`tools/` holds the CI gates from §1.4. `test/host/` runs on the dev machine and is where the
protocol codecs, poll policy, view model, and FSMs are actually tested.

---

## 4. Task architecture

Core 1 belongs to the UI. Core 0 hosts WiFi and every network service.

Task names below are the **real FreeRTOS names**, because `svc_diagnostics` keys its stack
watermark report on them — grepping a log for a name this table invented finds nothing.

| Task | Core | Prio | Stack | Cadence | Blocking allowed? |
|---|---|---|---|---|---|
| `ui_app` | 1 | 4 | 12 KB | `lv_timer_handler()` loop, ~16 ms | **Never** > 2 ms |
| `net_sup` | 0 | 6 | 4 KB | event-driven | yes |
| `denon` | 0 | 5 | 6 KB | socket select + cmd queue | yes |
| `intent` | 0 | 5 | 4 KB | blocks on the intent queue | yes |
| `spotify` | 0 | 5 | 12 KB | adaptive 1–30 s | yes |
| `artwork` | 0 | 3 | 16 KB | on track change | yes (long) |
| `svc_scheduler` | **any** | 2 | 6 KB | periodic jobs | yes |
| `ota` | 0 | 3 | 8 KB | created on demand, destroyed after | yes |
| `httpd` | 0 | 5* | 4 KB* | request-driven; exists only while the pair overlay is open | yes |
| `net_wifi_dns` | any | 4 | 3 KB | SoftAP provisioning only | yes |
| `console` | 0 | 1 | 4 KB | REPL, blocking UART read | yes |

Two rows need reading carefully:

- **`svc_scheduler` is `tskNO_AFFINITY`** — the only unpinned task we create, so the opening
  line above ("core 0 hosts every network service") holds for everything *except* this one,
  which is free to land on core 1 alongside the UI. A real caveat when reasoning about UI
  jitter, not a formatting quirk.
- **`httpd`'s starred cells are ESP-IDF defaults, not our decision.** `pair_http.c` uses
  `HTTPD_DEFAULT_CONFIG()` and overrides only ports, socket count, LRU purge and core — so
  an IDF bump can change them with no diff here. Measured after a real POST: ~1820 of 4096
  bytes free, logged by `pair_http.c` itself.

Two more exist only behind build flags: `denon_selftest` and `spotify_selftest`, both
4 KB / prio 1 / core 0.

Notes that matter:

- **`lvgl_tick` is gone.** It was a 2 ms periodic `esp_timer` whose only job was calling
  `lv_tick_inc()` — 500 timer callbacks a second to advance a counter LVGL only ever reads.
  `ui_app.c` now hands LVGL a `lv_tick_set_cb()` callback that returns
  `esp_timer_get_time() / 1000` on demand instead, which is also strictly more accurate: the
  old push advanced in fixed 2 ms steps regardless of real elapsed time, so a delayed or
  coalesced callback silently made LVGL's clock run slow.
- **`intent` blocks on the queue rather than being polled.** It replaced a 20 ms periodic
  `sched` job that woke 50 times a second to find an empty queue on almost every one. Priority
  5 matches the two services it hands intents to, so a tap doesn't pay an extra scheduling hop.
  Its stack went 3 KB → 4 KB when `INTENT_DOMAIN_PAIRING` landed: the old size was justified
  by "no TLS, no NVS, no deep IDF call chains", which stopped being true once this task could
  reach `httpd_start()` and `svc_spotify_begin_pair()` (~1.4 KB peak). It would probably still
  have fitted; the justification would not have.
- **The UI task never calls anything that can block.** No sockets, no NVS, no I²C beyond
  the touch read, no `vTaskDelay` other than the LVGL loop's own. It drains its inbound
  event queue with a **zero timeout** at the top of each iteration, then renders.
- **Priorities are below the WiFi task** (23) and the LCD DMA path deliberately. We never
  want application work to starve the radio or the panel refresh.
- `artwork` sits at priority 3 because JPEG decode is a long CPU burn. On core 0 at low
  priority it cannot preempt the UI on core 1, and it yields to the network services.
- **Stack sizes are measured, not guessed.** `svc_diagnostics` logs
  `uxTaskGetStackHighWaterMark` over UART for a fixed list of seven tasks (`ui_app`,
  `svc_scheduler`, `artwork`, `denon`, `spotify`, `net_sup`, `intent`) — deliberately not
  "every task": it excludes ESP-IDF's own tasks, whose sizing this project doesn't own, and
  `ota`, which only exists during an update. The 25 % headroom floor is a rule applied by
  hand when reading those logs; **there is no automated soak test enforcing it** — Phase 7,
  which owns the soak, has not run. The value is in **bytes**, not words: ESP-IDF's port
  diverges from the vanilla FreeRTOS header on this, and reading it as words makes every
  task look like it has 4× its real headroom.
- Every task registers with the **task watchdog**, and **feeds it only after observable
  progress** — never once per loop iteration. A network task that feeds the watchdog at the
  top of a loop it is deadlocked inside reports itself healthy forever, which is the exact
  failure the watchdog exists to catch. Progress means: an event consumed, a request
  completed, a socket state change, or a rendered frame. A task blocked on a socket with a
  correct timeout will fail to feed and reboot — which is the desired outcome.
- **Core assignment was provisional; Phase 0 and Phase 5 Tier 7 measured it.** Pinning every network
  service to core 0 puts them alongside ESP-IDF's WiFi and LWIP tasks, and the contention is
  unproven. The table above began as a hypothesis and has since been
  validated under real load; four task stacks moved to PSRAM on the strength of measured
  high-water marks — see [RISKS.md](RISKS.md) R1 and [MEMORY.md](MEMORY.md) §1.
- **Two tasks are created after startup rather than at boot**, and both suspend `svc_artwork`
  first so peak RAM stays bounded. `ota` exists only for the duration of an update, created
  *after* `svc_artwork` has been suspended and its pool released. `httpd` exists only while
  the Spotify pair overlay is open ([adr/0011](adr/0011-on-device-pair-page-over-console-paste.md));
  its stack comes from internal RAM via a plain `xTaskCreate`, of which this board has ~24 KB
  free, and `svc_artwork`'s 16.7 KB contiguous TLS record buffer competes for the same pool —
  hence the same suspend treatment.
- `console` was not anticipated in this table's original (Phase 0) form. Phase 3 adds it as
  a UART REPL (`esp_console`) exercising `svc_denon` -- ROADMAP.md's Phase 3 bullet literally
  says *"a crude debug UI — text on screen, no theme,"* and this deliberately overrides that
  specific line: no `hal_display`/LVGL bring-up exists yet (Phase 5 scope), and none of Phase
  3's actual exit criteria need an on-screen UI to verify. Only compiled in behind
  `CONFIG_DSC_DENON_DEBUG_CONSOLE` (default off); `tools/check_release.py` refuses a
  production profile with it set.

---

## 5. Event and state architecture

Split into its own document — see **[EVENTS_AND_STATE.md](EVENTS_AND_STATE.md)**.

In short: **intents** travel UI-to-services on a single queue, so the ordering of user
actions is globally preserved. **State** travels services-to-UI on a topic bus, where every
event is a fixed-size POD copied by value — no allocation, no shared lifetimes. Large
payloads such as album art stay in a preallocated slot pool and are referenced by a
generation-tagged handle, which turns use-after-evict into a returned error rather than a
corrupted frame.

Each domain of `app_state_t` has exactly one writer, and readers get a `memcpy`'d snapshot,
so the UI can never observe a half-updated struct. Each domain also carries its own
`link_status` and `updated_us`, which is what lets the UI say "stale" honestly instead of
rendering a confident thirty-second-old volume number.

---

## 6. Memory strategy

Split into its own document — see **[MEMORY.md](MEMORY.md)**.

~2.1 MiB of the 8 MB PSRAM is committed, leaving ~5.9 MiB of deliberate headroom. Internal
SRAM, not PSRAM, is the scarce resource.

Allocation follows **bounded allocation domains** ([MEMORY.md](MEMORY.md) §2): every
allocation belongs to a named domain with a declared bound. There is deliberately *no* "no
allocation after startup" rule — it is unachievable on ESP-IDF, whose WiFi, LWIP, DNS, HTTP
and TLS stacks allocate continuously and are not ours to control. Application long-lived
memory is fixed at `app_start()`; everything else is pooled, bounded, and measured.

The dominant constraint is that the ST7701 sits on the parallel RGB bus, so panel DMA reads
PSRAM continuously (~27 MB/s) in competition with WiFi and JPEG decode — **every number in
MEMORY.md is provisional until the Phase 0 spike measures it**, and the measured internal-SRAM
table is an approval gate, not a formality.

---

## 7. Language and dependencies

- **C11**, ESP-IDF latest stable, pinned in `dependencies.lock` and committed. No floating
  version ranges — a toolchain that changes under a multi-year project is a liability.
- **LVGL 9.x, pinned to an exact patch version.** The v8→v9 migration was breaking; we
  choose when to take that cost, not `idf.py update-dependencies`.
- **We own the LVGL/task glue** — `esp_lvgl_port` is not used. The tick source, flush
  callback, and UI task loop are ours (`components/ui/ui_app/ui_app.c` + `ui_app_screens.c` —
  most of the growth past this doc's original ~200-line estimate is real-hardware-finding
  comments, not added logic), sitting on the critical path of the top project risk (R1). See
  [adr/0008](adr/0008-own-the-lvgl-glue.md).
- **No C++.** Its one plausible win is UI composition, which LVGL's C API and our
  widget-factory pattern already cover — without exceptions, RTTI, or a second ABI.

See [adr/](adr/) for the decisions above with full rationale and rejected alternatives.
