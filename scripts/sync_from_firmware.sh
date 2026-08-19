#!/usr/bin/env bash
# Refresh vendored SDK files from a Game & Watch Retro-Go SD checkout.
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 /path/to/game-and-watch-retro-go-sd" >&2
  exit 1
fi

FW=$(cd "$1" && pwd)
ROOT=$(cd "$(dirname "$0")/.." && pwd)
INC_ROOT="$ROOT/sdk/include"

echo "Firmware: $FW"
echo "SDK root: $ROOT"

INC=(
  -I"$FW/Core/Inc"
  -I"$FW/Core/Inc/retro-go"
  -I"$FW/Core/Inc/porting"
  -I"$FW/Core/Src/porting/core_common"
  -I"$FW/Core/Src/porting/lib"
  -I"$FW/Core/Src/porting/lib/FatFs"
  -I"$FW/retro-go-stm32/components/odroid"
  -I"$FW/Drivers/STM32H7xx_HAL_Driver/Inc"
  -I"$FW/Drivers/STM32H7xx_HAL_Driver/Inc/Legacy"
  -I"$FW/Drivers/CMSIS/Device/ST/STM32H7xx/Include"
  -I"$FW/Drivers/CMSIS/Include"
)
DEFS=(-DUSE_HAL_DRIVER -DSTM32H7B0xx -DSD_CARD=1 -DGNW_DISABLE_COMPRESSION
      -DIS_LITTLE_ENDIAN -DCOVERFLOW=0 -DCHEAT_CODES=0
      -DGNW_CORE_ENTRY_TARGET=app_main)

rm -rf "$INC_ROOT"
mkdir -p "$INC_ROOT"

DEPS=$(arm-none-eabi-gcc -M "${DEFS[@]}" "${INC[@]}" \
  -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard \
  "$FW/Core/Src/porting/core_common/gw_core_bridge.c" \
  | tr ' ' '\n' | grep -E '\.(h|hpp)$' | sort -u)

while IFS= read -r h; do
  [[ -z "$h" ]] && continue
  case "$h" in
    "$FW"/*)
      rel="${h#$FW/}"
      dest="$INC_ROOT/$rel"
      mkdir -p "$(dirname "$dest")"
      cp "$h" "$dest"
      ;;
  esac
done <<< "$DEPS"

for extra in \
  Core/Inc/retro-go/gnw_core_meta.h \
  Core/Inc/retro-go/gwhb.h \
  Core/Inc/gw_malloc.h \
  Core/Inc/heap.hpp
do
  [[ -f "$FW/$extra" ]] || continue
  dest="$INC_ROOT/$extra"
  mkdir -p "$(dirname "$dest")"
  cp "$FW/$extra" "$dest"
done

# Drop bridge headers from the include tree (live in sdk/src/)
rm -rf "$INC_ROOT/Core/Src/porting/core_common"

for f in gw_core_bridge.c gw_core_bridge.h gw_core_entry.S \
         gw_core_bridge_redefine_syms.txt gw_core_i18n.c gw_core_i18n.h \
         gw_core_cxx_support.cpp; do
  cp "$FW/Core/Src/porting/core_common/$f" "$ROOT/sdk/src/"
done

cp "$FW/ld/gnw_ram_emu.ld" "$ROOT/sdk/ld/gnw_ram_emu.ld"
cp "$FW/ld/gnw_itcm_core.ld" "$ROOT/sdk/ld/gnw_itcm_core.ld"
cp "$FW/ld/gnw_ram_uc_core.ld" "$ROOT/sdk/ld/gnw_ram_uc_core.ld"
cp "$FW/tools/pack_core.py" "$ROOT/sdk/tools/pack_core.py"
cp "$FW/tools/pack_homebrew.py" "$ROOT/sdk/tools/pack_homebrew.py"

ABI_VER=$(grep -E '#define\s+GW_FIRMWARE_ABI_VERSION' \
  "$INC_ROOT/Core/Inc/retro-go/gw_firmware_abi.h" | awk '{print $3}' | tr -d 'u')
cat > "$ROOT/SDK_VERSION" << META
# Snapshot metadata for this SDK tree (informational).
FIRMWARE_ABI_VERSION=$ABI_VER
SOURCE=$FW
SYNC_DATE=$(date -u +%Y-%m-%dT%H:%M:%SZ)
META

echo "Synced. ABI version=$ABI_VER"
echo "Review git diff before committing. Linker script core_ram_emu.ld is"
echo "kept as the SDK copy (not overwritten) — merge manually if needed."
