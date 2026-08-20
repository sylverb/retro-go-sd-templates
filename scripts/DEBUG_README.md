# Debug symbols (crash PC/LR → function / line)

This archive matches a Retro-Go SD core or homebrew release build.

| File | Purpose |
|------|---------|
| `*_core.elf` | Linked image with DWARF (`-g`). Use for address resolution. |
| `*_core.map` | Linker map (symbol addresses, section layout). |

## Resolve a crash

From a device log, take the **PC** and **LR** (hex), then:

```bash
arm-none-eabi-addr2line -e example_core.elf -f -C -a 0x24012abc 0x24004567
```

Example output:

```
0x24012abc
app_main
/path/to/src/main.c:142
0x24004567
common_emu_frame_loop
…
```

Without a local toolchain, use the builder image:

```bash
docker run --rm -v "$PWD:/w" -w /w sylverb/retro-go-sd-builder:v1.5 \
  arm-none-eabi-addr2line -e example_core.elf -f -C -a 0x24012abc
```

If you have a checkout of the project template / core repo, you can also use:

```bash
python3 scripts/resolve_addr.py --elf example_core.elf 0x24012abc 0x24004567
```

The packed `.bin` on the SD card is stripped of DWARF; only this ELF is
useful for source-level crash investigation.
