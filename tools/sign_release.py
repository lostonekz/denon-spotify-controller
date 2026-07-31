#!/usr/bin/env python3
"""
Orchestrates a full release build + signed manifest: `idf.py build` (RSA
image signing is already fully solved by existing ESP-IDF tooling, working
since Phase 1) -> tools/gen_manifest.py -> stages both files for the
exit-criteria proofs' local HTTPS server (docs/ROADMAP.md's Tier 7).

Assumes an already-activated ESP-IDF environment (idf.py on PATH) -- same
assumption every other `idf.py build` invocation in this project makes;
this script does not source export.ps1/export.sh itself.

Usage:
    python tools/sign_release.py --url https://ota.example.com/dsc-0.2.0.bin
    python tools/sign_release.py --url ... --skip-build   # reuse the last build
"""

import argparse
import json
import os
import shutil
import subprocess
import sys


def run_idf_build(root):
    proc = subprocess.run(["idf.py", "build"], cwd=root)
    return proc.returncode == 0


def app_bin_path(root):
    desc_path = os.path.join(root, "build", "project_description.json")
    if not os.path.exists(desc_path):
        raise RuntimeError("no build/project_description.json -- has `idf.py build` run at least once?")
    with open(desc_path, encoding="utf-8") as fh:
        desc = json.load(fh)
    return os.path.join(root, "build", desc["app_bin"])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", required=True, help="URL the device will fetch the image from")
    ap.add_argument("--skip-build", action="store_true", help="reuse the existing build/ output")
    ap.add_argument("--key", default=None)
    ap.add_argument("--key-id", default=None)
    ap.add_argument("--min-prev-build", type=int, default=0)
    ap.add_argument("--mandatory", action="store_true")
    ap.add_argument("--notes", default="")
    ap.add_argument("--out-dir", default=None, help="staging directory (default: release/)")
    args = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    out_dir = args.out_dir or os.path.join(root, "release")

    if not args.skip_build:
        print("=== idf.py build ===")
        if not run_idf_build(root):
            print("FAILED: idf.py build")
            return 1

    try:
        image_path = app_bin_path(root)
    except RuntimeError as e:
        print("FAILED: %s" % e)
        return 1

    if not os.path.exists(image_path):
        print("MISSING built image: %s" % image_path)
        return 1

    os.makedirs(out_dir, exist_ok=True)
    staged_image = os.path.join(out_dir, os.path.basename(image_path))
    shutil.copyfile(image_path, staged_image)

    manifest_path = os.path.join(out_dir, "manifest.json")
    gen_argv = [
        sys.executable, os.path.join(here, "gen_manifest.py"),
        "--image", staged_image,
        "--url", args.url,
        "--min-prev-build", str(args.min_prev_build),
        "--notes", args.notes,
        "--out", manifest_path,
    ]
    if args.key:
        gen_argv += ["--key", args.key]
    if args.key_id:
        gen_argv += ["--key-id", args.key_id]
    if args.mandatory:
        gen_argv.append("--mandatory")

    print("=== gen_manifest.py ===")
    rc = subprocess.run(gen_argv, cwd=root).returncode
    if rc != 0:
        return rc

    print("staged release in %s:" % out_dir)
    print("  %s" % staged_image)
    print("  %s" % manifest_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
