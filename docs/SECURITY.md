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
| Image signature | verified against a **dev key committed to this repo** | verified against a release key held offline |
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
| T5 | Token or credential interception during setup on a shared LAN | PKCE + `state`, short-lived listener, pairing PIN | yes |
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
- Dev key lives in `tools/keys/dev_ota.pem`, committed deliberately. It signs nothing that
  matters, and its presence in the repo is what keeps the verification path exercised.
- Release key lives offline, backed up independently of the source tree, with the backup
  location recorded in the project's operational notes — not in one person's memory.

---

## 5. Setup-session hardening

The provisioning and OAuth flow ([NETWORKING.md §1.2](NETWORKING.md)) is the one moment the
device deliberately opens a listener. It is hardened regardless of profile:

- **SoftAP stage** — WPA2 with a per-device password derived from the MAC and shown on
  screen. Not an open AP. The portal is reachable only while provisioning is active.
- **OAuth stage** — the local HTTP listener binds only for the pairing window, times out
  after **5 minutes**, and closes on first successful exchange.
- **CSRF** — the OAuth `state` parameter is a 128-bit random value generated per attempt,
  compared on the callback, and single-use. PKCE `code_verifier` likewise.
- **Pairing PIN** — the on-screen QR embeds a nonce the device also displays; the callback
  is rejected without it. Prevents another host on the LAN racing the redirect.
- **No listener outside pairing.** The device runs no inbound service in normal operation.

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

## 7. Manufacturing and field operations

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

---

## 7b. Factory test is a separate component

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

## 8. What is deliberately not decided yet

These need a real answer before a production run, and none of them block Phase 0–7:

- Whether units get individual device certificates, or share a firmware trust anchor only.
- Whether Secure Boot v2 is worth its recovery cost at the intended volume.
- Whether OTA hosting moves from static files to a server that can do staged rollout.
- Privacy: what a diagnostics export may contain if it ever leaves the user's network.

Under ARCHITECTURE §0.1 each of these stays open because the partition table reserves the
space, the manifest carries the fields, and the verification path already runs.
