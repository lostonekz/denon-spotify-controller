#!/usr/bin/env python3
"""
GATE 6 — sdkconfig policy.

Several architectural claims in docs/ are really sdkconfig settings wearing a
prose disguise. This gate makes those settings assertions instead of
intentions. Every entry cites the doc it enforces.

CONFIG_SPIRAM_FETCH_INSTRUCTIONS/RODATA are asserted ON, not off. An earlier
version of this policy required them OFF, reasoning that keeping code/rodata
off PSRAM was the primary R1 mitigation. Phase 0 found that reasoning
incomplete on first hardware boot: without XIP_FROM_PSRAM (which requires
both), any esp_flash_read (nvs_flash_init, an OTA check, any NVS write)
briefly disables the flash/PSRAM cache, and if the LCD's DMA ISR fires in
that window trying to refresh from the PSRAM framebuffer, the core panics
with "Cache disabled but cached memory region accessed". A crash is strictly
worse than the bus contention the old setting guarded against, and that
contention is now something Phase 0 measures empirically rather than assumes
away at compile time. See sdkconfig.defaults.esp32s3 and docs/MEMORY.md §3.
"""

import argparse
import os
import sys

# (key, required_value, doc reference, why it matters)
# required_value None means "must NOT be y".
POLICY = [
    # --- R1: PSRAM bandwidth vs the RGB panel (docs/MEMORY.md §3) ---
    ("CONFIG_SPIRAM", "y", "MEMORY.md §1",
     "8 MB PSRAM holds framebuffers, LVGL pool and artwork"),
    ("CONFIG_SPIRAM_MODE_OCT", "y", "MEMORY.md §3",
     "quad PSRAM has nowhere near the bandwidth for RGB scanout"),
    ("CONFIG_SPIRAM_SPEED_80M", "y", "MEMORY.md §3",
     "40 MHz PSRAM cannot sustain 27 MB/s scanout plus WiFi"),
    ("CONFIG_SPIRAM_FETCH_INSTRUCTIONS", "y", "MEMORY.md §3",
     "required by XIP_FROM_PSRAM - without it, esp_flash_read (NVS, OTA) "
     "disabling the cache concurrently with LCD DMA panics the core"),
    ("CONFIG_SPIRAM_RODATA", "y", "MEMORY.md §3",
     "required by XIP_FROM_PSRAM, same cache-disable/LCD-DMA panic"),
    ("CONFIG_SPIRAM_XIP_FROM_PSRAM", "y", "MEMORY.md §3",
     "keeps the ext-mem cache available during flash ops - the actual fix "
     "for the Phase 0 hardware panic, not a performance nicety"),

    # --- OTA (docs/PLATFORM.md §2) ---
    ("CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE", "y", "PLATFORM.md §2.2",
     "the health gate cannot revert anything without this"),
    ("CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT", "y", "SECURITY.md §1",
     "signature verification runs in EVERY profile, or it rots"),

    # --- eFuse: never from a build (docs/ARCHITECTURE.md §0.1) ---
    ("CONFIG_SECURE_BOOT", None, "ARCHITECTURE.md §0.1",
     "eFuse burn is a manufacturing-station action, never a build action"),
    ("CONFIG_SECURE_FLASH_ENC_ENABLED", None, "ARCHITECTURE.md §0.1",
     "eFuse burn is a manufacturing-station action, never a build action"),

    # --- TLS memory (docs/MEMORY.md §4) ---
    ("CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN", "4096", "MEMORY.md §4",
     "default 16 KB fragment costs ~25 KB extra internal SRAM per session"),
    ("CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN", "2048", "MEMORY.md §4",
     "same, outbound"),

    # --- watchdog (docs/ARCHITECTURE.md §4) ---
    ("CONFIG_ESP_TASK_WDT_INIT", "y", "ARCHITECTURE.md §4",
     "a frozen panel must become a reboot with a coredump"),

    # --- coredump (docs/LIFECYCLE.md §2) ---
    ("CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH", "y", "LIFECYCLE.md §2",
     "a field failure that cannot be reproduced never gets fixed"),

    # --- partition table (docs/PLATFORM.md §1) ---
    ("CONFIG_PARTITION_TABLE_CUSTOM", "y", "PLATFORM.md §1",
     "the documented layout is not one of the presets"),
    ("CONFIG_ESPTOOLPY_FLASHSIZE_16MB", "y", "PLATFORM.md §1",
     "the partition table assumes 16 MB"),
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".")
    ap.add_argument("--sdkconfig", default=None)
    args = ap.parse_args()
    root = os.path.abspath(args.root)

    if args.sdkconfig:
        cfg = parse_config(args.sdkconfig)
        source = args.sdkconfig
    else:
        generated = os.path.join(root, "sdkconfig")
        if os.path.exists(generated):
            cfg = parse_config(generated)
            source = "sdkconfig (generated)"
        else:
            cfg = parse_config(os.path.join(root, "sdkconfig.defaults"))
            cfg.update(parse_config(os.path.join(root, "sdkconfig.defaults.esp32s3")))
            source = "sdkconfig.defaults + .esp32s3"

    print("check_sdkconfig: source=%s, %d key(s) loaded, %d policy rule(s)"
          % (source, len(cfg), len(POLICY)))

    if not cfg:
        print("  FAIL no sdkconfig found at all")
        print("FAILED")
        return 1

    failures = []
    for key, want, ref, why in POLICY:
        got = cfg.get(key)
        if want is None:
            if got == "y":
                failures.append("%s=y must be disabled [%s] - %s" % (key, ref, why))
        else:
            if got != want:
                failures.append("%s=%r, requires %r [%s] - %s"
                                % (key, got, want, ref, why))

    for f in failures:
        print("  FAIL %s" % f)

    if failures:
        print("FAILED: %d sdkconfig policy violation(s)" % len(failures))
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
