#!/usr/bin/env python3
"""
GATE 5 — Partition table sanity.

docs/PLATFORM.md §1. The partition table is the one thing in this project that
needs physical access to change on a deployed unit, so getting it wrong is the
most expensive available mistake. It is worth a dedicated gate.

Checks:
  - no overlaps, no gaps that swallow a partition
  - offsets and sizes within the 16 MB device
  - app partitions 64 KB-aligned (bootloader requirement)
  - ota_0 and ota_1 identical in size (asymmetric slots break A/B silently)
  - the partitions docs/PLATFORM.md promises all exist, at the promised sizes
  - nvs_keys reserved (docs/ARCHITECTURE.md §0.1: reserve the option)
"""

import argparse
import csv
import os
import sys

FLASH_SIZE = 16 * 1024 * 1024

# name -> (type, subtype, min_size). Sizes from docs/PLATFORM.md §1.
REQUIRED = {
    "nvs":      ("data", "nvs",      0x6000),
    "nvs_keys": ("data", "nvs_keys", 0x1000),
    "otadata":  ("data", "ota",      0x2000),
    "ota_0":    ("app",  "ota_0",    0x600000),
    "ota_1":    ("app",  "ota_1",    0x600000),
    "storage":  ("data", "spiffs",   0x380000),
    "coredump": ("data", "coredump", 0x40000),
}

APP_ALIGN = 0x10000  # 64 KB


def parse_size(s):
    s = s.strip()
    if not s:
        return None
    if s.lower().endswith("k"):
        return int(s[:-1], 0) * 1024
    if s.lower().endswith("m"):
        return int(s[:-1], 0) * 1024 * 1024
    return int(s, 0)


def load(path):
    rows = []
    with open(path, encoding="utf-8") as fh:
        for raw in fh:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            parts = next(csv.reader([line]))
            parts = [p.strip() for p in parts]
            while len(parts) < 5:
                parts.append("")
            rows.append({
                "name": parts[0],
                "type": parts[1],
                "subtype": parts[2],
                "offset": parse_size(parts[3]),
                "size": parse_size(parts[4]),
            })
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".")
    args = ap.parse_args()
    root = os.path.abspath(args.root)
    path = os.path.join(root, "partitions.csv")

    if not os.path.exists(path):
        print("check_partitions: MISSING partitions.csv")
        return 1

    rows = load(path)
    failures = []

    print("check_partitions: %d partition(s) in partitions.csv" % len(rows))

    by_name = {r["name"]: r for r in rows}

    # --- required partitions present, at promised sizes ---
    for name, (ptype, subtype, min_size) in REQUIRED.items():
        r = by_name.get(name)
        if r is None:
            failures.append("missing required partition %r "
                            "(docs/PLATFORM.md §1)" % name)
            continue
        if r["type"] != ptype or r["subtype"] != subtype:
            failures.append("%s: type/subtype %s/%s, docs say %s/%s"
                            % (name, r["type"], r["subtype"], ptype, subtype))
        if r["size"] is None or r["size"] < min_size:
            failures.append("%s: size %s < documented %s"
                            % (name, r["size"], hex(min_size)))

    # --- geometry ---
    ordered = sorted([r for r in rows if r["offset"] is not None],
                     key=lambda r: r["offset"])
    prev = None
    for r in ordered:
        if r["size"] is None:
            failures.append("%s: no size" % r["name"])
            continue
        end = r["offset"] + r["size"]
        if end > FLASH_SIZE:
            failures.append("%s: ends at 0x%X, past 16 MB flash (0x%X)"
                            % (r["name"], end, FLASH_SIZE))
        if prev is not None:
            prev_end = prev["offset"] + prev["size"]
            if r["offset"] < prev_end:
                failures.append("%s overlaps %s (0x%X < 0x%X)"
                                % (r["name"], prev["name"], r["offset"], prev_end))
        if r["type"] == "app" and (r["offset"] % APP_ALIGN) != 0:
            failures.append("%s: app partition offset 0x%X is not 64 KB aligned"
                            % (r["name"], r["offset"]))
        prev = r

    # --- A/B slots must be identical, or rollback is a lie ---
    a, b = by_name.get("ota_0"), by_name.get("ota_1")
    if a and b and a["size"] != b["size"]:
        failures.append("ota_0 (0x%X) and ota_1 (0x%X) differ in size - "
                        "asymmetric A/B slots break rollback silently"
                        % (a["size"], b["size"]))

    # --- the reserve-the-option principle ---
    if "nvs_keys" not in by_name:
        failures.append("nvs_keys not reserved - ARCHITECTURE §0.1 requires "
                        "reserving it so encrypted NVS stays a config change "
                        "rather than a reflash")

    used = sum(r["size"] for r in rows if r["size"])
    print("  flash used: 0x%X of 0x%X (%.1f%%), free tail: 0x%X"
          % (used, FLASH_SIZE, 100.0 * used / FLASH_SIZE, FLASH_SIZE - used))

    for f in failures:
        print("  FAIL %s" % f)

    if failures:
        print("FAILED: %d partition problem(s)" % len(failures))
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
