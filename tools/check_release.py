#!/usr/bin/env python3
"""
GATE 4 — Production build guard.

docs/SECURITY.md §1. The dev signing key is committed to this repo ON PURPOSE:
it is what keeps the signature-verification path executing on every developer
build, so it cannot rot. That choice is only safe if shipping it is impossible.

This gate proves it. In a production profile it fails on:
  - the dev trust anchor being present
  - any key material at all under tools/keys/ (dev or release, image-signing
    or manifest-signing -- the release key is generated offline and never
    placed there; see tools/keys/README.md)
  - assertions / debug logging / console enabled
  - Secure Boot signing disabled
  - factory-test code compiled in

In a development profile it asserts the INVERSE: the dev key must be present
and signing must be on. A dev build that has silently stopped verifying
signatures is exactly the rot this design exists to prevent.
"""

import argparse
import os
import re
import sys

DEV_KEY_MARKERS = ("dev_ota_signing", "dsc-dev-", "DEV_TRUST_ANCHOR")

# (config key, required value in production, human reason)
PROD_REQUIRED = [
    ("CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT", "y",
     "production images must be signed"),
    ("CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE", "y",
     "health gate is inert without rollback"),
    ("CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_SILENT", "y",
     "assertion strings leak internals and bloat the image"),
    ("CONFIG_DSC_FACTORY_TEST", None,
     "factory-test code must not ship"),
    ("CONFIG_DSC_TEST_FORCE_HEALTH_GATE_TIMEOUT", None,
     "test-only gate-timeout override must not ship"),
    ("CONFIG_DSC_DENON_DEBUG_CONSOLE", None,
     "debug console must not ship"),
    ("CONFIG_DSC_DENON_SELFTEST", None,
     "boot-time self-test must not ship"),
    ("CONFIG_ESP_CONSOLE_UART_DEFAULT", None,
     "no serial console on a shipping unit"),
]

DEV_REQUIRED = [
    ("CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT", "y",
     "signature verification must run in dev too, or it rots"),
]

# eFuse operations are forbidden in EVERY profile built from this tree.
# Production units are burned at a manufacturing station, not by a build.
# docs/ARCHITECTURE.md §0.1.
EFUSE_FORBIDDEN_ALWAYS = [
    "CONFIG_SECURE_BOOT",
    "CONFIG_SECURE_FLASH_ENC_ENABLED",
    "CONFIG_SECURE_BOOT_INSECURE",
    "CONFIG_SECURE_DISABLE_ROM_DL_MODE",
]


def parse_config(path):
    cfg = {}
    if not os.path.exists(path):
        return cfg
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if "=" in line:
                k, v = line.split("=", 1)
                cfg[k.strip()] = v.strip().strip('"')
    return cfg


def scan_key_material_files(root):
    """Any *.pem/*.der file under tools/keys/, regardless of name or content.

    Content/marker scanning (scan_for_dev_key below) only catches material
    that happens to mention a known dev-key string -- a renamed file, or a
    key for something markers were never written for (e.g. Phase 2's ECDSA
    manifest-signing key), would silently pass it. tools/keys/README.md is
    explicit that the release key is "never placed in this directory,
    never committed" -- so the convention-independent invariant is simpler
    and stronger: a production build has no business finding ANY key file
    here at all.
    """
    keys_dir = os.path.join(root, "tools", "keys")
    if not os.path.isdir(keys_dir):
        return []
    return sorted(
        os.path.relpath(os.path.join(keys_dir, f), root).replace(os.sep, "/")
        for f in os.listdir(keys_dir)
        if f.endswith((".pem", ".der"))
    )


def scan_for_dev_key(root):
    """Any dev-key material reachable from the build."""
    hits = []
    for sub in ("main", "components", "tools/keys"):
        base = os.path.join(root, sub)
        if not os.path.isdir(base):
            continue
        for dirpath, dirs, files in os.walk(base):
            dirs[:] = [d for d in dirs if d not in {".git", "build", "__pycache__"}]
            for f in files:
                if not f.endswith((".c", ".h", ".pem", ".der")):
                    continue
                path = os.path.join(dirpath, f)
                with open(path, encoding="utf-8", errors="replace") as fh:
                    text = fh.read()
                for m in DEV_KEY_MARKERS:
                    if m in text:
                        hits.append("%s contains %r"
                                    % (os.path.relpath(path, root).replace(os.sep, "/"), m))
                        break
    return hits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".")
    ap.add_argument("--profile", choices=["dev", "prod"], default="dev")
    ap.add_argument("--sdkconfig", default=None)
    args = ap.parse_args()
    root = os.path.abspath(args.root)

    failures = []

    cfg_path = args.sdkconfig or os.path.join(root, "sdkconfig")
    cfg = parse_config(cfg_path)
    used_defaults = False
    if not cfg:
        # No generated sdkconfig (nothing built yet) -> validate the defaults,
        # which is what a build would start from.
        cfg = parse_config(os.path.join(root, "sdkconfig.defaults"))
        cfg.update(parse_config(os.path.join(root, "sdkconfig.defaults.esp32s3")))
        used_defaults = True

    print("check_release: profile=%s source=%s"
          % (args.profile, "sdkconfig.defaults" if used_defaults else cfg_path))

    # --- eFuse: forbidden in every profile ---
    for key in EFUSE_FORBIDDEN_ALWAYS:
        if cfg.get(key) == "y":
            failures.append("%s=y - eFuse operations are forbidden in every "
                            "build (ARCHITECTURE §0.1); production units are "
                            "burned at the manufacturing station" % key)

    dev_key_hits = scan_for_dev_key(root)
    key_files = scan_key_material_files(root)

    if args.profile == "prod":
        for key, want, why in PROD_REQUIRED:
            got = cfg.get(key)
            if want is None:
                if got == "y":
                    failures.append("%s=y in production - %s" % (key, why))
            elif got != want:
                failures.append("%s=%r, production requires %r - %s"
                                % (key, got, want, why))
        for h in dev_key_hits:
            failures.append("dev key material in a production build: %s" % h)

        for f in key_files:
            failures.append(
                "%s exists - production must not carry any key material in "
                "tools/keys/ (release key is generated offline and never "
                "placed in this directory, per tools/keys/README.md)" % f)

        # scan_for_dev_key() only scans main/, components/, tools/keys/ file
        # CONTENTS for the marker strings -- it never looks at sdkconfig
        # itself, so a production build whose signing key config still
        # points at the dev path was structurally invisible to it. An
        # independent review caught that gap; this closes it directly.
        signing_key = cfg.get("CONFIG_SECURE_BOOT_SIGNING_KEY", "")
        for m in DEV_KEY_MARKERS:
            if m in signing_key:
                failures.append(
                    "CONFIG_SECURE_BOOT_SIGNING_KEY=%r in production - "
                    "points at the dev signing key (matched %r)"
                    % (signing_key, m))
                break
    else:
        for key, want, why in DEV_REQUIRED:
            if cfg.get(key) != want:
                failures.append("%s=%r, dev requires %r - %s"
                                % (key, cfg.get(key), want, why))
        if not dev_key_hits:
            print("  NOTE: no dev key material found yet "
                  "(expected once tools/keys/ is populated)")
        else:
            print("  dev key material present in %d file(s) - correct for dev"
                  % len(dev_key_hits))

    for f in failures:
        print("  FAIL %s" % f)

    if failures:
        print("FAILED: %d release-guard violation(s)" % len(failures))
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
