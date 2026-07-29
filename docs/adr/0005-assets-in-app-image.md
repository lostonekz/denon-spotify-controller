# 0005 — Compile fonts and icons into the app image

Status: Accepted
Date: 2026-07-28

## Context

The UI needs four font sizes (40, 26, 18, 15 px) plus an icon set. Rendered as LVGL bitmap
fonts with a Latin-1 glyph set, this is plausibly 400 KB–1 MB of read-only data.

With a dual-slot OTA layout that data is stored twice — once per app slot. The obvious
optimisation is a separate `assets` partition holding the fonts, loaded at runtime, stored
once and updatable independently.

The 16 MB flash makes this a genuine choice rather than a forced one.

## Decision

Fonts and icons are generated at build time by `tools/gen_assets.py` into C arrays and
**linked into the app image**. The `storage` partition holds only derivable data (the album
art cache and rotated logs).

App slots are sized at 6 MiB each to absorb the cost with room to spare.

## Consequences

**Good — and this is the whole reason.** An OTA update is **atomic**. Firmware and its
assets are one artifact, versioned together, rolled back together. There is no state in
which a new binary is running against old fonts.

**Good.** No asset-version negotiation, no missing-glyph fallback paths, no filesystem
mount failure mode on the boot path, no "the display is blank because SPIFFS is corrupt"
support case. Each of those is a small piece of code and a permanent source of rare bugs.

**Good.** Fonts are memory-mapped from flash rather than loaded into RAM, so the runtime
cost is zero.

**Cost.** ~1 MB of flash duplicated across the two slots. With 6 MiB slots this is
comfortably affordable, and flash is the resource we have most of.

**Cost.** Changing a font requires a full firmware update. Given that fonts change roughly
never after the design settles, this is not a real constraint.

**Revisit if.** App image size approaches 4 MiB, or a future feature needs a large CJK or
full-Unicode font. At that point a separate partition becomes worth its complexity — and
the partition table has been sized so that change is possible.

## Alternatives rejected

**Separate `assets` partition with a LittleFS filesystem and `lv_font_load`.** The
efficient answer, and wrong here. Rejected because it introduces *version skew*: after a
partial or interrupted update, the app and its assets can be at different versions, and
every screen then needs a path for "the glyph I need doesn't exist in the installed font".
That is a permanent, low-grade complexity tax paid across the entire UI layer to save flash
we are not short of. On a device with no console and no keyboard, a boot-path filesystem
mount failure is also a materially worse failure mode than a slightly larger binary.

**Ship a minimal font in the image and download the full set on first boot.** Rejected:
adds a network dependency to first-boot rendering, and creates a device that looks wrong
until it has been online — a bad first impression for a device whose entire premise is feel.

**Use LVGL's built-in fonts.** Rejected on design grounds. The built-ins are Montserrat at
fixed weights and do not support the typographic hierarchy the design calls for. Typography
is a large fraction of what makes the device read as premium.

**Runtime vector font rendering (FreeType).** Rejected: significant RAM and CPU per glyph on
a device where PSRAM bandwidth is already the top risk, in exchange for flexibility we do
not need at four fixed sizes.
