# Production — Deferred

**Status (2026-08-01): parked at the user's explicit request.** This device is, for the time
being, **only a personal dev/test unit** — there is no current plan to manufacture, sell, or
field more than one. Nothing below is cancelled or decided against; it's relocated out of the
active roadmap, risk register, and security doc so it stops shaping day-to-day decisions on a
project that isn't building toward it right now. If that changes, this content moves back to
its original home (each section below says where it came from) and picks up from here — it is
not being rewritten from scratch at that point.

This does **not** touch [adr/0006](adr/0006-staged-security.md) or the dev-profile security
machinery described in [SECURITY.md](SECURITY.md) §1–6 (signed images, anti-rollback via NVS,
credential redaction, setup-session hardening). Those are real, already-built, already-active
mechanisms that run on the dev profile today — staging them was already the decision ADR-0006
made, and this doesn't reverse it. What's parked here is specifically the *unbuilt* work aimed
at manufacturing, fielding, and supporting units beyond this one bench unit.

---

## From ROADMAP.md — Phase 7b and Phase 8

### Phase 7b — Production profile dry run (one sacrificial unit)

Scheduled, and **before** any product decision. The point of building security in stages is
lost if the production profile is first exercised on the day it must work.

- Build the production profile with a throwaway release key.
- Verify the production build gate rejects: dev anchor present, assertions on, console on.
- Flash one **sacrificial** unit, enable encrypted NVS, burn Secure Boot v2 and flash
  encryption. Expect to lose this board; that is what it is for.
- Confirm signed OTA still works on a secure-booted unit, that rotation works, and that
  anti-rollback holds against the eFuse counter.
- Time and document the manufacturing sequence.

**Exit criteria:** a written procedure and a known cost, rather than an assumption. If the
production profile turns out not to work, this is the cheapest possible moment to find out —
one board, no customers.

### Phase 8 — Production readiness (only if it becomes a product)

Not scheduled. Listed so the trigger conditions are written down rather than rediscovered.

- Swap the dev trust anchor for an offline release key; verify rotation works on real units
- Enable encrypted NVS (`nvs_keys` already reserved)
- Decide Secure Boot v2 and flash encryption against a manufacturing threat model
- Factory test fixture and pass/fail criteria ([SECURITY.md](SECURITY.md) §7)
- eFuse burn station — **last step, after factory test passes**, since it is the step that
  makes a failed unit unrecoverable
- RMA flow: redacted diagnostics export, zeroising factory reset
- Staged-rollout OTA hosting, if fleet size justifies it

Everything here is a config change or a procedure. None of it is a redesign — which is the
entire return on doing [adr/0006](adr/0006-staged-security.md) now rather than later.

---

## From RISKS.md — R15 and R16

### R15 — Anti-rollback is one-way in production

**Risk.** `security_version` is monotonic and, in the production profile, backed by an eFuse
counter that cannot be rewound. Bumping it casually — or bumping it in a release that later
turns out to be broken — permanently blocks downgrade to the last known-good firmware.

**Mitigation.** `security_version` is bumped **only** for a release that fixes something
which must not be rolled back past, never as part of routine versioning. It has a dedicated
release-checklist item requiring explicit justification. In the development profile it is
enforced from NVS only, so it stays recoverable while the discipline is being established.

*Why this is parked, not just quiet:* the eFuse counter this risk is about doesn't exist on
the dev profile at all — nothing here is currently exposed. It matters again the day
`Secure Boot v2`/flash encryption get burned on a real unit (Phase 7b/8 above).

### R16 — Signing key loss or compromise

**Risk.** The release key signs firmware for devices reachable only over the air. Loss ends
updateability; compromise allows hostile firmware.

**Mitigation.** Two trust anchors (`current` + `next`) shipped in every image, so rotation
is possible without physical access as long as one valid key survives
([SECURITY.md](SECURITY.md) §4). The dev key is committed to the repo deliberately and
protects nothing, which keeps the verification path exercised on every build. The release
key is backed up independently of the source tree, with the location recorded in project
operational notes rather than in one person's memory — see RISKS.md R14.

*Why this is parked, not just quiet:* there is no release key yet — only the disposable dev
key. This risk is about a key that doesn't exist until Phase 8 creates one.

---

## From SECURITY.md — §7, §7b, §8

### §7. Manufacturing and field operations

Sketched now so the production profile is a procedure rather than a research project. Not
built until it is needed.

| Stage | Actions |
|---|---|
| **Flash** | partition table, dev or release image, per-unit ID written to NVS |
| **Factory test** | panel + touch + backlight sweep, WiFi scan + associate, PSRAM/flash integrity, IMU present, temperature sane. Pass/fail on screen; result stored. |
| **Secure enable** (production only) | burn Secure Boot v2 + flash encryption eFuses. **Point of no return** — after this the unit cannot be reflashed unsigned. Runs last, after factory test passes. |
| **Field** | OTA only. No inbound services. Diagnostics screen behind a long-press. |
| **Factory reset** | zeroise `secure`, erase `settings`, erase SPIFFS cache, reboot to provisioning. Firmware slot untouched. |
| **RMA** | coredump + diagnostics export (redacted) retrievable over USB or the local network before reset. |

The ordering matters: **eFuse burn is the last step and only after the unit has passed
every other test**, because it is the step that makes a failed unit unrecoverable.

### §7b. Factory test is a separate component

`factory_test` is its own component with its own entry point, **not** a mode bolted into the
application. Manufacturing logic embedded in app code is how test hooks end up in shipped
firmware.

- Built only under `CONFIG_DSC_FACTORY_TEST`, which the production build gate
  (ARCHITECTURE §1.4, gate 4) fails on for a shipping profile.
- It links `bsp_board` and `hal_*` directly and does **not** link `ui`, `svc_spotify`, or
  `svc_denon` — it is a hardware exerciser, not the product.
- Results are written to a dedicated NVS namespace and displayed as pass/fail with a fault
  code. The eFuse burn station reads that namespace and **refuses to burn a unit that has
  not passed**, which is what makes the ordering in §7 enforceable rather than procedural.

### §8. What is deliberately not decided yet

These need a real answer before a production run, and none of them block Phase 0–7:

- Whether units get individual device certificates, or share a firmware trust anchor only.
- Whether Secure Boot v2 is worth its recovery cost at the intended volume.
- Whether OTA hosting moves from static files to a server that can do staged rollout.
- Privacy: what a diagnostics export may contain if it ever leaves the user's network.
- **A PIN on the pairing page.** [adr/0011](adr/0011-on-device-pair-page-over-console-paste.md)
  records an accepted exposure: while the pair overlay is open, any LAN host can read the
  authorize URL including the live `state` nonce, and could pair this panel to its own Spotify
  account by winning a race against the user. The owner of this device declined a PIN in
  favour of a shorter flow, understanding the trade. **That decision does not survive shipping
  to anyone else** — a 4-digit code rendered on the panel and required as a second form field
  closes it outright, and is the first thing to add before a unit leaves this house.
- **A `PAIR_CANCELLED` event for `spotify_session_fsm`.** Consenting on the phone and then
  closing the pair overlay before pasting leaves the session in `AWAITING_CODE` until a reboot
  or a fresh pairing. The old console flow had the same hole, but the overlay makes it far
  easier to hit. Fixing it properly needs the new event plus a `RESUMING` re-entry so an
  abandoned re-pair can return to a working session — real new FSM surface, host-testable
  against the existing suite, and more than the pairing feature itself warranted.

Under ARCHITECTURE §0.1 each of these stays open because the partition table reserves the
space, the manifest carries the fields, and the verification path already runs.
