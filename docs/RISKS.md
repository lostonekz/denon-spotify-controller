# Risk Register

Ordered by expected cost = likelihood × damage × how late we'd find out.
Anything marked **Phase 0** must be answered before the architecture is frozen.

---

## R1 — PSRAM bandwidth contention with the RGB panel · **Phase 0 — ACCEPTED**

**Risk.** The ST7701 sits on the parallel RGB bus, so panel DMA reads the framebuffer out of
PSRAM continuously, competing with WiFi DMA, TLS, and JPEG decode for the same bus. If it
tears under real load, the memory plan has to change before the UI is built, not after.

**Measured, under load that genuinely transferred data:** 6.7–7.1% dropped frames, against
an accepted 10% budget. An earlier pass measured ~1.5% and was **wrong** — every run behind
it had `bytes_downloaded == 0`, so the contention this risk is named for was never exercised.

**Current settings:** 1 framebuffer, 30 Hz, 8 MHz pclk, 20-row bounce buffers,
`LV_DISPLAY_RENDER_MODE_PARTIAL` with a 40-row draw buffer. Two of those reverse Phase 0's
own conclusions — the framebuffer count and the refresh rate were both revisited once the
real application existed, and the refresh rate twice.

**Accepted**, but note what that acceptance rests on: the dominant real-world symptom turned
out not to be bandwidth at all, but a periodic diagnostics scan that froze the scheduler.

→ **Full evidence, every fallback tried, and the nine root causes of bring-up:
[DISPLAY_PIPELINE.md](DISPLAY_PIPELINE.md).**

---

## R18 — The pairing page is readable by anything on the LAN

**Risk.** While the pair overlay is open, `main/pair_http.c` serves the Spotify authorize
URL — including the live `state` nonce — to any host on the network. A LAN neighbour can
consent with *their own* account and POST a valid `code`+`state` first, binding this panel
to the attacker's Spotify. PKCE still protects the other direction.

**Status: accepted, not mitigated.** Bounded to the seconds the overlay is open with someone
standing at the panel; the owner declined a PIN in favour of a shorter flow, understanding
the trade. [adr/0011](adr/0011-on-device-pair-page-over-console-paste.md) has the reasoning;
[PRODUCTION_DEFERRED.md](PRODUCTION_DEFERRED.md) §8 lists a panel-displayed PIN as the first
thing to add before a unit leaves a personal wall. Restricting the listener to the STA
interface was not available — `esp_http_server` binds `INADDR_ANY`.

Recorded because R4's own mitigation created it: a register that lists only the risks a
feature *solves* is not a register.

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
recovery without a factory reset or a serial console. **Built** (Phase 6,
[adr/0011](adr/0011-on-device-pair-page-over-console-paste.md)): the System screen's Spotify
row opens a pair overlay, and the QR points at a page the device serves itself.

**New exposure this mitigation created — see R18.**

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
that allocate continuously and are not ours to control; free-heap slope sampled every 60 s and
reported as a fault; a debug-build heap hook that traps unexpected `malloc`; a mandatory
**7-day soak test** before any release, with watermarks recorded.

**Weakened, and not re-mitigated.** This used to lean on "LVGL is confined to its own pool,
so its fragmentation cannot starve networking". No longer true — `lv_mem_add_pool()` was
abandoned after failing on real hardware, and LVGL now allocates from the **shared** heap
([MEMORY.md](MEMORY.md) §1/§3). `lv_mem_monitor()`, the instrument named above, is
meaningless under the C-library allocator. The 7-day soak (Phase 7, not yet run) is now the
only thing that would catch this.

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
(~590 lines across `hal_display.c` and `ui_app.c`, grown from this risk's original
~200-line estimate) and concentrated in two files, so the blast radius of an LVGL change is
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

**Mitigation.** Hard no-blocking rule; task watchdog on every task (a stall becomes a reboot
with a coredump, strictly better than a frozen panel); a debug assertion on loop duration;
the review checklist covers it. That assertion warns late — its threshold had to rise to
600 ms for artwork scaling (~285 ms, R1) — so `ui_diag_state_t.max_loop_us` also reports peak
loop duration per window. Measured: **~24 ms idle, 48–68 ms dragging**, frame rate unaffected.

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
Night dimming would reduce the largest heat source (**not built** — `hal_backlight` is
on/off only, so there is no level to dim *to*). `svc_power` now exists and does schedule the
backlight, but only to turn it fully off after `CONFIG_DSC_SCREEN_SLEEP_AFTER_MIN` of
inactivity (default 30). That is a real and probably larger reduction in the same heat source
than dimming would have been — an idle wall-mounted panel is dark rather than dimmed — but it
is driven by inactivity, not by the clock, so it does nothing for a panel being actively
watched at midnight. A night schedule still needs the PWM path.

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
