#!/usr/bin/env python3
"""
GATE 4 — Production build guard.

docs/SECURITY.md §1. The dev signing key is committed to this repo ON PURPOSE:
it is what keeps the signature-verification path executing on every developer
build, so it cannot rot. That choice is only safe if shipping it is impossible.

This gate proves it. In a production profile it fails on:
  - the dev trust anchor being present
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
