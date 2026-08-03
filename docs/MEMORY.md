# Memory Strategy

Deliverable §7. Split out of [ARCHITECTURE.md](ARCHITECTURE.md) because it is the section
most likely to change as Phase 0 measurements come in.

Target: ESP32-S3 N16R8 — 8 MB octal PSRAM, ~512 KB internal SRAM, 16 MB flash.

---

## 1. Budget

### PSRAM

| Allocation | Size | Notes |
|---|---|---|
| LCD framebuffer, 1 × 480×480×2 | 450 KiB | owned by `esp_lcd_rgb_panel` — 1 buffer, not 2; see §3 |
| LVGL heap (PSRAM pool) | 2048 KiB | added via `lv_mem_add_pool()`; see §3 for why 512 KiB wasn't enough |
| Artwork slots, 3 × 320×320×2 | 600 KiB | current + in-flight handoff + margin (see below) |
| JPEG staging buffer | 512 KiB | one decode at a time; see below |
| Log ring, diagnostics | 16 KiB | |
| **Total** | **~3.54 MiB** | **~4.46 MiB headroom** |

That headroom is deliberate. It is what makes years of feature additions possible without
re-architecting the memory plan.

**Real bug, found live (Phase 5, real hardware): the "artwork thumbs, 3 × 60×60×2, 21 KiB"
row this table used to list here never existed in code.** It described a blurred-backdrop/
palette-source thumbnail pool that was never actually implemented — `artwork_cache.c` only
ever allocates the one 3×320×320×2 pool above, and `artwork_palette_dominant_color_rgb565()`
(`artwork_service.c`) computes its palette directly against that same full-size decoded
slot, not a separate thumbnail. There is no blurred backdrop anywhere either — `w_art_card.c`'s
own top comment is explicit that the scrim IS the backdrop's darkening, a flat solid-opacity
rect, not a blurred image. Removed from the table above rather than left as aspirational.

**JPEG staging buffer raised twice on real hardware, 96 KiB → 256 KiB → 512 KiB.** The first
raise (96→256 KiB) fixed a real truncation of a fetched image (confirmed by the exact
failure signature — `esp_jpeg_decode` returning `JDR_INP`, tjpgd's own "wrong termination of
input stream" code, not a corrupt-data or format error — `net_http_request()`'s own contract
is silent truncation at the buffer cap, not an error). The second raise (256→512 KiB) fixed
the SAME signature recurring on a specific real cover (Capital Cities, "Safe and Sound"),
confirmed reproducible across many independent fetches of the identical URL — deterministic
per-cover, not transient network flakiness. A related real bug, found and fixed in the same
pass: `svc_artwork` used to mark a track "done" (stop retrying) only on a successful decode,
so a track whose cover repeatedly failed decode got re-fetched and re-decoded on *every* poll
for as long as it kept playing, hammering the CDN with an identical, doomed request forever.
Fixed with a bounded retry (`ARTWORK_FETCH_MAX_ATTEMPTS`, 3): a real, capped number of
attempts per track before giving up until the next real track change — enough to recover
from a genuine transient network hiccup on this long-lived streamed read, without retrying a
permanently-bad cover forever.

**Why 3 artwork slots and not 1:** `svc_artwork` publishes the new handle before releasing
the previous one (`artwork_cache.h`'s own ownership contract — never a window where
`core_state`/an in-flight event points at an already-released slot), so at least 2 must be
valid simultaneously during that handoff. The 3rd slot is real margin, not a currently-used
prefetch path — a future track-ahead prefetch could use it, but nothing fetches ahead of the
current track today (`artwork_cache.h`'s own `ARTWORK_CACHE_SLOT_COUNT` comment: "Phase 4
only ever holds 1 'live'"). 200 KiB of headroom for that, out of a resource we have in
surplus, isn't worth trimming back to 2 over.

**Why 320×320:** Spotify serves 640, 300, and 64 px variants. We fetch the 640 and decode at
**½ scale** — the JPEG decoder does this almost for free by dropping high-frequency
coefficients, and it beats fetching the 300 px variant and upscaling. 320 px displays at
300 px with a pixel to spare.

**Decoder is software, not hardware:** the ESP32-S3 has no JPEG codec peripheral at all —
that hardware block was introduced with the ESP32-P4 (`SOC_JPEG_CODEC_SUPPORTED` is unset for
S3, confirmed against `soc_caps.h` after wrongly assuming otherwise during Phase 0). The ½-scale
decode above is `espressif/esp_jpeg` (TJpgDec), which supports fractional output scale
natively (`JPEG_IMAGE_SCALE_1_2`) -- decodes a 640×640 source to 320×320 RGB565 directly, no
separate resize step. Timing under real combined load is exactly what the Phase 0 spike's
JPEG section now measures (`components/spike_phase0/spike_load_tasks.c`).

### Internal SRAM — the actually scarce resource

| Allocation | Size |
|---|---|
| RGB bounce buffers, 2 × 480×20×2 | 38 KiB |
| LVGL heap (static internal pool) | 24 KiB |
| WiFi + LWIP | ~60 KiB |
| mbedTLS, 2 concurrent sessions | ~50 KiB (tuned, §4) |
| Task stacks | ~50 KiB |
| IRAM-resident ISRs and hot paths | — |

Internal SRAM, not PSRAM, is what this design is actually rationed against. Every decision
below that looks conservative is conservative about this table.

The LVGL row above is small on purpose but not tiny: `lv_init()`'s own bootstrap (default
display/group/style-cache setup) needs real headroom in this pool before `ui_app.c` ever gets
a chance to add the real 2 MiB PSRAM pool from §1 — real hardware (Phase 5 Tier 7) found
Kconfig's own stated 2 KiB floor genuinely too small for `lv_init()` to even return, not just
too small for later widget work; 24 KiB gives that bootstrap real margin. `ui_app`'s own 12
KiB task stack is deliberately excluded from the "Task stacks" row above and from this
table's internal-SRAM total: real hardware found the internal-SRAM budget too tight for it by
the time `ui_app_create()` runs last in boot order, so it's PSRAM-backed instead
(`xTaskCreatePinnedToCoreWithCaps()`, `MALLOC_CAP_SPIRAM`) — the TCB itself still lands in
internal RAM automatically, only the stack moved. See §3.

---

## 2. Bounded allocation domains

An earlier draft of this document stated a rule of **"no dynamic allocation after
startup."** That rule is false, and stating it was worse than useless — it invited future
reviewers to trust an invariant the design already broke in at least four places.

It is false because ESP-IDF's WiFi driver, LWIP, DNS resolution, `esp_http_client`, and
mbedTLS all allocate and free continuously at runtime, on every connection and every
request. We do not control that and cannot switch it off. The design also knowingly
allocates for LVGL modals, the OTA task, and JSON parsing.

The honest rule, which is enforceable:

> **Every allocation belongs to a named domain with a declared bound. Nothing allocates
> from the global heap on an unbounded or unmeasured path.**

| Domain | Allocator | Bound | Enforcement |
|---|---|---|---|
| Application long-lived | `app_start()` only | fixed at boot | heap hook traps late allocs |
| Application variable-lifetime | `core_pool` fixed blocks | pool size | allocation fails, counted, degrades |
| LVGL | 24 KiB static internal + 2 MiB PSRAM pool | 2072 KiB total | cannot touch system heap; `lv_mem_monitor` sampled |
| ESP-IDF network stacks | system heap | **not ours** | budgeted by measurement, not by rule |
| OTA | system heap, task-scoped | ~50 KiB, artwork suspended first | peak is *lower* than steady state |

What this buys that the absolute rule did not:

- **LVGL's churn is legitimate and contained.** Building a screen's object tree allocates,
  and every text/style update after that can too — that is normal LVGL usage. Confining it
  to a private pool means its fragmentation is bounded and can never starve networking. That
  isolation was always the real design goal; "no allocation" was a bad description of it.
  (This bullet originally cited creating and destroying a settings screen; every screen is
  in fact created once at registration and never destroyed — UI_ARCHITECTURE.md §3 — so the
  churn that actually matters here is per-update, not per-screen-lifetime.)
- **The network stacks get a measured budget instead of a rule they were always violating.**
  Their steady-state and peak usage are characterised in Phase 0/2 and become numbers in
  §1, not an assumption.
- **The debug heap hook narrows to something checkable**: it traps allocations from
  *application* code outside a known-good context, and ignores ESP-IDF internals. A rule
  that fires constantly gets disabled; a rule that fires rarely gets read.

Monitoring stays as before, and matters more now that the rule is softer:

- `svc_diagnostics` samples free heap every 60 s and records a **slope**, per capability
  (internal vs PSRAM). A 24/7 device with a 40 B/hour leak dies in three months; slope
  monitoring finds it in three days.
- `lv_mem_monitor()` fragmentation is sampled separately, because LVGL can fragment its
  pool without leaking it.
- Pool utilisation high-water marks are reported per pool (§5).
- The 7-day soak test (ROADMAP Phase 7) is the actual acceptance gate. **Measurement, not
  assertion, is what makes this safe** — which is the correction the absolute rule was
  hiding.

---

## 3. The RGB panel constraint

The ST7701 is on the **parallel RGB bus**. The panel's DMA reads the framebuffer from PSRAM
continuously at the pixel clock — roughly **27 MB/s** at 480×480×16bpp×60 Hz. That traffic
competes with WiFi DMA, TLS working memory, and JPEG decode for the same PSRAM bus.

This is [RISKS.md](RISKS.md) R1, the highest-cost risk in the project. Consequences baked
into the design:

- **Bounce buffers in internal SRAM** (`bb_invalidate_cache`), so DMA burst reads are
  serviced from SRAM and PSRAM is touched in larger, more efficient bursts.
- `CONFIG_SPIRAM_FETCH_INSTRUCTIONS`, `CONFIG_SPIRAM_RODATA`, and
  `CONFIG_SPIRAM_XIP_FROM_PSRAM` are all **on** — see below; this reverses what an
  earlier draft of this document said, for a correctness reason found on real hardware.
- Octal PSRAM at ≥ 80 MHz, verified at bring-up rather than assumed.
- The LCD ISR and everything it calls live in **IRAM**.
- Artwork decode runs at priority 3 on core 0 specifically so it cannot saturate the bus
  during an animation on core 1.

### 3.1 XIP-from-PSRAM is required, not a bandwidth trade-off

An earlier draft of this section kept `SPIRAM_FETCH_INSTRUCTIONS`/`RODATA` **off**,
reasoning that keeping code and rodata out of PSRAM was the single most important R1
mitigation — less traffic competing with the LCD scanout. That reasoning was incomplete,
and Phase 0 caught it on the first hardware boot:

```
Guru Meditation Error: Core 0 panic'ed (Cache error).
Cache disabled but cached memory region accessed
```

The backtrace showed two interrupted call chains: `nvs_flash_init()` mid-`esp_flash_read()`
on one, and the LCD's GDMA end-of-frame ISR (`lcd_rgb_panel_fill_bounce_buffer`, refilling
from the PSRAM framebuffer) on the other. **Flash and PSRAM share the same cache/MMU path
on ESP32-S3.** A direct flash read briefly disables that cache; if the LCD's DMA interrupt
fires in that window and touches the PSRAM framebuffer, the core panics. This is not a rare
race — NVS writes (settings, WiFi state) and OTA checks are routine on a 24/7 device, and
the display is refreshing continuously, so the two collide on ordinary operation, not under
adversarial load.

`CONFIG_SPIRAM_XIP_FROM_PSRAM` is ESP-IDF's own documented fix (see
`components/esp_lcd/test_apps/rgb_lcd/sdkconfig.defaults.esp32s3` upstream) — it keeps the
ext-mem cache available during flash operations, at the cost of requiring
`FETCH_INSTRUCTIONS` and `RODATA`, i.e. code and rodata do live in PSRAM after all.

**A guaranteed crash is strictly worse than the bus contention the old setting was guarding
against.** The contention question doesn't go away, but it changes from something assumed
away at compile time to something the Phase 0 spike is positioned to measure directly —
which is the right place for that question to be answered.

**Phase 0 measured this on real hardware under real combined load, and a decision was made:
60 Hz, 2 framebuffers, 20-row (38 KiB) bounce buffers, WiFi/LWIP preferring PSRAM.** Real
network load (not the silently-idle version an earlier acceptance was wrongly based on — see
[RISKS.md](RISKS.md) R1) puts the drop rate at **6.7–7.1%**. Five different levers were
tried against that real number and none moved it meaningfully: larger bounce buffers, three
refresh rates (30/50/60 Hz), JPEG decode at the lowest task priority, and moving WiFi/LWIP
buffers off the PSRAM bus entirely (~12 KiB internal SRAM cost for a ~0.3-point change,
within run-to-run noise). Per-task attribution rules out JPEG decode and NVS writes as the
direct cause. The likely remaining source is WiFi's own baseline presence (association,
beacon reception), not any single load task — but with no tested lever changing the number,
**accepted at a 10% budget** rather than the original 0.1% target or the too-optimistic 2.0%
one, set with real margin above the observed range. Full history in [RISKS.md](RISKS.md) R1.

**That 2-framebuffer part of the decision was later reversed, for real, on real hardware —
this is not the Phase 0 measurement above being wrong, it's Phase 5 Tier 7 finding a second,
different problem the Phase 0 spike never exercised.** The one fallback this section
previously called "never tried... evidence points away from bandwidth as the cause" (single
framebuffer with partial refresh) WAS tried once the real full application (15 services, the
real widget tree, `svc_diagnostics`'s own periodic reporting) existed — and it was a real,
necessary part of fixing a periodic on-screen corruption/vertical-shift symptom, alongside a
genuinely different root cause (a scheduler-freezing diagnostics call, nothing to do with
PSRAM bandwidth) that the Phase 0 spike had no equivalent of and so could never have
surfaced. **Current, real settings: 30 Hz, 1 framebuffer, `LV_DISPLAY_RENDER_MODE_PARTIAL`
with a 40-row LVGL-owned draw buffer, 20-row (38 KiB) bounce buffers unchanged.** The 30 Hz
figure is itself a later correction (performance sweep, 2026-08-01): the first 30 Hz attempt
was retried and found worse at the time, but that result was confounded by the same
scheduler-freeze bug fixed in this same tier — once fixed, a clean re-test on real hardware
found 30 Hz visually indistinguishable from 60 Hz, so it was kept for the PSRAM-bandwidth
headroom it frees up. Full account in [RISKS.md](RISKS.md) R1's own Phase 5 Tier 7 update and
its performance-sweep follow-up, including why none of this overturns the original
6.7–7.1%/WiFi-baseline finding above.

**A later real-hardware pass (2026-08-02) tried reversing this same decision again —
`num_fbs=2` + `LV_DISPLAY_RENDER_MODE_FULL`, real double-buffering — and confirmed it WORSE:
static black partial frames, up to the bottom ~70% of the screen, from the LCD driver
swapping which framebuffer is "current" synchronously in `flush_cb`, with no wait for the
bounce-buffer refill ISR to finish scanning out the current one first. Reverted back to the
settings above.** A leftover diagnostic flag (`CONFIG_HEAP_POISONING_COMPREHENSIVE`, left
enabled from an unrelated investigation) was also found contending with the same bounce-buffer
refill ISR for CPU/bus time — real cause of an unrelated "white lines on the left edge"
report, now fixed by disabling it. Full account, including a third still-not-fully-explained
symptom (a "vertical anchor jitter" on frequently-redrawn widgets), in
[RISKS.md](RISKS.md) R1's own 2026-08-02 update.

**Why the LVGL pool grew from 512 KiB to 2 MiB (PSRAM) + 24 KiB (static internal), a real
finding, not a redesign:** the original 512 KiB figure was never actually wired up as a real
pool — `ui_app.c` only ever called `lv_init()` and nothing else, so LVGL ran on whatever tiny
static pool Kconfig's `LV_MEM_SIZE_KILOBYTES` happened to be set to. Real hardware exposed
this in two stages. First, the very first real render (the Art-forward Now Playing screen's
rounded, layered art card) permanently froze the UI task inside LVGL's own allocator —
`LV_USE_ASSERT_MALLOC`'s default handler is a bare `while(1);` on out-of-memory, not a clean
failure, so this looked like a hang, not an OOM. Wiring up the real PSRAM pool via
`lv_mem_add_pool()` (as this section always intended) was the fix — sized at 2 MiB, not the
original 512 KiB, because LVGL's software renderer composites layers (needed for the
rounded-corner clip the Art-forward design called for) in the layer's own color format, and a
near-full-480×480 layer at ARGB8888 (4 B/px) is ~900 KiB, comfortably over 512 KiB before
accounting for the rest of the object tree. Second, shrinking the static internal pool all
the way to Kconfig's stated 2 KiB floor (the original plan, so that floor wouldn't silently
eat into the 512 KiB PSRAM budget) turned out to be too small for `lv_init()`'s own bootstrap
to even return — 24 KiB was the real, measured fix, still far below the old unconditional
64 KiB default. Separately, the rounded-corner clip that originally justified sizing for a
full-screen ARGB8888 layer was later removed entirely (a real LVGL compositing-layer bug,
not a memory problem — see [UI_ARCHITECTURE.md](UI_ARCHITECTURE.md) §4's `w_art_card` entry),
so the 2 MiB figure now has more headroom than its own original justification strictly needs;
not shrunk back down without a reason to, since PSRAM is not the scarce resource here.

---

## 4. TLS memory arbiter

mbedTLS costs ~40 KB of internal SRAM per session at default fragment lengths. Three
concurrent HTTPS connections — Spotify API, artwork, OTA — would exhaust internal RAM, and
the failure surfaces as an allocation error deep inside the TLS stack rather than as
anything diagnosable.

- A counting semaphore in `net_http` caps concurrent TLS sessions at **2**.
- `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=4096` and `OUT_CONTENT_LEN=2048` cut roughly 25 KB per
  session. Both Spotify and the OTA host negotiate max-fragment-length correctly; this is
  verified in Phase 2 rather than assumed.
- Session tickets are cached for `api.spotify.com`, so the frequent poll connection resumes
  instead of performing a full handshake every 5 s — a significant CPU and latency saving on
  the hottest network path.
- OTA takes both slots, with artwork suspended for the duration.

**Measured, Phase 2, real hardware (ESP32-S3, `idf.py build` release config, COM6):** with
WiFi joined (`net_wifi` → `ONLINE`) and one real TLS session in flight (`svc_ota`'s manifest
check, one `net_http_tls_acquire(1, ...)` slot — the "OTA takes both slots" case above is not
yet exercised, since the streaming image download needing 2 slots is a Tier 7 exercise, not
this one), `heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)` recorded **123.7 KiB**
(126663 B) free internal SRAM at its lowest point since boot, against the ~512 KiB total —
comfortably above R1's 60 KiB floor (§ above), with `net_wifi`/`net_time`/`net_http`/`svc_ota`
all running concurrently. This is a Phase 2 data point, not R1's own combined-load figure:
no display/artwork/JPEG pipeline exists yet to contend with WiFi/TLS for the same budget
(that combined measurement is R1's, Phase 0, before any of Phase 2 existed) — this replaces
Phase 2's own planning-stage guess with a real number, the same way Phase 0 replaced R1's.
`svc_diagnostics`'s periodic heap report now logs `internal_free`/`internal_free_min`
(`MALLOC_CAP_INTERNAL`) directly, alongside the pre-existing combined-heap and PSRAM-only
figures, so this number is a permanent, ongoing diagnostic rather than a one-off spike
measurement.

Not yet verified: the "Both Spotify and the OTA host negotiate max-fragment-length
correctly" claim above — the measurement here used a real public HTTPS endpoint
(`www.google.com`) as a TLS handshake target, not the actual (not-yet-existing) OTA host or
Spotify's API (Phase 4). Tier 7's exit-criteria proofs (below) deliberately served the
manifest/image over plain HTTP from a local test server rather than HTTPS — they exercise
`ota_policy`'s decision logic and the real bootloader-level admit/reboot/rollback path, not
the TLS transport itself (no custom CA trust bundle was worth building for a self-signed
local server just for this). So this specific claim — real dual-TLS-slot pressure and
max-fragment-length negotiation against an actual OTA host — stays open until a genuine
HTTPS OTA host exists.

**Tier 7 exit-criteria proofs — all three passed on real hardware (ESP32-S3, COM6):**

1. **Wrong signing key rejected.** A manifest signed with a key outside the trust anchor was
   fetched and rejected with `BAD_MANIFEST_SIG` before any image download began.
2. **Downgrade rejected.** With the device's confirmed `running_security_version` bumped to
   1, a correctly-signed manifest offering `security_version=0` was rejected with `ROLLBACK`
   — `ota_policy.c`'s ordering (signature verified before the rollback check runs) held.
3. **Deliberately-broken image rolls back.** A build compiled with a test-only Kconfig flag
   (`DSC_TEST_FORCE_HEALTH_GATE_TIMEOUT`, withholds `HEALTH_COND_BOOT_COMPLETE` so the health
   gate can never PASS) was downloaded via the real OTA path, admitted, and booted. The
   device correctly entered `OTA_FSM_STATE_PENDING_VERIFY` on that boot (confirmed via
   `esp_ota_get_state_partition()`, not a direct-flash boot), the gate timed out at ~121.5 s
   against the 120 s window, and `esp_ota_mark_app_invalid_rollback_and_reboot()` fired,
   logging ESP-IDF's own `Rollback to previously worked partition` before rebooting back onto
   the original good build. The bootloader-level rollback mechanism this proof exercises is
   independent of application code — see `bootloader_utility.c`.

This run also caught a real bug, not a test artifact: `svc_scheduler`'s task stack (3072 B,
sized in Phase 1 for lightweight settings/diagnostics ticks) overflowed the first time an OTA
apply action ran on it — `esp_ota_set_boot_partition()` + `esp_restart()`'s WiFi/netif
teardown chain is far deeper than anything Phase 1 exercised on that shared worker task.
Fixed by raising it to 6144 B; re-verified by rerunning the full download-reboot-timeout-
rollback cycle to completion. Any future service that dispatches OTA-apply-style actions onto
a shared tick-driven task should budget for this.

---

## 5. Pool inventory

Every pool is sized at startup from a Kconfig constant, and every one reports utilisation
and high-water mark to `svc_diagnostics`.

| Pool | Blocks × size | Owner |
|---|---|---|
| Event queues (per subscriber) | see [EVENTS_AND_STATE.md](EVENTS_AND_STATE.md) §5.3 | `core_eventbus` |
| Intent queue | 16 × 64 B | `core_eventbus` |
| Artwork slots | 3 × 200 KiB | `svc_artwork` |
| JPEG staging | 1 × 512 KiB | `svc_artwork` |
| HTTP response buffers | 2 × 16 KiB | `net_http` |
| **JSON parse scratch** | cJSON, general heap per parse call (see note below) | `svc_spotify` |
| String scratch (formatting) | 8 × 128 B | `ui` |
| Log ring | 8 KiB | `svc_logging` |
| Coredump summary staging | 1 × 2 KiB | `svc_diagnostics` |

**JSON parsing strategy**, since it is otherwise a hidden heap consumer — revised from an
earlier draft of this document, which described a streaming pull parser with no `cJSON` and
no per-node allocation. Phase 4 (`svc_spotify`) uses `cJSON` (ESP-IDF's built-in `json`
component) instead: a `/v1/me/player` response is a large, evolving, third-party JSON blob
where [RISKS.md](RISKS.md) R5 wants tolerance to unknown/additive fields, not a closed schema
a hand-rolled parser could reject strictly against — the opposite trade `svc_ota`'s manifest
parser makes, and for the opposite reason. See
[adr/0010](adr/0010-cjson-over-hand-rolled-parser.md).

`spotify_parse.c` is the *only* file that ever builds a cJSON tree: it parses one response,
extracts fields by key into `core_types` structs, and frees the tree before returning — peak
heap is bounded to one parse call's lifetime, never retained across calls or shared with
anything else. This does **not** offer the streaming parser's hard input-ceiling protection
against a malformed or oversized response; `net_http_get()`'s own truncate-not-error contract
(a fixed response buffer cap) is what bounds the input `spotify_parse.c` ever sees.

A real measured peak-heap number for a `/v1/me/player` response is **pending Tier 6**
hardware verification (needs a live Spotify account) — intentionally not filled in with a
placeholder here.

If a pool ever runs dry, that is a **design error, not a runtime condition**: the allocation
fails, the failure is counted and logged, and the caller degrades visibly. We never block
waiting for a pool block, because blocking on a shared pool is how a low-priority task
deadlocks a high-priority one.
