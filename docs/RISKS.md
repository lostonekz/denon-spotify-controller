# Risk Register

Ordered by expected cost = likelihood × damage × how late we'd find out.
Anything marked **Phase 0** must be answered before the architecture is frozen.

---

## R1 — PSRAM bandwidth contention with the RGB panel · **Phase 0**

**Risk.** The ST7701 is on the parallel RGB bus, so panel DMA reads the framebuffer out of
PSRAM continuously (~27 MB/s at 480×480×16bpp×60 Hz). WiFi DMA, TLS working memory, and
JPEG decode compete for the same bus. Symptoms are visible tearing, horizontal glitch
lines, or a permanently offset image — and they appear *only* under combined load, which
means they appear late, in front of the user, and not on the bench.

**Why it's #1.** Every other risk here is recoverable in software. This one can invalidate
the memory plan, the frame rate target, and the artwork pipeline simultaneously.

**Mitigation.** Bounce buffers in internal SRAM; `SPIRAM_FETCH_INSTRUCTIONS`/`RODATA` off;
octal PSRAM at ≥ 80 MHz confirmed at bring-up; LCD ISR in IRAM; artwork decode at low
priority on core 0.

**Proof required in Phase 0:** a spike that renders a moving gradient at 60 fps while
simultaneously (a) pulling a 5 MB HTTPS download, (b) decoding a 640×640 JPEG in a loop,
and (c) writing NVS. Instrument dropped frames. If it tears, we find out in week one, when
dropping to a single framebuffer, a lower refresh, or partial-refresh mode is still cheap.

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

## R15 — Anti-rollback is one-way in production

**Risk.** `security_version` is monotonic and, in the production profile, backed by an eFuse
counter that cannot be rewound. Bumping it casually — or bumping it in a release that later
turns out to be broken — permanently blocks downgrade to the last known-good firmware.

**Mitigation.** `security_version` is bumped **only** for a release that fixes something
which must not be rolled back past, never as part of routine versioning. It has a dedicated
release-checklist item requiring explicit justification. In the development profile it is
enforced from NVS only, so it stays recoverable while the discipline is being established.

---

## R16 — Signing key loss or compromise

**Risk.** The release key signs firmware for devices reachable only over the air. Loss ends
updateability; compromise allows hostile firmware.

**Mitigation.** Two trust anchors (`current` + `next`) shipped in every image, so rotation
is possible without physical access as long as one valid key survives
([SECURITY.md](SECURITY.md) §4). The dev key is committed to the repo deliberately and
protects nothing, which keeps the verification path exercised on every build. The release
key is backed up independently of the source tree, with the location recorded in project
operational notes rather than in one person's memory — see R14.

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
