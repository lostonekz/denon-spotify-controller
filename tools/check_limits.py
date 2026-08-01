#!/usr/bin/env python3
"""
GATE 2 — File length limit.

docs/ARCHITECTURE.md project rules: 400 lines maximum per source file.

Also checks the docs, because a 500-line architecture document that mandates
400-line source files is not a rule anyone will take seriously.
"""

import argparse
import io
import os
import sys

LIMIT = 400

SOURCE_EXT = (".c", ".h")
DOC_EXT = (".md",)

SKIP_DIRS = {".git", "managed_components", "__pycache__", ".venv", "node_modules"}


def _is_build_dir(name):
    # Matches "build" (ESP-IDF's own dir) and "build_host"/"build_asan"
    # (docs/DEVELOPMENT.md) by prefix, not by an exact-match list, so the
    # next build-dir rename doesn't silently reopen this gap the way the
    # build/host -> build_host rename just did.
    return name == "build" or name.startswith("build_")

# Generated files are excluded: their length is a function of the table they
# are generated from, and splitting them would mean splitting the table.
GENERATED_SUFFIXES = (".inc",)

# Same reasoning, applied by location instead of suffix: docs/adr/0005's
# "generated at build time... into C arrays" convention -- an assets/
# directory holds ONLY tools/gen_assets.py output (font glyph bitmaps
# today; icons later), never hand-written logic, so nothing under one is
# ever a candidate for the human-readability limit this gate exists to
# enforce. Path-based (any "assets" path segment under components/) rather
# than a new suffix, since lv_font_conv's own output is a real, standalone
# .c file (its own translation unit), not a .inc fragment meant to be
# #include-d into something else.
GENERATED_DIR_SEGMENTS = {"assets"}


def _is_generated_dir(rel_path):
    parts = rel_path.replace(os.sep, "/").split("/")
    return any(p in GENERATED_DIR_SEGMENTS for p in parts[:-1])


def count_lines(path):
    with io.open(path, encoding="utf-8", errors="replace") as fh:
        return sum(1 for _ in fh)


def scan(root, exts, limit):
    over = []
    total = 0
    for dirpath, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS and not _is_build_dir(d)]
        for f in files:
            if not f.endswith(exts):
                continue
            if f.endswith(GENERATED_SUFFIXES):
                continue
            path = os.path.join(dirpath, f)
            if _is_generated_dir(os.path.relpath(path, root)):
                continue
            total += 1
            n = count_lines(path)
            if n > limit:
                over.append((n, os.path.relpath(path, root).replace(os.sep, "/")))
    return total, sorted(over, reverse=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".")
    ap.add_argument("--limit", type=int, default=LIMIT)
    args = ap.parse_args()
    root = os.path.abspath(args.root)

    failed = False

    for label, exts in (("source", SOURCE_EXT), ("docs", DOC_EXT)):
        total, over = scan(root, exts, args.limit)
        print("check_limits[%s]: %d file(s), limit %d" % (label, total, args.limit))
        for n, p in over:
            print("  OVER %4d  %s" % (n, p))
        if over:
            failed = True

    if failed:
        print("FAILED: file(s) over the %d line limit" % args.limit)
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
