# Display Pipeline — bandwidth, framebuffers, and what bring-up actually cost

Split out of [RISKS.md](RISKS.md), where this was risk **R1** and had grown to nearly half
the register. It is kept whole because it is the most expensive knowledge in this repo: the
measurements behind every framebuffer/refresh-rate decision, and the nine independently
confirmed root causes between "nothing on screen" and a stable picture.

**R1 itself still lives in [RISKS.md](RISKS.md)** as the register entry; this file is its
evidence. Anything citing "RISKS.md R1" for detail means this document.

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
  open, and note those figures were measured against the **2012 ROM** TJpgDec: the build
  later switched to `esp_jpeg`'s vendored 2021 decoder (`CONFIG_JD_USE_ROM=n`) and moved its
  work buffer to PSRAM. Nothing has been re-measured since.)
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
