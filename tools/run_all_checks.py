#!/usr/bin/env python3
"""
Run every architecture gate. Used by CI and by the CMake build.

Exit code is the number of failed gates, so a build system sees non-zero on
any failure. Every gate runs even if an earlier one fails -- a report listing
one problem at a time is a report nobody wants to iterate against.
"""

import argparse
import os
import subprocess
import sys

GATES = [
    ("includes",       "check_includes.py",      []),
    ("limits",         "check_limits.py",        []),
    ("lvgl_boundary",  "check_lvgl_boundary.py", []),
    ("release_guard",  "check_release.py",       ["--profile", "dev"]),
    ("partitions",     "check_partitions.py",    []),
    ("sdkconfig",      "check_sdkconfig.py",     []),
    ("pins",           "check_pins.py",          []),
    ("mv_vectors",     "denon_mv_oracle.py",     []),
    ("mv_vectors_c",   "denon_mv_oracle.py",     ["--check-c"]),
    ("ota_vectors",    "ota_manifest_oracle.py", []),
    ("ota_vectors_c",  "ota_manifest_oracle.py", ["--check-c"]),
    ("fw_version_header", "gen_fw_version_header.py", ["--check"]),
]

# tools/gen_trust_anchor.py's --check is deliberately NOT a GATES entry: it
# shells out to `openssl` to derive the current key's public half, and
# that isn't reliably on PATH inside the ESP-IDF build's own Python
# environment (this script's stdlib-only, subprocess-free gates all are,
# which is why arch_checks -- the CMake target that runs this file on
# every `idf.py build` -- can trust them unconditionally). Release tooling
# with an external-tool dependency (this, gen_manifest.py, sign_release.py)
# stays a manually-invoked developer check instead.


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".")
    ap.add_argument("--skip", action="append", default=[],
                    help="gate name to skip (repeatable)")
    args = ap.parse_args()

    root = os.path.abspath(args.root)
    tools = os.path.join(root, "tools")

    results = []
    for name, script, extra in GATES:
        if name in args.skip:
            results.append((name, None))
            continue

        path = os.path.join(tools, script)
        if not os.path.exists(path):
            print("=== %s: MISSING %s" % (name, script))
            results.append((name, 1))
            continue

        print("=" * 68)
        print("GATE: %s" % name)
        print("=" * 68)
        sys.stdout.flush()
        cmd = [sys.executable, path]
        if script not in ("denon_mv_oracle.py", "ota_manifest_oracle.py",
                          "gen_fw_version_header.py", "gen_trust_anchor.py"):
            cmd += ["--root", root]
        cmd += extra
        rc = subprocess.run(cmd, cwd=root).returncode
        results.append((name, rc))
        print("")

    print("=" * 68)
    print("SUMMARY")
    print("=" * 68)
    failed = 0
    for name, rc in results:
        if rc is None:
            print("  SKIP  %s" % name)
        elif rc == 0:
            print("  PASS  %s" % name)
        else:
            print("  FAIL  %s" % name)
            failed += 1

    total = len([r for _, r in results if r is not None])
    print("\n%d/%d gate(s) passed" % (total - failed, total))
    return failed


if __name__ == "__main__":
    sys.exit(main())
