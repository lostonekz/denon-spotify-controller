# denon-spotify-controller

Wall-mounted Spotify + Denon AVR-X3800H controller.
ESP32-S3-Touch-LCD-4 (N16R8, 480×480 ST7701 on the parallel RGB bus, GT911 touch).

**Status: Phase 0 skeleton.** This repo exists to make the architecture in
[docs/](docs/) *falsifiable* — not to implement features. There is no UI, no
Spotify integration, and no Denon service yet, deliberately.

---

## What is actually here

| | |
|---|---|
| **Architecture docs** | [docs/](docs/) — 20 documents, 8 ADRs |
| **Enforcement** | 9 gates in [tools/](tools/), all dependency-free Python |
| **Gate self-test** | [tools/test_gates.py](tools/test_gates.py) — proves the gates fail when violated |
| **Host tests** | [test/host/](test/host/) — Denon MV codec, OTA admission policy |
| **Phase 0 spike** | [components/spike_phase0/](components/spike_phase0/) — the R1 measurement |

## Quick start

```bash
# Architecture gates — needs only Python 3, no pip install
python tools/run_all_checks.py --root .

# Prove the gates actually enforce (mutates a scratch clone, not your tree)
python tools/test_gates.py

# Host tests — needs a C compiler and CMake, no ESP-IDF
cmake -S test/host -B build/host
cmake --build build/host
ctest --test-dir build/host --output-on-failure

# Firmware / Phase 0 spike — needs ESP-IDF v5.5+ and hardware
idf.py set-target esp32s3
idf.py menuconfig     # set Phase 0 spike WiFi SSID/password and URL
idf.py build flash monitor
```

## The gates

Every architectural claim that can be mechanically checked, is. The rest is
just prose that decays.

| Gate | Enforces | Doc |
|---|---|---|
| `check_includes` | layer boundaries; the ui→hal glue exception at header granularity | ARCHITECTURE §1.2, §1.4 |
| `check_limits` | 400-line ceiling on source **and docs** | project rules |
| `check_lvgl_boundary` | no `lv_*` symbol outside `components/ui/` | ADR-0008 |
| `check_release` | dev key can never ship; signing can never be off in dev | SECURITY §1 |
| `check_partitions` | overlaps, alignment, A/B symmetry, `nvs_keys` reserved | PLATFORM §1 |
| `check_sdkconfig` | the R1 mitigations and OTA/eFuse policy, as assertions | MEMORY §3, SECURITY |
| `check_pins` | GPIO double-assignment, reserved and strapping pins | MODULES |
| `denon_mv_oracle` | the MV boundary table, via an independent implementation | RISKS R13 |
| `test_gates` | that all of the above actually fail when violated | — |

Three of these caught real bugs while this skeleton was being written: three
double-assigned GPIOs, an undeclared `app → spike` edge, and a checker that
was reporting success while walking an empty directory.

## Why the Denon MV codec is the first thing tested

`MV52` is 52.0 dB. `MV525` is 52.5 dB, not 525. `MVMAX 98` is a *maximum
report*, not a level — a naive `atoi(s+2)` on it yields 0. Getting any of these
wrong sends a wrong volume to an amplifier driving real speakers
([RISKS.md](docs/RISKS.md) R13).

So it is written twice: once in C, once in Python
([tools/denon_mv_oracle.py](tools/denon_mv_oracle.py)), from the protocol rather
than from each other. Both are checked against one shared vector table. A wrong
expected value has to be wrong identically in two languages to survive.

## Phase 0 is the gate on everything else

[RISKS.md R1](docs/RISKS.md): the ST7701 is on the parallel RGB bus, so panel
DMA reads the framebuffer from PSRAM continuously (~27 MB/s) in competition
with WiFi DMA, TLS, and JPEG decode. The symptoms appear *only* under combined
load — which means late, in front of the user, never on the bench.

[components/spike_phase0/](components/spike_phase0/) creates that load
deliberately and prints a pass/fail verdict against the thresholds the docs
commit to. It is throwaway code and says so.

**The architecture is not approved until the spike reports PASS on hardware.**
If it fails, the response is documented and ordered: larger bounce buffers →
50 Hz refresh → single framebuffer with partial refresh.

## Layout

```
docs/          architecture, ADRs, risk register, roadmap
tools/         CI gates (Python 3 stdlib only) + the MV oracle
main/          composition root — currently just launches the spike
components/
  bsp_board/     every GPIO in the project, exactly once
  svc_denon/     MV codec only (full service is Phase 3)
  svc_ota/       admission policy only (transport is Phase 2)
  spike_phase0/  THROWAWAY R1 measurement harness
test/host/     Unity tests — run on the dev machine, no hardware
```

## Not here yet, on purpose

UI, Spotify OAuth, the full Denon service, and the production security profile
are all out of scope for this phase. See [docs/ROADMAP.md](docs/ROADMAP.md) —
the sequencing is deliberate, and Denon comes before Spotify for reasons
explained there.
