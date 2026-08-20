#!/usr/bin/env python3
"""Resolve PC/LR (or any) crash addresses against a core/homebrew ELF.

Typical crash log from Retro-Go SD shows program counter and link register.
Point this script at the matching release ELF (from the *-debug.zip) and
pass those hex addresses:

  python3 resolve_addr.py --elf example_core.elf 0x24012abc 0x24004567
  python3 resolve_addr.py 0x24012abc          # uses *.elf next to this script

Requires arm-none-eabi-addr2line on PATH (or set ADDR2LINE=…), e.g. from the
retro-go-sd-builder Docker image or a local GNU Arm Embedded toolchain.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent


def find_default_elf() -> Path | None:
    candidates = sorted(SCRIPT_DIR.glob("*_core.elf"))
    if len(candidates) == 1:
        return candidates[0]
    if len(candidates) > 1:
        print(
            f"multiple ELFs next to script; pass --elf explicitly:\n"
            + "\n".join(f"  {p.name}" for p in candidates),
            file=sys.stderr,
        )
    return None


def find_addr2line(explicit: str | None) -> str:
    if explicit:
        return explicit
    env = os.environ.get("ADDR2LINE")
    if env:
        return env
    for name in ("arm-none-eabi-addr2line", "llvm-addr2line", "addr2line"):
        path = shutil.which(name)
        if path:
            return path
    raise SystemExit(
        "addr2line not found. Install a GNU Arm Embedded toolchain, or run:\n"
        "  docker run --rm -v \"$PWD:/w\" -w /w "
        "sylverb/retro-go-sd-builder:v1.5 "
        "arm-none-eabi-addr2line -e <elf> -f -C -a <addr>…\n"
        "Or set ADDR2LINE=/path/to/arm-none-eabi-addr2line"
    )


def normalize_addr(raw: str) -> str:
    s = raw.strip().lower()
    if s.startswith("0x"):
        s = s[2:]
    if not s or any(c not in "0123456789abcdef" for c in s):
        raise SystemExit(f"invalid address: {raw!r}")
    return "0x" + s


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Map crash PC/LR addresses to function + source line via ELF DWARF."
    )
    parser.add_argument(
        "--elf",
        type=Path,
        help="ELF with debug symbols (default: sole *_core.elf next to this script)",
    )
    parser.add_argument(
        "--tool",
        help="addr2line binary (default: ADDR2LINE env, then arm-none-eabi-addr2line)",
    )
    parser.add_argument(
        "addresses",
        nargs="+",
        help="hex addresses from the crash (PC, LR, …), with or without 0x",
    )
    args = parser.parse_args()

    elf = args.elf
    if elf is None:
        elf = find_default_elf()
    if elf is None:
        raise SystemExit("no ELF found; pass --elf path/to/*_core.elf")
    if not elf.is_file():
        raise SystemExit(f"ELF not found: {elf}")

    tool = find_addr2line(args.tool)
    addrs = [normalize_addr(a) for a in args.addresses]

    cmd = [tool, "-e", str(elf), "-f", "-C", "-a", *addrs]
    try:
        proc = subprocess.run(cmd, check=False, text=True, capture_output=True)
    except OSError as exc:
        raise SystemExit(f"failed to run {tool}: {exc}") from exc

    if proc.stdout:
        sys.stdout.write(proc.stdout)
    if proc.stderr:
        sys.stderr.write(proc.stderr)
    if proc.returncode != 0:
        raise SystemExit(proc.returncode)


if __name__ == "__main__":
    main()
