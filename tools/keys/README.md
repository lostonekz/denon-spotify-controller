# Signing keys

Real key material is never committed here. See [docs/SECURITY.md](../../docs/SECURITY.md) §4.

## Development key

The default profile expects `dev_ota_signing.pem` in this directory
(referenced by `sdkconfig.defaults`). It protects nothing — its only job is to
keep the signature-verification path exercised on every developer build so it
cannot silently rot before a production key ever exists.

Generate one locally:

```bash
espsecure.py generate_signing_key --version 2 --scheme rsa3072 tools/keys/dev_ota_signing.pem
# or, equivalently, since the dev key protects nothing:
openssl genrsa -out tools/keys/dev_ota_signing.pem 3072
```

**RSA-3072, not ECDSA.** ESP-IDF's signed-apps scheme is chip-dependent: plain
ECDSA only exists on Secure Boot V1 (which ESP32-S3 does not support at all),
and ECDSA-V2 is ESP32-C2-only. RSA-3072 is the only scheme this board has. An
earlier draft assumed ECDSA and generated a P-256 key here — Kconfig silently
dropped the unsatisfiable `ECDSA_SCHEME` symbol and fell back to its RSA
default, while the key on disk was EC, so the device aborted on every boot
(`check_signature_on_update_check`: "RSA signing scheme selected but
signature block generated for ECDSA scheme"). Caught on first hardware boot.

CI generates a fresh, disposable one on every run (`.github/workflows/ci.yml`) —
there is nothing to keep in sync locally.

`.gitignore` excludes `*.pem`/`*.der` in this directory. `tools/check_release.py`
fails a **production** build that contains dev key material — see gate 4 in
[docs/ARCHITECTURE.md](../../docs/ARCHITECTURE.md) §1.4.

## Release key

Generated offline, never placed in this directory, never committed. Backup
location is recorded in project operational notes, not in source control —
see [SECURITY.md](../../docs/SECURITY.md) §4 on why (key loss on an OTA-only
device is close to fatal).
