#!/usr/bin/env python3
"""
Negative tests for the architecture gates. Tests the tests.

A gate that has only ever returned PASS is not evidence of anything -- it might
be passing because it is broken, because its path resolution is wrong, or
because it silently found zero files to inspect. That last one actually
happened during development of this repo: a checker was reporting success
while walking an empty directory.

So each gate here is shown a deliberate violation in a scratch copy of the
repo and must FAIL. Then the copy is discarded. Nothing touches the real tree.

    python tools/test_gates.py
"""

import os
import shutil
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def clone_repo(dst):
    """Copy exactly the files git tracks, with their current on-disk content
    (including uncommitted edits).

    Deliberately NOT a hand-maintained exclusion list. An early version of
    this function skipped a fixed set of directory names, and every one of
    the three build directories in this project's history (build/,
    build_host/, build_asan/) has at some point slipped past a list like
    that -- most recently taking check_limits.py down with it the moment
    build/host was renamed to build_host. A generated `sdkconfig` at the
    repo root caused the same failure mode here: it isn't a directory, so no
    directory-exclusion list would ever have caught it, and it silently
    outranked the sdkconfig.defaults files the sdkconfig/release_guard
    mutations edit, making both gates look broken when they were not.

    `git ls-files` is authoritative and self-maintaining: it excludes
    exactly what .gitignore excludes, automatically, forever, with no
    parallel list for this function to fall out of sync with.
    """
    files = subprocess.run(
        ["git", "-C", REPO, "ls-files", "-z"],
        capture_output=True, check=True).stdout.decode("utf-8")
    for rel in files.split("\0"):
        if not rel:
            continue
        src = os.path.join(REPO, rel)
        tgt = os.path.join(dst, rel)
        os.makedirs(os.path.dirname(tgt), exist_ok=True)
        shutil.copy2(src, tgt)


def run_gate(root, script, extra=None):
    cmd = [sys.executable, os.path.join(root, "tools", script)]
    if script != "denon_mv_oracle.py":
        cmd += ["--root", root]
    if extra:
        cmd += extra
    p = subprocess.run(cmd, capture_output=True, text=True, cwd=root)
    return p.returncode, p.stdout + p.stderr


# --- each mutation returns a description; gate must fail afterwards -------

def mut_layering(root):
    """A core component reaching up into a service: the violation the whole
    layering scheme exists to prevent."""
    p = os.path.join(root, "components", "core_types")
    os.makedirs(os.path.join(p, "include"), exist_ok=True)
    with open(os.path.join(p, "include", "core_types.h"), "w") as fh:
        fh.write('#ifndef CT_H\n#define CT_H\n'
                 '#include "denon_codec.h"\n'   # core -> svc, forbidden
                 '#endif\n')
    with open(os.path.join(p, "CMakeLists.txt"), "w") as fh:
        fh.write('idf_component_register(INCLUDE_DIRS "include")\n')
    return "core_types includes svc_denon header"


def mut_file_length(root):
    p = os.path.join(root, "components", "svc_denon", "bloat.c")
    with open(p, "w") as fh:
        fh.write("/* deliberately oversized */\n")
        for i in range(450):
            fh.write("int pad_%d = %d;\n" % (i, i))
    return "450-line source file"


def mut_lvgl_boundary(root):
    p = os.path.join(root, "components", "svc_denon", "sneaky.c")
    with open(p, "w") as fh:
        fh.write('#include "lvgl.h"\nvoid f(void) {}\n')
    return "non-ui component includes lvgl.h"


def mut_pins(root):
    p = os.path.join(root, "components", "bsp_board", "include", "bsp_pins.h")
    with open(p, encoding="utf-8") as fh:
        txt = fh.read()
    txt = txt.replace("#define BSP_TOUCH_INT           GPIO_NUM_38",
                      "#define BSP_TOUCH_INT           GPIO_NUM_21")  # == PCLK
    with open(p, "w", encoding="utf-8") as fh:
        fh.write(txt)
    return "touch INT collides with LCD PCLK"


def mut_partitions_overlap(root):
    p = os.path.join(root, "partitions.csv")
    with open(p, encoding="utf-8") as fh:
        txt = fh.read()
    txt = txt.replace("ota_1,      app,  ota_1,    0x620000, 0x600000,",
                      "ota_1,      app,  ota_1,    0x600000, 0x600000,")
    with open(p, "w", encoding="utf-8") as fh:
        fh.write(txt)
    return "ota_1 overlaps ota_0"


def mut_partitions_asymmetric(root):
    p = os.path.join(root, "partitions.csv")
    with open(p, encoding="utf-8") as fh:
        txt = fh.read()
    txt = txt.replace("ota_1,      app,  ota_1,    0x620000, 0x600000,",
                      "ota_1,      app,  ota_1,    0x620000, 0x500000,")
    with open(p, "w", encoding="utf-8") as fh:
        fh.write(txt)
    return "A/B slots differ in size (silently breaks rollback)"


def mut_sdkconfig_r1(root):
    """The R1 mitigation being silently switched off -- the exact regression
    this gate exists to catch."""
    p = os.path.join(root, "sdkconfig.defaults.esp32s3")
    with open(p, encoding="utf-8") as fh:
        txt = fh.read()
    txt = txt.replace("CONFIG_SPIRAM_FETCH_INSTRUCTIONS=n",
                      "CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y")
    with open(p, "w", encoding="utf-8") as fh:
        fh.write(txt)
    return "SPIRAM_FETCH_INSTRUCTIONS re-enabled (kills the R1 mitigation)"


def mut_sdkconfig_efuse(root):
    p = os.path.join(root, "sdkconfig.defaults")
    with open(p, encoding="utf-8") as fh:
        txt = fh.read()
    txt = txt.replace("CONFIG_SECURE_BOOT=n", "CONFIG_SECURE_BOOT=y")
    with open(p, "w", encoding="utf-8") as fh:
        fh.write(txt)
    return "Secure Boot enabled in a build (eFuse burn, forbidden)"


def mut_signing_off(root):
    p = os.path.join(root, "sdkconfig.defaults")
    with open(p, encoding="utf-8") as fh:
        txt = fh.read()
    txt = txt.replace("CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT=y",
                      "CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT=n")
    with open(p, "w", encoding="utf-8") as fh:
        fh.write(txt)
    return "signature verification disabled in dev (the rot this prevents)"


def mut_mv_vector(root):
    """The 10 dB error from docs/RISKS.md R13, injected into the table."""
    p = os.path.join(root, "test", "host", "denon_mv_vectors.csv")
    with open(p, encoding="utf-8") as fh:
        txt = fh.read()
    txt = txt.replace("parse,MV525,OK,105,", "parse,MV525,OK,1050,")
    with open(p, "w", encoding="utf-8") as fh:
        fh.write(txt)
    return "MV525 expected value wrong (52.5 dB read as 525)"


def mut_mv_stale_include(root):
    p = os.path.join(root, "test", "host", "denon_mv_vectors.csv")
    with open(p, "a", encoding="utf-8") as fh:
        fh.write("parse,MV42,OK,84,added without regenerating\n")
    return "CSV changed without regenerating the C include"


CASES = [
    ("includes",      mut_layering,             "check_includes.py",      None),
    ("limits",        mut_file_length,          "check_limits.py",        None),
    ("lvgl_boundary", mut_lvgl_boundary,        "check_lvgl_boundary.py", None),
    ("pins",          mut_pins,                 "check_pins.py",          None),
    ("partitions",    mut_partitions_overlap,   "check_partitions.py",    None),
    ("partitions",    mut_partitions_asymmetric,"check_partitions.py",    None),
    ("sdkconfig",     mut_sdkconfig_r1,         "check_sdkconfig.py",     None),
    ("sdkconfig",     mut_sdkconfig_efuse,      "check_sdkconfig.py",     None),
    ("release_guard", mut_signing_off,          "check_release.py",  ["--profile", "dev"]),
    ("mv_vectors",    mut_mv_vector,            "denon_mv_oracle.py",     None),
    ("mv_vectors_c",  mut_mv_stale_include,     "denon_mv_oracle.py", ["--check-c"]),
]


def main():
    print("test_gates: each gate must FAIL when shown a deliberate violation\n")

    passed = 0
    failed = []

    for gate, mutate, script, extra in CASES:
        tmp = tempfile.mkdtemp(prefix="dsc_gate_")
        try:
            clone_repo(tmp)

            rc_before, _ = run_gate(tmp, script, extra)
            if rc_before != 0:
                failed.append("%s: clean clone already fails (rc=%d)" % (gate, rc_before))
                continue

            desc = mutate(tmp)
            rc_after, out = run_gate(tmp, script, extra)

            if rc_after == 0:
                failed.append("%s: did NOT detect %r" % (gate, desc))
                print("  MISS  %-14s %s" % (gate, desc))
            else:
                passed += 1
                print("  caught %-13s %s" % (gate, desc))
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    print("\n%d/%d violations detected" % (passed, len(CASES)))
    for f in failed:
        print("  FAIL %s" % f)

    if failed:
        print("\nFAILED: some gates do not actually enforce what they claim")
        return 1
    print("\nOK: every gate detects its violation and passes on a clean tree")
    return 0


if __name__ == "__main__":
    sys.exit(main())
