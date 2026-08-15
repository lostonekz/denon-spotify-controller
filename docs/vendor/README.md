# Vendor documentation — Waveshare ESP32-S3-Touch-LCD-4

Downloaded 2026-08-15 from the Waveshare wiki. These are unmodified vendor files, kept here
because the wiki has already proven unreliable across board revisions and because
`www.waveshare.com` blocks scripted fetches (HTTP 403 without a browser User-Agent).

| File | Source |
|---|---|
| `ESP32-S3-Touch-LCD-4_V4.0_schematic.pdf` | <https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-4/ESP32-S3-Touch-LCD-4%20V4.0.pdf> |
| `SW6106_datasheet.pdf` | <https://files.waveshare.com/wiki/common/SW6106%20Datasheet.pdf> — **see the revision warning below; this part is not on V4.0** |
| `ESP32-S3-Touch-LCD-4_wiki.html` | <https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4> |

This board is **Rev4.0**, per [../../README.md](../../README.md). Waveshare publishes separate
schematics for V1/V2/V3/V4; only the V4.0 sheet applies here.

## Battery connector — J1

Traced from the V4.0 schematic (page 1, connector `J1`), not from the wiki prose:

| J1 pin | Net |
|---|---|
| 1 | **GND** (battery negative) |
| 2 | **B+** (battery positive) |

How this was established, so it can be re-checked rather than trusted:

- `J1`'s body is a rectangle spanning x 507.6–516.9, y 94.8–104.04. Two wires enter its left
  edge at y = 97.96 (pin 1) and y = 101.13 (pin 2).
- The net label `B+` sits at x 487.0 with its baseline on y = 101.13 — the left endpoint of
  the **pin 2** wire.
- A vertical wire at x = 496.56 runs from a GND symbol (bars at y 84.04–86.89, widest bar
  nearest the stem) down through y = 97.96 — the **pin 1** wire — and continues to y = 107.45,
  J1's shell/mounting pins, which are therefore also grounded.
- A separate GND symbol at (447.5, 95.1) belongs to the battery-sense divider
  (`B+` → R15 20K → `EXIO_ADC` → R32 10K → GND), **not** to the connector. It is easy to
  misread as J1's ground.

### Still unverified — physical pin 1

The schematic gives the electrical net for each *pin number*. It does **not** establish which
physical contact of the PH2.0 housing is pin 1. That requires the board silkscreen, the
footprint, or a meter. Do not wire a cell from this table alone.

## Revision warning: the wiki prose describes a different charger

The wiki text — including its onboard-resources list and FAQ — describes an **SW6106**
charge/discharge IC. **The V4.0 schematic contains no SW6106.** V4.0 uses an **ETA6098**
charger. The wiki's own FAQ gives this away, referring to "the battery function (SW6106
scheme)".

Power-related parts actually present on V4.0: `ETA6098` (charger), `XL1509-ADJE1` (wide-input
buck, 7–36 V DC), `MP1658GTF-Z` (buck), `MBR230LSFT1G`, `B5819WS` ×2, `RB521S-30` ×2.

Consequently, treat every battery, charging, and I²C-fuel-gauge statement in the wiki as
describing V1–V3 until confirmed against the V4.0 sheet. This is the same class of
revision drift that cost most of Phase 0 (see [../../README.md](../../README.md)).

## Firmware scope

Nothing in this project reads, charges, or monitors a battery. There is no VBAT sense and no
`svc_power` — it is listed as unbuilt in [../../README.md](../../README.md). Brownout detection
sits at level 7 (`CONFIG_ESP_BROWNOUT_DET_LVL=7`), the most aggressive threshold, so a sagging
cell produces resets rather than a graceful shutdown.
