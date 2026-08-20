#!/usr/bin/env python3
"""Stage Retro-Go SD release assets for the active project kind.

Reads PROJECT_KIND and PACKED_BIN from the root Makefile, builds:
  1. SD install zip:   <stem>-<tag>.zip       → cores|homebrews/<packed.bin>
  2. Debug symbols zip: <stem>-<tag>-debug.zip → ELF, map, README

Extracts release notes from CHANGELOG.md for the requested tag.

Usage:
  python3 scripts/stage_release.py --bin example.bin --tag v1.0.0 --out release \\
      --elf build/core/example_core.elf --map build/core/example_core.map
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAKEFILE = ROOT / "Makefile"
DEFAULT_CHANGELOG = ROOT / "CHANGELOG.md"
DEBUG_README = ROOT / "scripts" / "DEBUG_README.md"

MAKE_VARS = (
    "PROJECT_KIND",
    "PACKED_BIN",
    "CORE_NAME",
    "DOCKER_IMAGE",
    "TARGET_ELF",
    "TARGET_MAP",
)

HEADING_RE = re.compile(
    r"^##\s*(?:\[(?P<bracket>[^\]]+)\]|(?P<plain>[^\s#]+))(?:\s*-\s*(?P<date>.+))?\s*$",
    re.MULTILINE,
)


def read_make_vars() -> dict[str, str]:
    cmd = ["make", "-f", str(MAKEFILE), "--no-print-directory"]
    for var in MAKE_VARS:
        cmd.append(f"print-{var}")

    try:
        # Keep stderr separate — Makefile $(warning) lines must not pollute values.
        proc = subprocess.run(
            cmd,
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
    except OSError as exc:
        raise SystemExit(f"failed to run make: {exc}") from exc

    if proc.returncode != 0:
        sys.stderr.write(proc.stderr or proc.stdout or "")
        raise SystemExit(f"failed to read Makefile variables from {MAKEFILE}")

    if proc.stderr:
        sys.stderr.write(proc.stderr)

    values = [line for line in proc.stdout.splitlines() if line.strip()]
    # Defensive: if anything else leaked to stdout, keep the last N lines.
    if len(values) > len(MAKE_VARS):
        values = values[-len(MAKE_VARS) :]
    if len(values) != len(MAKE_VARS):
        raise SystemExit(
            f"expected {len(MAKE_VARS)} Makefile values, got {len(values)}:\n{proc.stdout}"
        )
    return dict(zip(MAKE_VARS, values, strict=True))


def sd_subdir(project_kind: str) -> str:
    if project_kind == "core":
        return "cores"
    if project_kind == "homebrew":
        return "homebrews"
    raise SystemExit(f"unsupported PROJECT_KIND: {project_kind!r} (expected core or homebrew)")


def slug(tag: str) -> str:
    return re.sub(r"[^A-Za-z0-9._-]+", "-", tag).strip("-") or "release"


def extract_changelog_section(changelog_path: Path, tag: str) -> str:
    if not changelog_path.is_file():
        raise SystemExit(f"CHANGELOG not found: {changelog_path}")

    text = changelog_path.read_text(encoding="utf-8")
    matches = list(HEADING_RE.finditer(text))
    for index, match in enumerate(matches):
        version = (match.group("bracket") or match.group("plain") or "").strip()
        if version != tag:
            continue
        start = match.end()
        end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
        body = text[start:end].strip()
        if not body:
            raise SystemExit(f"CHANGELOG section for {tag!r} is empty")
        return body

    raise SystemExit(
        "no CHANGELOG section for tag "
        f"{tag!r}; add '## [{tag}] - YYYY-MM-DD' before pushing the tag"
    )


def build_release_notes(
    *,
    tag: str,
    changelog_body: str,
    project_kind: str,
    packed_name: str,
    sd_dir: str,
    core_name: str,
    docker_image: str,
    archive_name: str,
    debug_archive_name: str,
) -> str:
    sdk_version = (ROOT / "SDK_VERSION").read_text(encoding="utf-8").strip()
    install_path = f"/{sd_dir}/{packed_name}"

    lines = [
        f"# {tag}",
        "",
        changelog_body,
        "",
        "---",
        "",
        f"- Project kind: `{project_kind}`",
        f"- Packed binary: `{packed_name}`",
        f"- SD install path: `{install_path}`",
        f"- Release archive: `{archive_name}` (unzip onto the SD root)",
        f"- Debug archive: `{debug_archive_name}` (ELF + linker map)",
        f"- Built with: `{docker_image}`",
        "",
        "Crash PC/LR → source (needs `arm-none-eabi-addr2line`):",
        "",
        "```bash",
        f"unzip {debug_archive_name}",
        "arm-none-eabi-addr2line -e <name>_core.elf -f -C -a 0x<PC> 0x<LR>",
        "```",
        "",
        "Or from a checkout of this repo: `python3 scripts/resolve_addr.py --elf …`",
        "",
        "```",
        sdk_version,
        "```",
    ]
    if project_kind == "core":
        lines.append(f"- Test ROMs: `/roms/{core_name}/`")
    else:
        stem = Path(packed_name).stem
        lines.append(f"- Optional cover: `/covers/homebrew/{stem}.img`")

    return "\n".join(lines) + "\n"


def write_zip(archive_path: Path, members: list[tuple[Path, str]]) -> None:
    if archive_path.exists():
        archive_path.unlink()
    with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for src, arcname in members:
            zf.write(src, arcname)


def stage_release(
    *,
    bin_path: Path,
    tag: str,
    out_dir: Path,
    changelog_path: Path,
    docker_image: str | None,
    elf_path: Path | None,
    map_path: Path | None,
) -> None:
    cfg = read_make_vars()
    project_kind = cfg["PROJECT_KIND"]
    packed_name = cfg["PACKED_BIN"]
    core_name = cfg["CORE_NAME"]
    resolved_docker = docker_image or cfg.get("DOCKER_IMAGE") or "sylverb/retro-go-sd-builder:v1.5"

    if not bin_path.is_file():
        raise SystemExit(f"packed binary not found: {bin_path}")

    elf = elf_path or (ROOT / cfg["TARGET_ELF"])
    if not elf.is_absolute():
        elf = ROOT / elf
    map_file = map_path or (ROOT / cfg["TARGET_MAP"])
    if not map_file.is_absolute():
        map_file = ROOT / map_file

    if not elf.is_file():
        raise SystemExit(f"ELF not found: {elf}")
    if not map_file.is_file():
        raise SystemExit(f"linker map not found: {map_file}")
    if not DEBUG_README.is_file():
        raise SystemExit(f"debug readme not found: {DEBUG_README}")

    changelog_body = extract_changelog_section(changelog_path, tag)

    sd_dir = sd_subdir(project_kind)
    out_dir.mkdir(parents=True, exist_ok=True)
    sd_root = out_dir / sd_dir
    sd_root.mkdir(parents=True, exist_ok=True)

    sd_bin = sd_root / packed_name
    shutil.copy2(bin_path, sd_bin)

    stem = Path(packed_name).stem
    tag_slug = slug(tag)

    archive_name = f"{stem}-{tag_slug}.zip"
    archive_path = out_dir / archive_name
    write_zip(archive_path, [(sd_bin, f"{sd_dir}/{packed_name}")])

    debug_archive_name = f"{stem}-{tag_slug}-debug.zip"
    debug_archive_path = out_dir / debug_archive_name
    write_zip(
        debug_archive_path,
        [
            (elf, elf.name),
            (map_file, map_file.name),
            (DEBUG_README, "README.md"),
        ],
    )

    notes_path = out_dir / "RELEASE_NOTES.md"
    notes_path.write_text(
        build_release_notes(
            tag=tag,
            changelog_body=changelog_body,
            project_kind=project_kind,
            packed_name=packed_name,
            sd_dir=sd_dir,
            core_name=core_name,
            docker_image=resolved_docker,
            archive_name=archive_name,
            debug_archive_name=debug_archive_name,
        ),
        encoding="utf-8",
    )

    # GitHub Release assets: install zip + debug zip only (no loose .bin).
    release_files = [archive_path, debug_archive_path]
    files_path = out_dir / "release-files.txt"
    files_path.write_text(
        "\n".join(p.name for p in release_files) + "\n",
        encoding="utf-8",
    )

    print(f"release_title={tag}")
    print(f"project_kind={project_kind}")
    print(f"packed_bin={packed_name}")
    print(f"sd_path=/{sd_dir}/{packed_name}")
    print(f"archive={archive_path}")
    print(f"debug_archive={debug_archive_path}")
    print(f"notes={notes_path}")
    print(f"files={files_path}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--bin",
        dest="bin_path",
        type=Path,
        help="packed .bin to stage (default: PACKED_BIN from Makefile, relative to repo root)",
    )
    parser.add_argument(
        "--elf",
        dest="elf_path",
        type=Path,
        help="linked ELF with debug symbols (default: TARGET_ELF from Makefile)",
    )
    parser.add_argument(
        "--map",
        dest="map_path",
        type=Path,
        help="linker map (default: TARGET_MAP from Makefile)",
    )
    parser.add_argument("--tag", required=True, help="release tag (e.g. v1.0.0)")
    parser.add_argument(
        "--out",
        type=Path,
        default=Path("release"),
        help="output directory (default: release/)",
    )
    parser.add_argument(
        "--changelog",
        type=Path,
        default=DEFAULT_CHANGELOG,
        help="changelog file (default: CHANGELOG.md)",
    )
    parser.add_argument(
        "--docker-image",
        help="builder image string for release notes (default: Makefile DOCKER_IMAGE)",
    )
    args = parser.parse_args()

    cfg = read_make_vars()
    bin_path = args.bin_path or (ROOT / cfg["PACKED_BIN"])
    if not bin_path.is_absolute():
        bin_path = ROOT / bin_path

    changelog_path = args.changelog
    if not changelog_path.is_absolute():
        changelog_path = ROOT / changelog_path

    stage_release(
        bin_path=bin_path,
        tag=args.tag,
        out_dir=(ROOT / args.out).resolve() if not args.out.is_absolute() else args.out,
        changelog_path=changelog_path,
        docker_image=args.docker_image,
        elf_path=args.elf_path,
        map_path=args.map_path,
    )


if __name__ == "__main__":
    main()
