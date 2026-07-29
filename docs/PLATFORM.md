# Platform Strategy

Deliverables §8–9: flash layout, OTA, configuration.
Networking (§10) is in [NETWORKING.md](NETWORKING.md).
Companion to [ARCHITECTURE.md](ARCHITECTURE.md).

---

## 1. Flash layout (16 MB)

```csv
# Name       Type  SubType   Offset     Size     Notes
nvs          data  nvs       0x9000     0x6000   settings, schema-versioned
nvs_keys     data  nvs_keys  0xF000     0x1000   RESERVED, unused — see below
otadata      data  ota       0x10000    0x2000   dual-copy, wear-levelled
phy_init     data  phy       0x12000    0x1000
ota_0        app   ota_0     0x20000    0x600000 6 MiB
ota_1        app   ota_1     0x620000   0x600000 6 MiB
storage      data  spiffs    0xC20000   0x380000 3.5 MiB — coverart cache, logs
coredump     data  coredump  0xFA0000   0x40000  256 KiB
```

The partition table is the one thing here that is genuinely hard to change later — it is
fixed at flash time, and altering it on a deployed unit means physical access. So it is
sized under the project's reversibility principle (ARCHITECTURE §0): **reserve the option,
don't commit to it.**

- **6 MiB per app slot** is generous on purpose. Embedded fonts and icons are large, and
  running out of app-slot headroom in year two is unrecoverable over the air.
- **`nvs_keys` is reserved, unused in the development profile.** Encrypted NVS is enabled in
  the production profile only, because it is meaningful only alongside flash encryption
  (§3.2, [SECURITY.md](SECURITY.md)). Reserving 4 KiB now means switching profiles is a
  config change rather than a reflash — one sector for a permanent option.

**Fonts and icons are compiled into the app image**, not shipped in the `storage`
partition. A separate asset partition means the app and its assets can be at different
versions after a partial update, and every screen then needs a version-mismatch path.
Atomic updates are worth the flash. See [adr/0005](adr/0005-assets-in-app-image.md).

`storage` (SPIFFS/LittleFS) holds only *derivable* data — the persistent album-art cache and
rotated logs. If it is corrupted we erase and rebuild it; nothing there is authoritative.

---

## 2. OTA strategy

### 2.1 Mechanism

Standard dual-slot A/B with `esp_https_ota` and **rollback enabled**
(`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`).

### 2.2 The health gate — the part that matters

A new image boots into `ESP_OTA_IMG_PENDING_VERIFY`. It is confirmed only after it proves
itself.

**The gate tests the platform, never an application integration.** Firmware validity must
depend only on things this firmware controls. Anything that can fail for reasons outside the
device — a cloud outage, a rate limit, a revoked token, a password change, an AVR that is
unplugged — is *application state*, not evidence that the image is bad. A gate that
conflates the two will roll back perfectly good firmware during a Spotify incident, and
worse, will do it to every unit at once.

`svc_ota` waits for **all** of:

| Gate | Why |
|---|---|
| Boot reached `app_start()` completion | the composition root wired up |
| All tasks created and feeding the watchdog | no startup deadlock |
| Panel initialised, UI rendered ≥ 1 frame, touch enumerated | display path and I²C came up |
| NVS mounted, settings schema read | storage survived the update |
| WiFi associated + IP acquired | a broken netif is the classic bad-flash symptom |
| SNTP synced | proves DNS, UDP, and the netif stack work end to end |
| **One successful TLS handshake to any host** | proves mbedTLS + cert bundle + heap |
| No task-watchdog trip, no panic, no brownout reset | |
| Free heap ≥ 85 % of the recorded watermark | catches an image that boots but leaks |

All gates green within **120 s** → `esp_ota_mark_app_valid_cancel_rollback()`.
Otherwise → deliberate reboot → the bootloader reverts to the previous slot automatically.

Explicitly **not** gates: Spotify token refresh, Spotify API reachability, Denon discovery,
Denon link establishment. All four are surfaced as degraded application state and are
visible in diagnostics, but none can trigger a rollback.

> The TLS handshake gate is what a Spotify token refresh was standing in for — it proves
> mbedTLS, the cert bundle, the clock, and the heap all survived, without inheriting
> Spotify's availability. It targets the OTA host itself, which we already need to reach.

The failure mode this exists to prevent: a wall-mounted device with no console, no USB
access, and no keyboard, running an image that boots but cannot reach the network. Without
the gate, that is a screwdriver-and-ladder recovery.

### 2.3 Manifest

Static JSON on an HTTPS host — no update server to operate:

```json
{
  "version": "1.4.2",
  "build": 142,
  "security_version": 3,
  "url": "https://ota.example.com/dsc-1.4.2.bin",
  "sha256": "…",
  "size": 2894336,
  "sig": "…",
  "key_id": "dsc-dev-2026-07",
  "min_prev_build": 100,
  "mandatory": false,
  "notes": "Faster artwork transitions"
}
```

Verified in order, all mandatory, **in every build profile** — see
[SECURITY.md](SECURITY.md):

1. **Manifest signature** (`sig`, ECDSA P-256) against the trust anchor named by `key_id`.
2. **`security_version` ≥ stored minimum** — monotonic anti-rollback. Bumped only when a
   release fixes something that must not be rolled back past; independent of `version`.
3. **`sha256`** of the downloaded image.
4. **App descriptor** magic and project name.
5. **Image signature** before the slot is marked bootable.

- `min_prev_build` forces a stepping-stone release when a settings migration cannot safely
  skip versions.
- Downgrade within the same `security_version` is supported by pinning an older manifest,
  and is never automatic.
- The development profile verifies against a **dev key committed to this repo**. Verification
  is never stubbed out — a signature path that only runs in production is one that has never
  been tested.

Note the distinction between the checks: steps 1, 2 and 5 are *authenticity* and close the
threats a compromised OTA host presents. Steps 3 and 4 are *integrity* and close the more
likely failure — a corrupt download bricking a device behind a wall plate. Both stay in
scope permanently.

### 2.4 Policy

- Check daily at a **randomised** hour (avoid a thundering herd if this ever ships to more
  than one unit), plus on demand from Settings.
- Apply automatically only when **idle**: not playing, screen asleep, AVR off or on another
  input. A firmware update must never interrupt music.
- `mandatory: true` overrides idle-waiting after a 24 h grace period.
- Before download: `svc_artwork` is suspended and its 600 KiB pool released, and the TLS
  arbiter grants OTA both slots.
- Progress is published on `TOPIC_OTA`; the UI shows it only if the user is on the Settings
  screen, otherwise updates are invisible. Silent, boring updates are a premium feature.
- Downgrade is supported (pin an older version in the manifest) but never automatic.

---

## 3. Configuration strategy

Three tiers, by lifetime and by who owns them.

### 3.1 Compile-time — Kconfig

Board pins, buffer sizes, task priorities, feature flags (`CONFIG_DSC_ENABLE_IMU`,
`CONFIG_DSC_ENABLE_SYSLOG`), poll-interval defaults. Anything a *user* should never see.

### 3.2 Secrets — a separate NVS namespace

Namespace `secure`:

`wifi.ssid` · `wifi.psk` · `spotify.refresh_token` · `spotify.client_id`

Plain NVS in the **development profile**; encrypted NVS in the **production profile**, using
the already-reserved `nvs_keys` partition. Encryption is staged because it only provides real
protection alongside flash encryption, which is an irreversible eFuse burn done at
manufacturing (see [SECURITY.md](SECURITY.md) §7) — enabling it earlier would buy nothing but
a key-management problem and a new way to lose the user's WiFi credentials.

The namespace is a distinct compartment in **both** profiles, because that is what buys the
protections that matter regardless of encryption:

- Only `svc_storage` may open it, so there is one place that touches secrets.
- Secrets are never logged, never rendered, and never included in a diagnostics export —
  `svc_diagnostics` redacts by key prefix. A refresh token pasted into a support log is a
  far more likely leak than someone desoldering the flash chip.
- Factory reset **zeroises** this namespace — overwrites before erase, not merely marks
  free — without touching user settings. Cheap, and it is what makes a resold or RMA'd unit
  safe.

Enabling encryption later is a config change plus a migration, not a partition-table change,
because `nvs_keys` is already reserved (§1).

### 3.3 User settings — versioned NVS schema

Namespace `settings`, accessed only through typed accessors:

```c
esp_err_t settings_get_u8 (settings_h, setting_key_t, uint8_t *out);
esp_err_t settings_set_u8 (settings_h, setting_key_t, uint8_t value);
```

Keys are an enum, not strings at call sites — a typo becomes a compile error.

**Migration.** A `schema_version` key is read at boot and a migration chain runs
`v1→v2→v3…`, each step a small pure function with a host test. Missing keys resolve to
defaults, so adding a setting needs no migration at all; migrations exist only for
*renamed*, *retyped*, or *rescaled* keys. Rolling back to an older firmware with a newer
schema is detected and handled by falling back to defaults rather than misreading data.

**Write coalescing — a 24/7 requirement.** Dragging a brightness slider generates hundreds
of value changes per second. Writing each one to NVS would destroy the flash sector within
months. So:

- Setting writes land in a RAM shadow copy immediately (readers see them at once).
- The shadow is flushed to NVS after **2 s of quiescence**, or after 30 s of continuous
  change, or immediately on a shutdown/OTA hook.
- `svc_diagnostics` counts lifetime NVS writes; the number is visible on the diagnostics
  screen and reviewed during soak testing.

### 3.4 Settings inventory

| Setting | Type | Default |
|---|---|---|
| `brightness_active` | u8 0–100 | 85 |
| `brightness_dim` | u8 0–100 | 15 |
| `dim_timeout_s` | u16 | 60 |
| `sleep_timeout_s` | u16 | 300 |
| `night_mode_enabled` / `night_start` / `night_end` | bool / hh:mm | off |
| `wake_on_playback` | bool | true |
| `denon_host` | str | (discovered) |
| `denon_transport_pref` | enum telnet\|http\|auto | auto |
| `volume_step` | u8 | 1 (0.5 dB) |
| `volume_limit_max` | u8 | 70 (**safety**) |
| `default_screen` | enum | now_playing |
| `ota_channel` | enum stable\|beta | stable |
| `ota_auto_apply` | bool | true |
| `schema_version` | u16 | — |

`volume_limit_max` is not a nicety. A stray touch that sends `MV98` to an AVR-X3800H
driving real speakers damages hardware and hearing. The clamp is enforced in `svc_denon` at
the protocol boundary, **not** in the UI — so it holds regardless of which screen, gesture,
or future integration issues the command.

### 3.5 Factory reset

Long-press in Settings, with confirmation: erase both NVS namespaces, erase the SPIFFS
cache, reboot into provisioning. The OTA slot is left alone — a factory reset should not
also downgrade firmware.

---

## 4. Networking strategy

Split into its own document — see **[NETWORKING.md](NETWORKING.md)**, which covers the
connectivity FSM, two-stage provisioning, the Spotify session and polling policy, the Denon
dual transport, and HTTP client policy.
