# Signing keys

Real key material is never committed here. See [docs/SECURITY.md](../../docs/SECURITY.md) §4.

## Development key

The default profile expects `dev_ota_signing.pem` in this directory
(referenced by `sdkconfig.defaults`). It protects nothing — its only job is to
keep the signature-verification path exercised on every developer build so it
cannot silently rot before a production key ever exists.

Generate one locally:

```bash
espsecure.py generate_signing_key --version 2 tools/keys/dev_ota_signing.pem
# or, equivalently, since the dev key is P-256 and protects nothing:
openssl ecparam -name prime256v1 -genkey -noout -out tools/keys/dev_ota_signing.pem
```

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
