#!/usr/bin/env python3
"""
GATE 3 — LVGL symbol boundary.

docs/adr/0008-own-the-lvgl-glue.md. We deliberately run WITHOUT an LVGL lock:
the invariant "only the ui task touches LVGL" is enforced structurally instead.

CMake REQUIRES is necessary but not proof -- it constrains headers, not symbols,
and a transitive dependency can reintroduce the edge silently. This gate is the
proof: nm every object outside components/ui/, fail on any undefined reference
matching ^lv_.

Runs post-link, so it needs a build directory. In CI it runs after idf.py build.
Source-level pre-check runs anywhere and catches the common case early.
"""

import argparse
import os
import re
import subprocess
import sys

LV_SYM = re.compile(r"^lv_[A-Za-z0-9_]+$")
LV_INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]((?:lvgl|lv_)[^>"]*)[>"]', re.M)

SKIP_DIRS = {".git", "build", "managed_components", "__pycache__"}

# Only these components may reference LVGL.
UI_COMPONENTS = {"ui"}


def source_precheck(root):
    """Cheap, compiler-free: no non-ui component may #include LVGL."""
    violations = []
    comp_root = os.path.join(root, "components")
    if not os.path.isdir(comp_root):
        return violations

    for dirpath, dirs, files in os.walk(comp_root):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        rel = os.path.relpath(dirpath, comp_root).replace(os.sep, "/")
        comp = rel.split("/")[0]
        if comp in UI_COMPONENTS:
            continue
        for f in files:
            if not f.endswith((".c", ".h")):
                continue
            path = os.path.join(dirpath, f)
            with open(path, encoding="utf-8", errors="replace") as fh:
                text = fh.read()
            for inc in LV_INCLUDE.findall(text):
                violations.append("%s includes %r (component %r is not ui)"
                                  % (os.path.relpath(path, root).replace(os.sep, "/"),
                                     inc, comp))
    return violations


def find_nm():
    for cand in ("xtensa-esp32s3-elf-nm", "nm", "llvm-nm"):
        try:
            subprocess.run([cand, "--version"], capture_output=True, check=False)
            return cand
        except (OSError, FileNotFoundError):
            continue
    return None


def symbol_check(root, build_dir, nm):
    """Post-link: undefined lv_* references from non-ui objects."""
    violations = []
    checked = 0

    for dirpath, dirs, files in os.walk(build_dir):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for f in files:
            if not f.endswith(".obj") and not f.endswith(".o"):
                continue
            path = os.path.join(dirpath, f)
            norm = path.replace(os.sep, "/")

            # Only inspect our own components; skip IDF and LVGL's own objects.
            if "/esp-idf/" in norm and "/components/" not in norm:
                pass
            if any("/%s.dir/" % c in norm or "/%s/" % c in norm
                   for c in UI_COMPONENTS):
                continue
            if "lvgl" in norm.lower():
                continue

            try:
                out = subprocess.run([nm, "-u", path], capture_output=True,
                                     text=True, check=False)
            except OSError:
                continue

            checked += 1
            for line in out.stdout.splitlines():
                sym = line.strip().split()[-1] if line.strip() else ""
                if LV_SYM.match(sym):
                    violations.append("%s references %s"
                                      % (os.path.relpath(path, build_dir), sym))

    return checked, violations


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".")
    ap.add_argument("--build-dir", default=None,
                    help="build output dir; if absent, only the source "
                         "pre-check runs")
    args = ap.parse_args()
    root = os.path.abspath(args.root)

    violations = source_precheck(root)
    print("check_lvgl_boundary: source pre-check done")

    build_dir = args.build_dir or os.path.join(root, "build")
    if os.path.isdir(build_dir):
        nm = find_nm()
        if nm is None:
            print("  NOTE: no nm found; symbol check skipped "
                  "(source pre-check still ran)")
        else:
            checked, sym_v = symbol_check(root, build_dir, nm)
            print("  symbol check: %d object(s) via %s" % (checked, nm))
            violations.extend(sym_v)
    else:
        print("  NOTE: no build dir at %s; symbol check skipped. "
              "Run after idf.py build for full enforcement." % build_dir)

    for v in violations:
        print("  VIOLATION %s" % v)

    if violations:
        print("FAILED: %d LVGL boundary violation(s)" % len(violations))
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
