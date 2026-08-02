# Risk Register

Ordered by expected cost = likelihood × damage × how late we'd find out.
Anything marked **Phase 0** must be answered before the architecture is frozen.

---

## R1 — PSRAM bandwidth contention with the RGB panel · **Phase 0 — ACCEPTED**

**Risk.** The ST7701 is on the parallel RGB bus, so panel DMA reads the framebuffer out of
PSRAM continuously (~27 MB/s at 480×480×16bpp×60 Hz). WiFi DMA, TLS working memory, and
JPEG decode compete for the same bus. Symptoms are visible tearing, horizontal glitch
lines, or a permanently offset image — and they appear *only* under combined load, which
means they appear late, in front of the user, and not on the bench.

**Why it's #1.** Every other risk here is recoverable in software. This one can invalidate
the memory plan, the frame rate target, and the artwork pipeline simultaneously.

**Mitigation.** Bounce buffers in internal SRAM; `SPIRAM_XIP_FROM_PSRAM` on (required to
avoid a cache-disable panic when a flash op coincides with LCD DMA — found on first
hardware boot, see [MEMORY.md](MEMORY.md) §3.1); octal PSRAM at ≥ 80 MHz confirmed at
bring-up; LCD ISR in IRAM; artwork decode at low priority on core 0.

**Proof required in Phase 0:** a spike that renders a moving gradient at 60 fps while
simultaneously (a) pulling a 5 MB HTTPS download, (b) decoding a 640×640 JPEG in a loop,
and (c) writing NVS. Instrument dropped frames. If it tears, we find out in week one, when
dropping to a single framebuffer, a lower refresh, or partial-refresh mode is still cheap.

**Phase 0 status: measured on real hardware under real combined load, decision made.**

A first pass fixed three spike bugs (vsyncs-not-frames drop-rate denominator, CPU-bound
fill pattern, render loop not actually pinned to core 1) and a timing artifact that
misreported the WiFi-connect wait as a 1.3 s render stall, landing on a steady-state
**~1.5% dropped-frame rate** and a 2.0% budget. That acceptance was wrong: an independent
review caught that every run behind it had `bytes_downloaded == 0` for the full 120 s — the
network load task was silently never exercising the WiFi/PSRAM contention this risk is
named for. Once network load actually transferred data (Cloudflare's speed-test endpoint,
after Hetzner turned out to fail for unrelated reasons — see
[spike_net_load.c](../components/spike_phase0/spike_net_load.c)), the real combined-load
drop rate jumped to **6.7–7.1%**.

Everything tried against that real number left it unchanged or moved it within noise:
- **Larger bounce buffers** (20→40 rows): unchanged (1.54%→1.47%, before real network load
  was in the picture), and cost enough internal SRAM to newly fail the 60 KB floor on its
  own. Reverted.
- **Three refresh rates** (60/50/30 Hz), tested against the real ~7% number: all landed
  within a point of each other. Reverted to 60 Hz — no reason to pay a lower refresh rate
  for no benefit.
- **JPEG decode at the lowest task priority**, and a **real 640×640 → 320×320 decode**
  (`espressif/esp_jpeg`/TJpgDec, replacing an earlier synthetic placeholder — see
  [MEMORY.md](MEMORY.md) §1): per-task attribution shows only 0–2 of ~260 drops per run
  land inside a JPEG decode window. Not the cause. (JPEG decode itself is real now and
  genuinely slow — avg 660 ms / max ~1.8 s, over the spike's separate 400 ms budget. Still
  open; see below.)
- **Moving WiFi/LWIP buffers off the PSRAM bus** (`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`):
  the most mechanistically direct lever tried — WiFi's RX/TX buffers normally allocate in
  PSRAM first, meaning its background housekeeping (not just active transfers) competes
  with the LCD scanout continuously. Disabling it moved the rate from ~7.0% to ~6.7%, within
  the run-to-run noise already observed, while costing ~12 KiB of internal SRAM. Not a clear
  enough win. Reverted.
- **Single framebuffer with partial refresh** (original third fallback): not tried. Like
  the other display-side levers, there's no reason to expect it to behave differently
  against a number that hasn't responded to any bandwidth-side change.

**Decision: accepted at the original 60 Hz / 2 framebuffers / 20-row bounce buffers /
WiFi-prefers-PSRAM settings, budget set to 10%** — set with real margin above the observed
6.7–7.1% range, not against a number no tested configuration has actually cleared. The
likely remaining cause is WiFi's own baseline presence (association, beacon reception)
rather than any single load task, but no further lever tested changes it, so continuing to
chase a lower number has diminishing returns. The architecture is unfrozen on this point
specifically because a regression past 10%, or visible tearing once real UI exists, would
mean this acceptance was wrong — not because the number itself is still in question.

**Still open, separately:** JPEG decode timing (avg 660 ms / max ~1.8 s against a 400 ms
budget) is real and unresolved. Artwork would visibly lag a track change at these numbers.
Not blocking R1's drop-rate question, but blocking a future "artwork feels instant" bar.

**Phase 5 Tier 7 update — real full application, not the Phase 0 spike, found a different,
more severe problem than anything above, and corrected one of this section's own
predictions.**

The Phase 0 spike above never exercised this project's actual full application: 15 real
services, the real widget tree, and `svc_diagnostics`'s periodic reporting all running
together. On real hardware with all of that in place, Now Playing showed a periodic,
regularly-timed band of corrupted/blown-out color sweeping the screen, plus a fixed-interval
vertical shift — a materially worse and more visible symptom than the ~7% figure above ever
described, and on a fixed timer, not the noisy/load-correlated pattern R1's own WiFi-baseline
theory would predict.

Two real, distinct causes, found in order:

1. **`svc_diagnostics_report_tick()` was periodically freezing the entire scheduler.** It
   called `uxTaskGetSystemState()` every 5 s to log every task's stack high-water mark.
   Confirmed by reading `FreeRTOS-Kernel-SMP/tasks.c` directly (this target is dual-core):
   that call wraps its ENTIRE per-task stack scan in `vTaskSuspendAll()`, freezing every task
   on both cores for its whole duration — long enough, with ~15 tasks including a 12 KiB UI
   stack, to starve the RGB panel's bounce-buffer DMA refill on a fixed 5 s cadence. This has
   nothing to do with PSRAM bandwidth at all — it is a scheduling problem, a risk category
   this section never considered, because the Phase 0 spike had no equivalent periodic
   diagnostic doing a full-system task enumeration. Fixed properly (not just made less
   frequent) by resolving each of this project's own task handles once by name at boot
   (`xTaskGetHandle()`, one small suspend, done once) and using
   `uxTaskGetStackHighWaterMark(handle)` per tick thereafter — confirmed by reading the same
   file, that single-task call never suspends the scheduler at all. See
   `components/svc_diagnostics/svc_diagnostics.c`'s own top comment for the full detail.
2. **"Single framebuffer with partial refresh" — this section's own "not tried... no reason
   to expect it to behave differently" fallback — was finally tried, and that prediction was
   wrong.** Against real, non-synthetic Now Playing content (not a moving gradient), switching
   from `LV_DISPLAY_RENDER_MODE_FULL` (redraw the whole 480×480 screen every cycle, into one
   of 2 alternating framebuffers) to `LV_DISPLAY_RENDER_MODE_PARTIAL` (LVGL's own small
   intermediate draw buffer, only the genuinely-changed area touches PSRAM) was a real,
   necessary part of the fix — most ticks now move only a moving playhead or a ticking clock,
   not a full frame, which gave real additional margin against cause #1's own DMA-starving
   effect (and would plausibly help against genuine bandwidth contention too, though that was
   never isolated as a separate variable here). A second buffer would only reintroduce
   staleness for partial updates (`esp_lcd_rgb_panel`'s own `draw_bitmap()` writes into
   whichever framebuffer index is "current", consistent frame-to-frame only with exactly
   one), so `num_fbs` dropped from 2 to 1 as part of the same change.

**Refresh rate retested against the real symptom, not just the old drop-rate metric: 30 Hz
made both symptoms WORSE, not better**, then reverted to 60 Hz. Consistent with this
section's own original three-refresh-rate finding (30/50/60 Hz all within a point of each
other) if the dominant real-world cause is cause #1 above (a roughly fixed-duration freeze,
so a slower frame period just means each occurrence is visible for a larger fraction of a
frame) rather than a bandwidth effect refresh rate would directly move.

**This does not overturn the original Phase 0 finding.** The ~6.7–7.1% figure and "no
bandwidth-side lever moves it" conclusion were measured rigorously, with per-task attribution
ruling out specific causes, against a real spike under real load — that measurement stands as
a separate, likely still-real, minor residual (WiFi's own baseline presence). What changed is
that it was never the dominant VISIBLE symptom once the real widget tree and the real
diagnostics routine both existed — those are new interactions the spike, by construction,
could not have exercised.

**Performance sweep update (2026-08-01) — 30 Hz re-tested with cause #1 fixed, and
confirmed good.** The WORSE-at-30-Hz result immediately above was measured while
`svc_diagnostics`'s scheduler-freeze bug (cause #1) was still active — a fixed-duration
freeze is exactly the kind of confound that makes a slower frame period look worse for
reasons that have nothing to do with refresh rate itself. With that freeze genuinely fixed,
30 Hz was re-tested on real hardware against the real Now Playing widget tree: no corner
artifacts, no vertical shift, no visible regression versus 60 Hz. This is consistent with,
not a reversal of, the original Phase 0 three-refresh-rate finding (drop rate flat across
30/50/60 Hz) — the earlier WORSE-at-30-Hz result was the anomaly, explained by a confound
that no longer exists, not a real cost of a lower refresh rate. **Current settings: 30 Hz
(8 MHz pixel clock), 1 framebuffer, `LV_DISPLAY_RENDER_MODE_PARTIAL` with a 40-row
LVGL-owned draw buffer, 20-row (38 KiB) bounce buffers unchanged.** Settled at 30 Hz for the
PSRAM-bandwidth headroom it frees over 60 Hz, with no observed visual cost.

**Real-hardware update (2026-08-02) — three more real findings against this same risk,
chasing a fresh visual-artifact report on Now Playing content:**

1. **A leftover diagnostic flag, not a rendering bug, caused "white lines/dots popping up on
   the left edge of the screen."** `CONFIG_HEAP_POISONING_COMPREHENSIVE` had been switched on
   during an unrelated LVGL-allocation investigation (a device-switcher feature, since fully
   reverted) and never turned back off. Its real CPU cost (a periodic full-heap integrity
   walk around allocations) was contending with the LCD's own DMA bounce-buffer refill ISR
   for the same internal-RAM bus — exactly this section's own subject matter, just from a
   source R1 had never previously considered (a debug-only Kconfig option, not application
   load). Fixed by disabling it again (`CONFIG_HEAP_POISONING_DISABLED=y`); the artifact has
   not recurred since.
2. **`num_fbs=2` + `LV_DISPLAY_RENDER_MODE_FULL` (real double-buffering) was retried, on the
   theory that the single-buffer/PARTIAL config above might itself be the cause of a
   *different*, newly-reported symptom (see #3) — and confirmed WORSE, not reverted on
   suspicion.** Real symptom: static black partial frames, up to the bottom ~70% of the
   screen. Root cause, confirmed by reading `esp_lcd_panel_rgb.c`'s `rgb_panel_draw_bitmap()`
   directly: with bounce buffers active (this config always uses them), swapping
   `cur_fb_index` happens immediately/synchronously when `flush_cb` calls
   `esp_lcd_panel_draw_bitmap()`, with no wait for the CURRENT frame's real scan-out to
   finish first — the bounce-buffer refill ISR reads whichever buffer is "current" at
   whatever row it happens to be mid-scan, so a swap landing mid-frame can start pulling from
   a buffer LVGL only just started drawing into. Fixing double-buffering correctly here would
   need `lv_display_flush_ready()` gated on a real VSYNC boundary after the swap actually
   takes effect, not synchronous inside `flush_cb` — not attempted; reverted back to
   `num_fbs=1`/PARTIAL instead. See `hal_display.h`'s own top comment for the full account.
3. **A distinct "vertical anchor jitter" symptom — the progress bar/transport/labels
   visibly jumping to a different vertical position on nearly every frame, while the art card
   (redrawn only on a real track change) stayed rock stable — is NOT the same thing as the
   "fixed-interval vertical shift" documented under the Phase 5 Tier 7 update above (that one
   was `svc_diagnostics`'s scheduler-freeze bug, already fixed).** One fix attempt (giving the
   bottom stack a fixed height instead of `LV_SIZE_CONTENT`, removing a theoretical per-tick
   layout-recompute dependency) did not resolve it. It stopped being visible once the
   `num_fbs=2` experiment above was reverted back to the known-safe config — consistent with,
   but not proof of, this section's own PSRAM-bandwidth-contention theory: real Now Playing
   content redraws the progress bar/transport/labels every tick, exactly the kind of
   frequently-touched region this risk's original ~6.7–7.1% figure was measured against, just
   never visually this prominent against Phase 0's plain gradient test pattern. Not fully
   root-caused — treated as fragile-but-currently-working, not solved.

**Still open, separately:** JPEG decode timing (avg 660 ms / max ~1.8 s against a 400 ms
budget) is real and unresolved — see above. A related but distinct problem, JPEG decode
*failure* (truncated input, `JDR_INP`), was found and fixed this same session: see
[MEMORY.md](MEMORY.md) §1 for the staging-buffer size history and the bounded-retry fix.

---

## R2 — Denon telnet is single-client, device-wide

**Risk.** Port 23 accepts one connection. Home Assistant, the Denon phone app, or any other
controller in the house will take it, and we get a refused connection or a silent EOF with
no explanation. If the design assumes telnet, the product intermittently dies for reasons
the user cannot see.

**Mitigation.** Dual transport behind `denon_transport_if`, automatic HTTP fallback,
periodic promotion attempts, telnet released while asleep. Link state is surfaced in
diagnostics but **not** as a user-facing error, because in fallback everything still works.

**Residual.** HTTP fallback has 300 ms–1 s latency and no push events, so volume feedback
feels less immediate. Acceptable, and honest.

---

## R3 — Spotify Web API has no push channel

**Risk.** Polling is the only option. Rate limits are a rolling window and are not published
precisely, so "just poll faster" is not available. Naive polling either feels laggy or gets
429'd.

**Mitigation.** Adaptive intervals, local progress interpolation (a 5 s poll renders as a
60 fps bar), optimistic UI with force-poll convergence after intents, client-side token
bucket, strict `Retry-After`, artwork fetched only on track-ID change.

**Residual.** A change made on another device (someone skips a track from their phone) can
take up to 5 s to appear. Inherent to the platform.

---

## R4 — Spotify OAuth on a headless wall device

**Risk.** PKCE refresh tokens **rotate**; dropping the rotated token strands the device
after days or weeks — the classic silent failure for this class of integration. Password
changes and app revocation also invalidate tokens with no warning. A device with no
keyboard cannot easily recover.

**Mitigation.** Persist the rotated refresh token to NVS *before* using the new access
token, so a power cut mid-refresh is survivable. Proactive refresh at 80 % of `expires_in`.
A `REAUTH_REQUIRED` state that shows an on-screen QR code for re-pairing from a phone —
recovery without a factory reset or a serial console.

---

## R5 — Third-party API drift over a multi-year life

**Risk.** Spotify has deprecated endpoints before (Implicit Grant, and several metadata
endpoints in 2024). Denon's HTTP interface is undocumented and has changed across firmware
revisions. Either can break this device while it hangs on a wall.

**Mitigation.** All Spotify JSON parsing confined to `spotify_parse.c`; all Denon encoding
to `denon_codec.c`. No vendor field name appears anywhere else. Host tests run against
captured fixtures. A Denon capability probe (`Deviceinfo.xml`) at startup detects a changed
device rather than misbehaving. OTA exists precisely so this is fixable remotely.

---

## R6 — Memory exhaustion from TLS concurrency

**Risk.** mbedTLS costs ~40 KB internal SRAM per session. API + artwork + OTA concurrently
exhausts internal RAM, and the failure mode is an allocation failure deep inside the TLS
stack — an obscure crash, not a clean error.

**Mitigation.** Counting semaphore caps concurrent sessions at 2; reduced fragment lengths
(`IN=4096`, `OUT=2048`) save ~25 KB/session; session-ticket reuse for `api.spotify.com`;
artwork suspended during OTA.

---

## R7 — Slow heap leak on a 24/7 device

**Risk.** A device that reboots weekly hides leaks. This one runs for months. A 40 B/hour
leak is invisible in testing and fatal in the field.

**Mitigation.** Bounded allocation domains ([MEMORY.md](MEMORY.md) §2) — every allocation
path has a declared bound and a monitored watermark, including the ESP-IDF network stacks
that allocate continuously and are not ours to control; LVGL confined to its own
pool so its fragmentation cannot starve networking; free-heap slope sampled every 60 s and
reported as a fault; a debug-build heap hook that traps unexpected `malloc`; a mandatory
**7-day soak test** before any release, with watermarks recorded.

---

## R8 — NVS flash wear

**Risk.** A brightness slider writing NVS per change destroys the sector in months. Silent
until the device stops keeping settings.

**Mitigation.** RAM shadow + 2 s debounced flush; lifetime write counter surfaced in
diagnostics and reviewed during soak.

---

## R9 — LVGL version drift

**Risk.** LVGL v8→v9 was breaking. An unpinned dependency means a routine
`idf.py update-dependencies` can silently break the entire UI layer. Because we own the
display glue rather than using `esp_lvgl_port` ([ADR-0008](adr/0008-own-the-lvgl-glue.md)),
a major LVGL change also lands on our `lv_display` and flush-callback code rather than being
absorbed upstream.

**Mitigation.** Exact patch pin in `dependencies.lock`, committed. Upgrades are a
deliberate, scheduled task with its own branch — never a side effect. The glue is small
(~200 lines) and concentrated in two files, so the blast radius of an LVGL API change is
bounded and visible.

---

## R9b — We own the RGB panel glue

**Risk.** Declining `esp_lvgl_port` means the fiddly parts — VSYNC-synchronised buffer
swap, bounce-buffer cache invalidation, flush timing — are ours. Bugs here produce
intermittent visual artifacts that are hard to attribute and easy to misdiagnose as LVGL or
hardware faults.

**Mitigation.** This code is written during **Phase 0**, in isolation, when it has full
attention and a dedicated measurement harness — not squeezed in alongside UI work in
Phase 5. `esp_lvgl_port` remains a viable fallback: the `hal_display` ↔ `ui_app` interface
is deliberately narrow enough to swap. Accepted knowingly because this layer is the direct
lever on R1, which is the risk worth controlling.

---

## R10 — UI task stall freezes the whole device

**Risk.** One blocking call in an LVGL event handler and the panel is frozen. On a wall
device with no console, that is indistinguishable from dead hardware.

**Mitigation.** Hard no-blocking rule; task watchdog on every task (a stall becomes a
reboot with a coredump, which is strictly better than a frozen panel); a debug assertion on
loop duration; the review checklist explicitly covers it.

---

## R11 — Clock/TLS bootstrap ordering

**Risk.** TLS certificate validation fails without a valid wall clock. Cold boot after a
long power cut → SNTP not yet synced → Spotify fails with a confusing certificate error.

**Mitigation.** `TOPIC_TIME` gates the first TLS request. The `SYNCING` state is explicit in
the connectivity FSM and shown honestly in the UI rather than surfacing as an auth failure.

---

## R12 — Thermal, in-wall, 24/7

**Risk.** Backlight, WiFi, and a continuously-scanning RGB panel in a sealed enclosure.
Sustained high junction temperature shortens PSRAM and flash life and can cause WiFi
instability that looks like a software bug.

**Mitigation.** Log the internal temperature sensor in diagnostics from day one; check it
during the soak test; treat the enclosure as a design deliverable, not an afterthought.
Night dimming reduces the largest heat source.

---

## R13 — Volume safety

**Risk.** A stray touch or a coalescing bug sends `MV98` to an AVR driving real speakers.
This damages hearing and hardware, and it is the one bug here with consequences outside the
device.

**Mitigation.** `volume_limit_max` clamped in `svc_denon` at the **protocol boundary**, not
in the UI — so it holds regardless of which screen, gesture, or future integration issues
the command. Explicit boundary tests in `test_denon_codec.c`, including the 2-vs-3-digit
`MV` ambiguity (`MV52` = 52.0, `MV525` = 52.5) that is the most likely source of a 10 dB
error.

---

## R15 / R16 — parked

Both were about the production profile specifically (anti-rollback backed by an eFuse
counter that doesn't exist yet; loss/compromise of a release key that hasn't been created
yet) — neither is a live risk on a dev-only device with no eFuses burned and no release key
issued. Full text parked in [PRODUCTION_DEFERRED.md](PRODUCTION_DEFERRED.md), not deleted.

---

## R17 — Non-Latin metadata renders badly

**Risk.** The glyph set is Latin-1 + Latin Extended-A. Spotify metadata is user-generated
and international; a Japanese bonus track or Cyrillic remix credit will appear eventually.
Worse than missing glyphs is a UTF-8 decoder that truncates a multi-byte sequence at a
buffer boundary and emits garbage.

**Mitigation.** Codepoint-aware truncation in `view_model`, host-tested against adversarial
strings. U+FFFD for unrepresentable glyphs, never `.notdef` boxes. Entirely-unrepresentable
strings fall back to a meaningful alternative. `svc_diagnostics` counts the events, so the
decision to widen coverage would be made on evidence rather than by guessing.

---

## R14 — Single-developer bus factor

**Risk.** The stated goal is a codebase a team maintains for years. Most of the cost of that
is paid by whoever picks it up without the original context.

**Mitigation.** ADRs capture *why*, not just what. Enforced layering means a newcomer cannot
accidentally couple the UI to networking. Host-testable pure modules mean the tricky logic
can be understood and exercised without hardware. A one-command build and a documented
Phase 0 bring-up procedure.
