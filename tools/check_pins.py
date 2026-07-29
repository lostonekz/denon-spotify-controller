#!/usr/bin/env python3
"""
GATE 7 — GPIO conflict detection in bsp_pins.h.

Not in the original gate list. Added because writing bsp_pins.h immediately
produced three double-assigned GPIOs by transcription error, and a
double-assigned pin on an RGB panel does not fail loudly -- it produces a
plausible but subtly wrong image, or an I2C bus that works until the display
initialises. That is a miserable thing to find in Phase 5.

Cheap to check, so it gets checked.

Parses the #define block, resolves GPIO_NUM_n, and reports any GPIO used by
more than one signal. Signals that are legitimately shared (an I2C bus is one
signal per line) are declared in ALLOWED_SHARED.
"""

import argparse
import os
import re
import sys

DEFINE_RE = re.compile(
    r'^\s*#define\s+(BSP_[A-Z0-9_]+)\s+GPIO_NUM_(\d+)', re.M)

# ESP32-S3 pins that must never be used for anything else.
RESERVED = {
    # Octal PSRAM occupies these on an N16R8 module. Assigning one to the
    # panel produces memory corruption, not a display bug, which is why this
    # is worth a hard check.
    35: "octal PSRAM (N16R8)",
    36: "octal PSRAM (N16R8)",
    37: "octal PSRAM (N16R8)",
    26: "SPI flash",
    27: "SPI flash",
    28: "SPI flash",
    29: "SPI flash",
    30: "SPI flash",
    31: "SPI flash",
    32: "SPI flash",
    33: "SPI flash",
    34: "SPI flash",
}

# Strapping pins: usable, but with boot-time constraints worth flagging.
STRAPPING = {0: "boot mode", 3: "JTAG source", 45: "VDD_SPI", 46: "boot log"}

# Native USB D-/D+. Hard-wired on every ESP32-S3, not board-specific, not
# configurable. This is a hard FAIL, not a warning: the ROM bootloader owns
# these pins before app_main runs, so a build using one for something else
# still flashes and enumerates fine -- then the instant the app configures
# that peripheral, USB drops off with no error and does not come back short
# of the board's physical BOOT-button download-mode sequence. An earlier
# draft of bsp_pins.h put an LCD data line on GPIO 20, and this was a WARN at
# the time; it cost a full debugging detour, including the user physically
# power-cycling the board, before the actual cause (this file) was found.
USB_JTAG_RESERVED = {19: "native USB D-", 20: "native USB D+"}

ALLOWED_SHARED = [
    # e.g. ({"BSP_A", "BSP_B"}, "reason")
]


def parse(path):
    with open(path, encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    pins = {}
    for name, num in DEFINE_RE.findall(text):
        pins.setdefault(int(num), []).append(name)
    return pins


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".")
    args = ap.parse_args()
    root = os.path.abspath(args.root)
    path = os.path.join(root, "components", "bsp_board", "include", "bsp_pins.h")

    if not os.path.exists(path):
        print("check_pins: MISSING %s" % path)
        return 1

    pins = parse(path)
    failures = []
    warnings = []

    print("check_pins: %d GPIO assignment(s) in bsp_pins.h" % sum(
        len(v) for v in pins.values()))

    for gpio in sorted(pins):
        names = pins[gpio]
        if len(names) > 1:
            shared_ok = any(set(names) <= allowed for allowed, _ in ALLOWED_SHARED)
            if not shared_ok:
                failures.append("GPIO %d assigned to %d signals: %s"
                                % (gpio, len(names), ", ".join(sorted(names))))
        if gpio in RESERVED:
            failures.append("GPIO %d (%s) is reserved: %s"
                            % (gpio, ", ".join(names), RESERVED[gpio]))
        if gpio in STRAPPING:
            warnings.append("GPIO %d (%s) is a strapping pin: %s"
                            % (gpio, ", ".join(names), STRAPPING[gpio]))
        if gpio in USB_JTAG_RESERVED:
            failures.append("GPIO %d (%s) is %s - claiming it kills the "
                            "board's USB connection the instant the app "
                            "runs, with no recovery short of BOOT-button "
                            "download mode"
                            % (gpio, ", ".join(names), USB_JTAG_RESERVED[gpio]))

    for w in warnings:
        print("  WARN %s" % w)
    for f in failures:
        print("  FAIL %s" % f)

    if failures:
        print("FAILED: %d pin conflict(s)" % len(failures))
        return 1
    print("OK%s" % (" (%d warning(s))" % len(warnings) if warnings else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
