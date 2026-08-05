# Security Architecture

Two profiles, one codebase. The security machinery is **designed and built now**, and
**enabled in stages**. Companion to [ARCHITECTURE.md](ARCHITECTURE.md) and
[adr/0006](adr/0006-staged-security.md).

---

## 1. The governing rule

> **Every security mechanism runs in every build. Only its trust anchor and its enforcement
> severity change between profiles.**

This is the whole design, and it exists because of one failure mode: a security path that
only executes in production is a security path that has never been tested. It rots. It gets
broken by an unrelated refactor in month four and nobody notices until the first production
build, which is the worst possible time to discover that signature verification was silently
returning `ESP_OK`.

So signature verification, anti-rollback comparison, and credential redaction are **always
compiled in and always executed** — including on a developer's desk. What differs:

| | Development profile (default) | Production profile |
|---|---|---|
| Image signature | verified against a **dev key generated locally, gitignored** (§4) | verified against a release key held offline |
| Verification failure | **hard fail** — image rejected | hard fail |
| Anti-rollback | enforced in software (NVS counter) | enforced in software **and** eFuse secure version |
| Secure Boot v2 | off | burned at manufacturing |
| Flash encryption | off | burned at manufacturing |
| NVS secrets | plain, separate namespace | encrypted NVS (`nvs_keys` already reserved) |
| eFuse operations | **never** | manufacturing station only |
| Log redaction | on | on |

The development profile is therefore not "security off". It is the same code with a key you
are allowed to lose. That distinction is what makes the production switch a config change
rather than a project.

---

## 2. Threat model

Stated explicitly, because an unstated threat model is how scope creeps in both directions.

### In scope

| # | Threat | Mitigation | Enabled in dev? |
|---|---|---|---|
| T1 | Compromised or spoofed OTA host pushes hostile firmware | signed images, signed manifest | **yes** |
| T2 | Downgrade to a known-broken firmware | monotonic `security_version`, anti-rollback | **yes** |
| T3 | Corrupt or truncated download bricks a wall-mounted unit | SHA-256 + header validation + health gate | yes |
| T4 | Credentials leaked through logs, diagnostics export, or a support paste | redaction by key prefix, secrets never rendered | yes |
| T5 | Token or credential interception during setup on a shared LAN | PKCE + single-use `state`; the pairing listener exists only while the pair overlay is open, and the `redirect_uri` still has nothing listening on it ([adr/0011](adr/0011-on-device-pair-page-over-console-paste.md)). Partial: a LAN host can read the authorize URL during that window — see §5 | partial |
| T6 | Physical access to a resold, returned, or discarded unit | flash encryption + zeroising factory reset | production only |
| T7 | Attacker on the local network reads or replays control traffic | — accepted, see below | — |

**T1 and T2 are enabled from day one** even in the personal build. They are pure software,
they are reversible, and they are the two the reviewer was right to call Critical. The
earlier decision to skip signing was wrong on its own terms: the key-management hazard it
avoided is better solved by *rotation support* (§4) than by having no key at all.

### Out of scope, deliberately

- **T7 — LAN traffic interception.** Denon control is unauthenticated ASCII over telnet by
  the AVR's design. We cannot fix that, and encrypting our half would achieve nothing.
- **Denial of service.** Someone on your LAN can hold the Denon telnet socket. That is
  already handled as a *reliability* concern ([adr/0003](adr/0003-denon-dual-transport.md)),
  not a security one.
- **Supply-chain attestation of the ESP-IDF toolchain.** Pinned versions and a lockfile;
  no reproducible-build guarantee.

---

## 3. OTA trust chain

```
  manifest.json  ──TLS──►  fetched
        │
        ├── sig  (ECDSA P-256 over the canonical manifest body)
        │        verified against the trust anchor
        ▼
  image .bin  ──TLS──►  streamed to inactive slot
        │
        ├── sha256          matches manifest
        ├── app descriptor  magic + project name match
        ├── security_version ≥ stored minimum      ◄── anti-rollback (T2)
        └── image signature  verified before boot  ◄── authenticity (T1)
        ▼
  reboot ──► health gate (PLATFORM §2.2) ──► confirm or roll back
```

Two independent signatures, two independent schemes — worth not conflating:

- **Manifest signature** — our own application code (Phase 2, `net_http`/mbedTLS),
  free to pick any scheme. ECDSA P-256 here is a real choice, not a constraint.
- **Image signature** — ESP-IDF's own bootloader-enforced verification
  (`esp_app_format`), and **not** a free choice: its scheme is fixed by the
  chip. See [adr/0006](adr/0006-staged-security.md) and
  [tools/keys/README.md](../tools/keys/README.md) — ESP32-S3 only supports
  RSA-3072 for "signed apps, no secure boot," which is why the dev/release
  signing key is RSA-3072, not ECDSA.

Manifest gains three fields over the earlier draft:

```json
{
  "version": "1.4.2",
  "build": 142,
  "security_version": 3,
  "url": "https://ota.example.com/dsc-1.4.2.bin",
  "sha256": "…",
  "sig": "…",
  "key_id": "dsc-dev-2026-07"
}
```

- **`security_version`** is monotonic and increments **only** when a release fixes something
  that must not be rolled back past. It is independent of the marketing version. The device
  stores the highest value it has ever run and refuses anything lower — in dev, from NVS; in
  production, additionally from the eFuse secure version counter, which cannot be rewound.
- **`key_id`** names the trust anchor, which is what makes rotation possible without a
  flag day (§4).
- **Downgrade** remains possible within the same `security_version`, and is never automatic.

---

## 4. Key management

The reason ADR-0006 originally rejected signing was that losing the key permanently ends
OTA capability on a wall-mounted device — a real hazard, and one that lands squarely on
RISKS R14 (single-developer bus factor). Rotation support is the actual answer:

- The firmware holds **two trust anchors**: `current` and `next`. Either verifies.
- Rotating: ship a release signed with `current` that adds the new key as `next` → wait for
  fleet convergence → ship a release signed with `next` → the old key is retired in the
  release after that.
- This means a compromised *or lost* key is recoverable as long as **one** valid key
  survives, rather than requiring physical access to every unit.
- Dev key lives at `tools/keys/dev_ota_signing.pem` (path set in `sdkconfig.defaults`),
  generated locally per `tools/keys/README.md` -- or fresh by CI on every run -- rather than
  committed; `tools/keys/*.pem` is gitignored like any other private key. It signs nothing
  that matters. What keeps the verification path exercised isn't the key sitting in the
  repo, it's `tools/check_release.py`'s dev-profile gate: signing must be on in every
  profile, so a dev build with no key present fails the gate instead of silently skipping
  verification.
- Release key lives offline, backed up independently of the source tree, with the backup
  location recorded in the project's operational notes — not in one person's memory.

---

## 5. Setup-session hardening

The device deliberately opens a listener in exactly two situations, both bounded and both
user-initiated: WiFi provisioning ([NETWORKING.md §1.2](NETWORKING.md)), and Spotify pairing
while the pair overlay is open on the panel ([adr/0011](adr/0011-on-device-pair-page-over-console-paste.md),
which supersedes [adr/0009](adr/0009-manual-code-paste-over-bounce-page.md)).

- **SoftAP stage** — WPA2 with a per-device password derived from the MAC and shown on
  screen. Not an open AP. The portal is reachable only while provisioning is active.
- **OAuth redirect — still no listener.** The `redirect_uri` registered with Spotify remains a
  loopback address (`http://127.0.0.1:<port>/callback`) that nothing listens on, on any
  device. The redirect only ever has to reach the phone browser's own address bar. This part
  of adr/0009 is unchanged and is what makes the flow work at all.
- **Pairing page — a bounded listener, by design.** The panel serves
  `http://<device>:<port>/pair` so a phone can scan a QR and paste the failed redirect URL
  back without a serial console. It runs only while the pair overlay is open on the panel,
  started and stopped by that screen's own `mount()`/`unmount()` — not at boot, not while
  idle. It is a different URL on a different port from the `redirect_uri` above and is never
  a redirect target.
  **Known, accepted exposure:** while it is up, any host on the LAN can fetch that page and
  read the authorize URL, including the live `state` nonce, and could complete consent with
  its own Spotify account and POST a valid `code`+`state` before the legitimate user does —
  pairing this panel to the attacker's account. PKCE still prevents the reverse (nobody can
  steal or replay *our* pairing). A PIN shown on the panel would close this and was declined
  in favour of a shorter flow; see adr/0011's Consequences for the full reasoning.
- **CSRF** — the OAuth `state` parameter is a 128-bit-class random value generated per pairing
  attempt, held in RAM only, and validated before any PKCE exchange is attempted — a mismatch
  is rejected outright with no exchange. The nonce is cleared on both terminal transitions
  (`action_enter_authorized_first_time` / `action_report_exchange_failed`), so it is genuinely
  single-use. It previously was not: it had no expiry and survived success, failure and
  unpair alike, which adr/0011 fixed as part of opening the LAN path. PKCE `code_verifier`
  likewise never leaves the device.
- **No listener in normal operation.** Outside those two windows the device runs no inbound
  service at all.

**Flagged for a human read before this ships**: this section describes a real change to the
device's threat surface — a LAN-reachable pairing page bounded to one screen's lifetime,
replacing a design that required physical/USB access — not just a wording fix. It deserves a
deliberate second look, not only an automated one.

---

## 6. Credential handling

- Single namespace `secure`, opened only by `svc_storage`.
- **Redaction is structural**: `svc_diagnostics` filters by key prefix, and the log ring
  drops any string that matches a registered secret value before it is written. A token
  cannot reach a support export even if someone logs it by accident — which is the realistic
  leak (T4), far more than flash extraction.
- **Factory reset zeroises**: the `secure` namespace is overwritten before erase, not merely
  marked free. Cheap, and it is what makes a resold or RMA'd unit safe (T6).
- Production adds encrypted NVS on top, using the already-reserved `nvs_keys` partition.

---

## 7. Manufacturing and field operations — parked

This device is a personal dev/test unit for the time being, not a product in progress. The
manufacturing/field-ops procedure and the `factory_test` component design that used to sit
here as §7/§7b are parked, in full, in
[PRODUCTION_DEFERRED.md](PRODUCTION_DEFERRED.md). Nothing was decided against; it moves back
here if that changes.

---

## 7c. Diagnostics export schema and consent

What may leave the device, defined as a schema rather than as a judgement call:

| Field class | Example | In export |
|---|---|---|
| Identity | firmware version, build, board rev, uptime | yes |
| Health | heap watermarks, stack high-water, pool utilisation, FPS, temperature | yes |
| Counters | reconnects, API error counts by class, event drops, NVS writes | yes |
| Fault records | task, PC, reason, uptime at crash (last 8) | yes |
| Network shape | RSSI, channel, whether DHCP | yes |
| **Identifiers** | MAC, SSID, Denon IP/serial, Spotify user ID | **no** |
| **Content** | track/artist/album names, artwork URLs | **no** |
| **Credentials** | anything from the `secure` namespace | **no** |
| **Raw memory** | coredump body | **no** — see [LIFECYCLE.md](LIFECYCLE.md) §2.1 |

The export is an allowlist, not a denylist — a new field is excluded until someone
deliberately adds it. Denylists fail open, which is the wrong direction for this.

Consent: export is always a deliberate user action from the diagnostics screen, shows a
preview of what will be included, and is never automatic or background.

## 8. What is deliberately not decided yet — parked

The list of production-run open questions (device certificates, Secure Boot v2's recovery
cost, staged-rollout OTA hosting, export privacy at fleet scale) is parked in
[PRODUCTION_DEFERRED.md](PRODUCTION_DEFERRED.md) along with everything else in this file's
old §7/§7b. None of it blocks Phase 0–7 — that was true before and stays true now.
