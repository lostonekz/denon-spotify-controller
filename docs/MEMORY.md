# Memory Strategy

Deliverable §7. Split out of [ARCHITECTURE.md](ARCHITECTURE.md) because it is the section
most likely to change as Phase 0 measurements come in.

Target: ESP32-S3 N16R8 — 8 MB octal PSRAM, ~512 KB internal SRAM, 16 MB flash.

---

## 1. Budget

### PSRAM

| Allocation | Size | Notes |
|---|---|---|
| LCD framebuffers, 2 × 480×480×2 | 900 KiB | owned by `esp_lcd_rgb_panel` |
| LVGL heap (dedicated pool) | 512 KiB | `LV_STDLIB_CUSTOM` onto our own pool |
| Artwork slots, 3 × 320×320×2 | 600 KiB | current / incoming / outgoing (crossfade) |
| Artwork thumbs, 3 × 60×60×2 | 21 KiB | blurred backdrop + palette source |
| JPEG staging buffer | 96 KiB | one decode at a time |
| Log ring, diagnostics | 16 KiB | |
| **Total** | **~2.1 MiB** | **~5.9 MiB headroom** |

That headroom is deliberate. It is what makes years of feature additions possible without
re-architecting the memory plan.

**Why 3 artwork slots and not 2:** a crossfade needs the outgoing and incoming images
simultaneously, and prefetching the next track's art needs a third. Two slots forces a
choice between smooth transitions and prefetch; the third slot costs 200 KiB of a resource
we have in surplus.

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
| WiFi + LWIP | ~60 KiB |
| mbedTLS, 2 concurrent sessions | ~50 KiB (tuned, §4) |
| Task stacks | ~50 KiB |
| IRAM-resident ISRs and hot paths | — |

Internal SRAM, not PSRAM, is what this design is actually rationed against. Every decision
below that looks conservative is conservative about this table.

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
| LVGL | dedicated PSRAM pool | 512 KiB | cannot touch system heap; `lv_mem_monitor` sampled |
| ESP-IDF network stacks | system heap | **not ours** | budgeted by measurement, not by rule |
| OTA | system heap, task-scoped | ~50 KiB, artwork suspended first | peak is *lower* than steady state |

What this buys that the absolute rule did not:

- **LVGL's churn is legitimate and contained.** Creating and destroying a settings screen
  allocates — that is normal LVGL usage. Confining it to a private pool means its
  fragmentation is bounded and can never starve networking. That isolation was always the
  real design goal; "no allocation" was a bad description of it.
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

**Phase 0 measured this on real hardware under real combined load, and the decision is made:
60 Hz, 2 framebuffers, 20-row (38 KiB) bounce buffers, WiFi/LWIP preferring PSRAM — the §1
table above and the original settings, all unchanged.** Real network load (not the
silently-idle version an earlier acceptance was wrongly based on — see
[RISKS.md](RISKS.md) R1) puts the drop rate at **6.7–7.1%**. Five different levers were
tried against that real number and none moved it meaningfully: larger bounce buffers, three
refresh rates (30/50/60 Hz), JPEG decode at the lowest task priority, and moving WiFi/LWIP
buffers off the PSRAM bus entirely (~12 KiB internal SRAM cost for a ~0.3-point change,
within run-to-run noise). Per-task attribution rules out JPEG decode and NVS writes as the
direct cause. The likely remaining source is WiFi's own baseline presence (association,
beacon reception), not any single load task — but with no tested lever changing the number,
**accepted at a 10% budget** rather than the original 0.1% target or the too-optimistic 2.0%
one, set with real margin above the observed range. Full history in
[RISKS.md](RISKS.md) R1. The one fallback never tried (single framebuffer with partial
refresh) is a bandwidth-side lever like the others that were, and the evidence points away
from bandwidth as the cause.

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
| Artwork slots | 3 × 200 KiB + 3 × 7 KiB | `svc_artwork` |
| JPEG staging | 1 × 96 KiB | `svc_artwork` |
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
