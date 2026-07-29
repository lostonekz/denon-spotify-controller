# Local Development Setup

What to install to actually run the checks in this repo, rather than
round-tripping every change through CI. Three independent tiers, matching
[README.md](../README.md)'s quick start and the three CI jobs in
[.github/workflows/ci.yml](../.github/workflows/ci.yml).

| Tier | Needs | Verifies |
|---|---|---|
| Architecture gates | Python 3 stdlib only | layering, limits, partitions, sdkconfig policy, pins |
| Host tests | a C11 compiler + CMake | the Denon codec and OTA policy logic, on the dev machine |
| Firmware | ESP-IDF v5.5+ | the actual target compile, on real hardware timing/types |

Gates need nothing beyond Python. The other two are below.

---

## 1. Host test toolchain (Windows)

**The problem this section exists to solve:** `cmake -S test/host -B build_host`
with no flags fails on a stock Windows box. CMake defaults to the NMake
Makefiles generator and looks for `cl.exe` (MSVC), which isn't there unless
Visual Studio Build Tools is installed — a multi-GB detour we don't need.

The fix is a self-contained clang that targets MinGW/UCRT instead of MSVC, so
no Windows SDK is required:

```powershell
winget install --id Kitware.CMake
winget install --id Ninja-build.Ninja
winget install --id MartinStorsjo.LLVM-MinGW.UCRT
```

`MartinStorsjo.LLVM-MinGW.UCRT` matters specifically — plain `LLVM.LLVM`
defaults to the MSVC target and hits the same `cl.exe`/Windows-SDK wall
(`lld-link: error: could not open 'kernel32.lib'`, etc.). The MinGW build
targets `x86_64-w64-windows-gnu` and links against its own bundled runtime,
so it needs nothing else. It also ships the sanitizer runtimes, which is what
makes the ASan/UBSan tier below work without a separate install.

After installing, the binaries are under
`%LOCALAPPDATA%\Microsoft\WinGet\Packages\...`, not a fixed path — winget
versions the directory name. Resolve it once and add it to `PATH`:

```powershell
$mingw = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Directory |
    Where-Object Name -match "LLVM-MinGW" |
    Get-ChildItem -Recurse -Directory -Filter bin |
    Select-Object -First 1 -ExpandProperty FullName

[Environment]::SetEnvironmentVariable(
    "Path",
    "$mingw;C:\Program Files\CMake\bin;$env:Path",
    "User")
```

Open a new shell after that (PATH changes don't reach already-running
terminals). Verify:

```powershell
clang --version   # expect: Target: x86_64-w64-windows-gnu
cmake --version
```

**Build and run:**

```powershell
cmake -S test/host -B build_host -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug
cmake --build build_host --parallel
ctest --test-dir build_host --output-on-failure
```

`-G Ninja -DCMAKE_C_COMPILER=clang` are not optional flourishes on this
platform — they are what avoids the NMake/MSVC path entirely. Once `PATH` is
set, `ninja` is picked automatically as the fastest available generator; only
the explicit compiler needs stating; CMake won't infer clang over an absent
`cl.exe` on its own.

### Sanitizer build

Same compiler, same everything, plus flags — this is what the CI job
`host-tests-sanitizers` runs:

```powershell
cmake -S test/host -B build_asan -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -g"
cmake --build build_asan --parallel
ctest --test-dir build_asan --output-on-failure
```

Worth running before every push that touches `denon_codec.c` or
`ota_policy.c`: both parse attacker-influenced bytes off a socket, and this is
where an off-by-one actually gets caught rather than in the field.

## 1b. Host test toolchain (Linux / macOS)

Nothing special needed — plain `gcc`/`clang` and `cmake` from the system
package manager work with the default generator:

```bash
cmake -S test/host -B build_host
cmake --build build_host --parallel
ctest --test-dir build_host --output-on-failure
```

The Windows-specific flags above exist only because Windows has no default C
compiler; they're harmless but unnecessary elsewhere.

---

## 2. ESP-IDF (firmware / Phase 0 spike)

Needed only for the actual target compile — the architecture gates and host
tests don't touch it. This is the tier that catches toolchain-specific bugs
the other two structurally cannot: differing type widths
(`uint32_t` is `long unsigned int` on xtensa-esp32s3, `unsigned int` on host
gcc/clang, so `%u` format mismatches only show up here), missing includes
that a component's `REQUIRES` makes *available* but doesn't *add*, and real
linker/partition behavior.

```powershell
mkdir C:\esp
cd C:\esp
git clone -b v5.5 --recursive --depth 1 --shallow-submodules `
    https://github.com/espressif/esp-idf.git esp-idf-v5.5
cd esp-idf-v5.5
.\install.ps1 esp32s3
```

Pin the version to match `.github/workflows/ci.yml`'s `esp_idf_version` —
building against a different one defeats the point of local verification,
since a pass locally against the wrong version proves nothing about CI.

`install.ps1 esp32s3` (not the bare `install.ps1`) installs only the
xtensa-esp32s3 toolchain rather than every target ESP-IDF supports, which is
most of the download size.

**Every new shell session**, before using `idf.py`:

```powershell
C:\esp\esp-idf-v5.5\export.ps1
```

This is deliberately *not* persisted to `PATH` the way CMake/clang are above —
`export.ps1` also sets `IDF_PATH` and a bundled Python venv, and doing that
globally in every shell is how you get ESP-IDF's Python silently shadowing
the system one. Source it per-session instead.

**Build and run the Phase 0 spike:**

```powershell
idf.py set-target esp32s3
idf.py menuconfig     # Phase 0 spike WiFi SSID/password and download URL
idf.py build
```

The dev signing key (`tools/keys/dev_ota_signing.pem`) does not need
generating by hand — see [tools/keys/README.md](../tools/keys/README.md). CI
generates a fresh disposable one every run for the same reason.

Flashing and running the spike against real hardware is the actual Phase 0
deliverable ([ROADMAP.md](ROADMAP.md)) and needs the board in hand:

```powershell
idf.py flash monitor
```

### Post-link LVGL boundary check

The include-based pre-check runs as part of `tools/run_all_checks.py`
regardless of whether ESP-IDF is installed. The *symbol*-level check —
the one that actually proves no non-UI object references `lv_*`
([adr/0008](adr/0008-own-the-lvgl-glue.md)) — needs compiled objects:

```powershell
python tools/check_lvgl_boundary.py --root . --build-dir build
```

---

## Why this file exists

The README's original quick start was written and never run on a machine
without Visual Studio installed — the exact platform the project's own
primary development happens on. It failed on the first try. This file is the
record of what was actually verified to work, so the next contributor's first
`cmake` invocation succeeds instead of hitting an opaque `nmake` error.
