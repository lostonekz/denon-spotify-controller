#!/usr/bin/env python3
"""
Generates components/ui/assets/fonts/*.c from the vendored source TTFs
under tools/font_sources/ -- docs/adr/0005-assets-in-app-image.md, Phase 5
plan Tier 2.

Manually run, not part of the routine `idf.py build` (see the note added
to the Phase 5 plan's Tier 2 section during implementation): the generated
.c files are committed, like tools/gen_fw_version_header.py's/
tools/gen_manifest.py's own output. Re-run this only when the type scale,
glyph set, or source fonts change.

Real toolchain dependency, isolated to this script alone: Node.js + the
pinned `lv_font_conv` in tools/package.json (`npm install` once in tools/).
Nothing else in this repo's build needs Node.

Type scale (the user's own design system, Phase 5 plan Context -- this is
the ground truth; UI_ARCHITECTURE.md's older 40/26/18/15 table is stale and
corrected in Tier 8):
    display  44 / 700
    title    30 / 600
    body     20 / 400
    label    16 / 500
    meta     13 / 500  (IBM Plex Mono, not Archivo)
    meta_lg  15 / 500  (IBM Plex Mono -- Art-forward's volume numeral readout only)
    hero     38 / 700  (Art-forward Now Playing's on-image track title only)

Glyph set (UI_ARCHITECTURE.md §5.3b): Basic Latin + Latin-1 Supplement +
Latin Extended-A, plus curly quotes/en-dash/em-dash/ellipsis (already
covered by Basic Latin + Latin-1 Supplement/named punctuation in Archivo
and IBM Plex Mono directly) and the flat/sharp music symbols (NOT present
in either font -- merged in from Noto Music). U+FFFD (the replacement
character view_model_text.c emits for unrepresentable input, R17) is
ALSO not present in Archivo or IBM Plex Mono -- merged in from Noto Sans.
Without these two merges, R17's "never a `.notdef` box" guarantee would
hold at the string layer (Tier 1) but silently fail at the font layer:
LVGL would fall back to its own missing-glyph box the moment a real
replacement character needed to render.
"""
import argparse
import shutil
import subprocess
import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
REPO_ROOT = TOOLS_DIR.parent
FONT_SOURCES = TOOLS_DIR / "font_sources"
LV_FONT_CONV = TOOLS_DIR / "node_modules" / "lv_font_conv" / "lv_font_conv.js"
OUT_DIR = REPO_ROOT / "components" / "ui" / "assets" / "fonts"

# Basic Latin (printable) + Latin-1 Supplement (printable, skipping the
# C1 control range 0x80-0x9F) + Latin Extended-A.
PRIMARY_RANGES = ["0x20-0x7E", "0xA0-0xFF", "0x100-0x17F"]

# U+FFFD: not in Archivo or IBM Plex Mono -- sourced from a 1-glyph subset
# of Noto Sans (tools/font_sources/NotoSans-Regular.ttf).
REPLACEMENT_CHAR_FONT = FONT_SOURCES / "NotoSans-Regular.ttf"
REPLACEMENT_CHAR_RANGE = "0xFFFD"

# Flat (U+266D) / sharp (U+266F): not in Archivo or IBM Plex Mono either --
# sourced from a 2-glyph subset of Noto Music.
MUSIC_SYMBOL_FONT = FONT_SOURCES / "NotoMusic-Regular.ttf"
MUSIC_SYMBOL_RANGES = ["0x266D", "0x266F"]

BPP = 4

# (lv font symbol name, primary source font, size)
FONTS = [
    ("ui_font_display", FONT_SOURCES / "Archivo-700.ttf", 44),
    ("ui_font_hero",    FONT_SOURCES / "Archivo-700.ttf", 38),   # Art-forward redesign:
                                                                    # resized from 34px
    ("ui_font_title",   FONT_SOURCES / "Archivo-600.ttf", 30),
    ("ui_font_body",    FONT_SOURCES / "Archivo-400.ttf", 20),
    ("ui_font_label",   FONT_SOURCES / "Archivo-500.ttf", 16),
    ("ui_font_meta",    FONT_SOURCES / "IBMPlexMono-500.ttf", 13),
    ("ui_font_meta_lg", FONT_SOURCES / "IBMPlexMono-500.ttf", 15),   # Art-forward
                                                                        # redesign: volume
                                                                        # numeral readout
]


def find_node():
    node = shutil.which("node")
    if node is None:
        sys.exit("gen_assets: 'node' not found on PATH -- install Node.js "
                  "(only needed to re-run this script, not for idf.py build)")
    return node


def check_prereqs():
    if not LV_FONT_CONV.exists():
        sys.exit(f"gen_assets: {LV_FONT_CONV} not found -- run 'npm install' "
                  f"in {TOOLS_DIR} first (tools/package.json pins the version)")
    missing = [p for p in [REPLACEMENT_CHAR_FONT, MUSIC_SYMBOL_FONT] +
               [f for _, f, _ in FONTS] if not p.exists()]
    if missing:
        sys.exit("gen_assets: missing vendored font source(s):\n" +
                  "\n".join(f"  {p}" for p in missing))


def build_cmd(node, name, primary_font, size, out_path):
    cmd = [node, str(LV_FONT_CONV)]
    cmd += ["--font", str(primary_font)]
    for r in PRIMARY_RANGES:
        cmd += ["-r", r]
    cmd += ["--font", str(REPLACEMENT_CHAR_FONT), "-r", REPLACEMENT_CHAR_RANGE]
    cmd += ["--font", str(MUSIC_SYMBOL_FONT)]
    for r in MUSIC_SYMBOL_RANGES:
        cmd += ["-r", r]
    cmd += ["--size", str(size), "--bpp", str(BPP), "--format", "lvgl"]
    cmd += ["--lv-font-name", name]
    cmd += ["-o", str(out_path)]
    return cmd


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true",
                     help="Regenerate into a temp dir and diff against what's "
                          "committed, instead of overwriting -- for CI/gate use.")
    args = ap.parse_args()

    check_prereqs()
    node = find_node()

    target_dir = OUT_DIR
    if args.check:
        import tempfile
        target_dir = Path(tempfile.mkdtemp(prefix="gen_assets_check_"))

    target_dir.mkdir(parents=True, exist_ok=True)

    for name, primary_font, size in FONTS:
        out_path = target_dir / f"{name}.c"
        cmd = build_cmd(node, name, primary_font, size, out_path)
        print(f"  {name} <- {primary_font.name} @ {size}px, {BPP}bpp")
        result = subprocess.run(cmd, cwd=TOOLS_DIR, capture_output=True, text=True)
        if result.returncode != 0:
            sys.exit(f"gen_assets: lv_font_conv failed for {name}:\n"
                      f"{result.stdout}\n{result.stderr}")

    if args.check:
        import filecmp
        diffs = []
        for name, _, _ in FONTS:
            committed = OUT_DIR / f"{name}.c"
            fresh = target_dir / f"{name}.c"
            if not committed.exists() or not filecmp.cmp(committed, fresh, shallow=False):
                diffs.append(name)
        shutil.rmtree(target_dir, ignore_errors=True)
        if diffs:
            sys.exit("gen_assets --check: out of sync with committed output: " +
                      ", ".join(diffs) +
                      " -- run 'python tools/gen_assets.py' and commit the result")
        print("gen_assets --check: components/ui/assets/fonts/ is in sync")
        return

    print(f"Wrote {len(FONTS)} font(s) to {OUT_DIR}")


if __name__ == "__main__":
    main()
