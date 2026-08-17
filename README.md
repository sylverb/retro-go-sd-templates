# Retro-Go SD Core / Homebrew Template

Standalone SDK and starter project for building **one** external emulator
core **or** **one** GWHB homebrew for
[Game & Watch Retro-Go SD](https://github.com/sylverb/game-and-watch-retro-go-sd).

This repository is the project: clone or copy it, set `PROJECT_KIND`, customize
`src/main.c` / pack metadata, and ship a single `.bin`. Do not put several
emulators or homebrews in the same tree.

Both kinds share the same freestanding Cortex-M7 build (linked into
`RAM_EMU`, talking to the launcher **only** through `gw_firmware_abi_t`).
They differ in packaging and SD layout:


| | Dynamic core (`PROJECT_KIND=core`) | Homebrew (`PROJECT_KIND=homebrew`) |
|--|-----------------------------------|-------------------------------------|
| Packer | `sdk/tools/pack_core.py` (`CORE`) | `sdk/tools/pack_homebrew.py` (`GWHB`) |
| SD path | `/cores/<name>.bin` | `/roms/homebrew/<name>.bin` |
| Launcher | New system tab (dirname + extensions) | Homebrew tab |
| Assets | Pad/header 1bpp logos (`src/assets/`) | Optional JPEG cover (≤186×100, ≤10 KiB) |
| `src/main.c` | Loads the ROM given by the launcher | No ROM — `ACTIVE_FILE` is this `.bin` |

Headers, bridge trampolines, linker scripts, and packers are vendored under
`sdk/`. You do **not** need a firmware checkout to compile.

## Requirements

**Local build**

- `arm-none-eabi-gcc` (v10+, same family as the firmware; hard-float
  `fpv5-d16` is mandatory — ABI calling convention must match)
- GNU Make
- Python 3 + Pillow (`pip install -r requirements.txt`) for packaging
  logos / homebrew covers from PNG/BMP/JPEG

**Host SDL preview** (optional, Linux / macOS)

- Native C compiler (`cc` / clang / gcc)
- pkg-config
- SDL2 (`libsdl2-dev` / `brew install sdl2`) or SDL3 (`HOST_SDL=3`)

**Docker build** (no host toolchain)

- Docker
- Image [`sylverb/retro-go-sd-builder`](https://hub.docker.com/r/sylverb/retro-go-sd-builder)
  (same tag as the firmware repo, default `v1.5`)

## Quick start

Local (default = core):

```bash
make
# or: make PROJECT_KIND=homebrew
```

Docker:

```bash
make docker
make docker PROJECT_KIND=homebrew
```

Override the image tag if needed: `make docker RELEASE_VERSION=v1.5`.

Produces:

- **core:** `example.bin` → `/cores/example.bin`, test ROMs under `/roms/example/`
- **homebrew:** `ExampleHB.bin` → `/roms/homebrew/ExampleHB.bin`
  (optional override cover: `/covers/homebrew/ExampleHB.img`)

The skeleton draws a framebuffer with the ROM / file name, beeps a square
wave while a gameplay button is held, and wires save/load state, screenshot,
sleep wake-up, and SRAM hooks via `odroid_system_emu_init`. Replace the stubs
in `src/main.c` with your emulator or game.

Useful Docker targets:

- `make docker` — build + pack in the local builder image
- `make docker_pull` — refresh the image from Docker Hub
- `make docker_shell` — interactive shell in the same mount

## Host preview (SDL)

Compile the **same** `src/main.c` into a desktop binary for faster iteration
(no G&W flash cycle). This does not replace the ARM pack for the device.

```bash
make host                       # SDL2 → ./example_host
make host HOST_SDL=3            # SDL3 (needs sdl3.pc)
make host PROJECT_KIND=homebrew # → ./ExampleHB_host
./example_host                  # Esc / close window to quit
./example_host /path/to/rom.bin # optional ROM for cores (or HOST_ROM=…)
```

On macOS, if `pkg-config sdl2` fails, point it at Homebrew:

```bash
export PKG_CONFIG_PATH="$(brew --prefix)/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
```

Controls: arrows = D-pad, `Z`/`X` = B/A, Enter = Start, Shift = Select,
`A`/`S` = Y/X (core Start/Select on G&W). Scale with `HOST_SCALE=2` (default).

## Create your own core

1. Keep `PROJECT_KIND=core` (the default).
2. Edit the top of `Makefile`: `CORE_NAME`, `CORE_ENTRY`, `CORE_C_SOURCES`,
   and the `pack_core.py` metadata (`--system-name`, `--dirname`,
   `--extensions`, …).
3. Drop footer graphics in `src/assets/` and point `--pad-logo` / `--header-logo`
   at PNG or BMP files. Dark/opaque pixels become the lit 1bpp bits.
   Optional: `--logo-width` / `--logo-height` / `--logo-invert`.
4. If the core supports cheat files on the SD card, set `--cheat-ext`
   (`ggcodes`, `pceplus`, or `mcf`). Leave empty when unsupported.
5. Implement the loop in `src/main.c` (entry is `app_main` by default).
6. `make` → drop the `.bin` under `/cores/`.

## Create your own homebrew

1. Build with `PROJECT_KIND=homebrew`.
2. Edit `Makefile`: `CORE_NAME`, pack `--name` / `--version` / `--cover` /
   `--out`. You can drop the core-only pack recipe and `PROJECT_KIND_CORE`
   branches once you no longer need them.
3. Cover JPEG must decode ≤ **186×100** and be ≤ **10 KiB**. An on-disk
   `/covers/homebrew/<stem>.img` **overrides** the embedded cover.
4. Large assets that do not fit in RAM_EMU stay as **sibling files** under
   `/roms/homebrew/` and are opened via the ABI.
5. `make PROJECT_KIND=homebrew` → drop the `.bin` (and sidecars) under
   `/roms/homebrew/`.

Include order in `src/main.c`:

```c
#include "common.h"
#include "odroid_system.h"
/* … other firmware-style headers … */
#include "gw_core_bridge.h"   /* last — rewrites ACTIVE_FILE / common_emu_state */
```

Undefined references at link time usually mean a symbol is missing from
`sdk/src/gw_core_bridge_redefine_syms.txt` and/or lacks a `core_*` trampoline
in `sdk/src/gw_core_bridge.c`. If the symbol is not on the ABI yet, extend
the **firmware** ABI first, then refresh this SDK (see below).

## Layout

```
Makefile            Project build + pack + docker (PROJECT_KIND=core|homebrew)
src/                Project sources (main.c) and core logos (assets/)
sdk/
  include/          Vendored headers (ABI, odroid, CMSIS/HAL, FatFs, gwhb.h, …)
  src/              Bridge, entry trampoline, i18n, redefine-syms map
  ld/               RAM_EMU linker scripts (must match firmware);
                    start with ld/core_ram_emu.ld
  tools/            pack_core.py, pack_homebrew.py
  Makefile          Shared compile/link rules (included by the root Makefile)
scripts/            Sync helper
```

## ABI compatibility

Cores and homebrews embed `required_abi_version` and `required_abi_min_size`
(from `GW_CORE_BUILT_ABI_*` in the bridge). The firmware refuses to load a
binary that asks for a newer/larger ABI than it provides.

See `SDK_VERSION` for the snapshot this tree was cut from. After a released
ABI:

- **Append** a new function pointer at the end of `gw_firmware_abi_t` →
  usually no version bump; `required_abi_min_size` grows.
- **Change a ctl signature** or remove/reorder fields → bump
  `GW_FIRMWARE_ABI_VERSION`.
- **Add a new ctl op** without changing the C signature → bump version (or
  another capability flag) so binaries that need the op can require it.

While developing against unreleased firmware you may rebuild firmware +
binaries together without bumping.

## Refreshing the SDK from firmware

If you maintain this tree alongside a firmware checkout:

```bash
./scripts/sync_from_firmware.sh /path/to/game-and-watch-retro-go-sd
```

That re-copies headers (including `gwhb.h`), bridge sources, linker scripts,
`pack_core.py`, and `pack_homebrew.py`. Review the diff before committing.

## License

Build glue and the template are MIT (see `LICENSE`). Vendored files under
`sdk/include/` keep their upstream licenses (firmware / HAL / FatFs / etc.).
