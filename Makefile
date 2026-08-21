# Retro-Go SD template — one project = one CORE or one GWHB homebrew.
#
#   make                  — build + pack (default: PROJECT_KIND=core)
#   make PROJECT_KIND=homebrew
#   make host             — Linux/macOS SDL binary (same src/main.c)
#   make host HOST_SDL=3  — same with SDL3
#   make docker           — same build inside Docker (no host toolchain)
#   make docker_shell     — interactive shell in the builder image
#
# Customize CORE_NAME / pack metadata below, then replace src/main.c.
# Verbose compiler lines: make V=

#######################################
# Project identity
#######################################
# core     → pack_core.py     → /cores/<name>.bin
# homebrew → pack_homebrew.py → /homebrews/<name>.bin
PROJECT_KIND ?= core

CORE_NAME  := example
CORE_ENTRY := app_main

CORE_C_SOURCES := \
src/main.c

# Relative path so Docker bind-mounts work (do NOT use $(abspath) — it
# bakes the host path into Make prerequisites / .d files). Do not name
# this SDK_ROOT: that env var is commonly set by Android SDK installs.
GNW_CORE_SDK ?= sdk
# Separate build trees so switching PROJECT_KIND does not reuse stale .o.
BUILD_DIR ?= build/$(PROJECT_KIND)

#######################################
# SDK bridge overrides (optional)
#######################################
# The SDK bridge (gw_core_bridge.c) provides default implementations for
# memcpy/memset/memmove/__aeabi_mem* and malloc/calloc/free/realloc.
# Define these to exclude the SDK versions and supply your own:
#
#   GW_CORE_BRIDGE_DISABLE_SDK_MEMCPY — exclude memcpy only.
#       Memmove stays routed through the SDK bridge (Doom/fastmem needs it).
#
#   GW_CORE_BRIDGE_DISABLE_SDK_MEMSET — exclude memset only.
#
#   GW_CORE_BRIDGE_DISABLE_SDK_MEMMOVE — exclude memmove too (requires your
#       core to provide memmove).
#
#   GW_CORE_BRIDGE_DISABLE_SDK_MEMOPS — back-compat: exclude the full memops
#       block (memcpy/memset/memmove + all __aeabi_mem* helpers).
#
#   GW_CORE_BRIDGE_DISABLE_SDK_MALLOC — exclude the malloc/calloc/free/
#       realloc wrappers that forward to the firmware ABI heap. Use this when
#       the core links its own allocator or needs a custom malloc/free path.
#
# To enable, add the define(s) to CORE_C_DEFS below, e.g.:
#   CORE_C_DEFS += -DGW_CORE_BRIDGE_DISABLE_SDK_MEMCPY
#   CORE_C_DEFS += -DGW_CORE_BRIDGE_DISABLE_SDK_MEMSET
#   CORE_C_DEFS += -DGW_CORE_BRIDGE_DISABLE_SDK_MALLOC

#######################################
# Kind-specific compile defs + packing
#######################################
ifeq ($(PROJECT_KIND),core)
# Match release-firmware layout of retro_emulator_file_t: COVERFLOW fields
# sit before cheat_* — CHEAT_CODES alone with COVERFLOW=0 misaligns pointers.
# MAX_CHEAT_CODES mirrors Makefile.common's release default.
CORE_C_DEFS := \
-DPROJECT_KIND_CORE=1 \
-DCOVERFLOW=1 \
-DCHEAT_CODES=1 \
-DMAX_CHEAT_CODES=13

PACKED_BIN  := $(CORE_NAME).bin
PAD_LOGO    := src/assets/pad.png
HEADER_LOGO := src/assets/header.png

else ifeq ($(PROJECT_KIND),homebrew)
CORE_C_DEFS := \
-DPROJECT_KIND_HOMEBREW=1

PACKED_BIN := ExampleHB.bin
HB_NAME    := Example Homebrew
# Compact coverflow tile (HW max is 186x100 — do not use full width by default).
COVER_JPG    := $(BUILD_DIR)/cover.jpg
COVER_WIDTH  ?= 128
COVER_HEIGHT ?= 96

else
$(error PROJECT_KIND must be 'core' or 'homebrew' (got '$(PROJECT_KIND)'))
endif

include $(GNW_CORE_SDK)/Makefile

PACK_CORE     := $(GNW_CORE_SDK)/tools/pack_core.py
PACK_HOMEBREW := $(GNW_CORE_SDK)/tools/pack_homebrew.py
GEN_COVER     := scripts/gen_homebrew_cover.py

#######################################
# Packed header version
#######################################
# gnw_core_meta_t / gwhb_meta_t only store major.minor.patch (0..255).
# CORE_VERSION is the full git describe string passed to the packers; they
# extract the leading vX.Y.Z (NOTAG / missing tags → 0.0.0).
# Override: make CORE_VERSION=v1.2.3
CORE_VERSION ?= $(shell git describe --tags --dirty 2>/dev/null || echo NOTAG)

#######################################
# Pack
#######################################
.PHONY: pack cover

ifeq ($(PROJECT_KIND),core)

pack: $(TARGET_BIN) $(PAD_LOGO) $(HEADER_LOGO)
	$(V)$(ECHO) [ PACK CORE ] $(PACKED_BIN) version=$(CORE_VERSION)
	$(V)python3 $(PACK_CORE) \
		--elf $(TARGET_ELF) --bin $(TARGET_BIN) \
		--system-name "Example Core" --dirname example \
		--extensions "bin" \
		--core-name "Example" \
		--version "$(CORE_VERSION)" \
		--cheat-ext ggcodes \
		--pad-logo $(PAD_LOGO) \
		--header-logo $(HEADER_LOGO) \
		--out $(PACKED_BIN)

else

.PHONY: cover
cover: $(COVER_JPG)

# Must stay ≤ gui.c COVER_MAX_WIDTH x COVER_MAX_HEIGHT (186x100) and
# COVER_SIZE (10 KiB) — oversized covers smash the HW JPEG scratch.
$(COVER_JPG): $(GEN_COVER)
	$(V)$(ECHO) [ COVER ] $(COVER_JPG) ($(COVER_WIDTH)x$(COVER_HEIGHT))
	$(V)python3 $(GEN_COVER) \
		--out $(COVER_JPG) \
		--title "$(HB_NAME)" \
		--width $(COVER_WIDTH) \
		--height $(COVER_HEIGHT)

pack: $(TARGET_BIN) $(COVER_JPG)
	$(V)$(ECHO) [ PACK GWHB ] $(PACKED_BIN) version=$(CORE_VERSION)
	$(V)python3 $(PACK_HOMEBREW) \
		--elf $(TARGET_ELF) --bin $(TARGET_BIN) \
		--name "$(HB_NAME)" --version "$(CORE_VERSION)" \
		--cover $(COVER_JPG) \
		--out $(PACKED_BIN)

endif

all: pack

# Read-only helpers for CI / scripts (make print-PROJECT_KIND, etc.).
.PHONY: print-PROJECT_KIND print-PACKED_BIN print-CORE_NAME print-DOCKER_IMAGE \
	print-TARGET_ELF print-TARGET_MAP print-CORE_VERSION
print-PROJECT_KIND:
	@echo $(PROJECT_KIND)
print-PACKED_BIN:
	@echo $(PACKED_BIN)
print-CORE_NAME:
	@echo $(CORE_NAME)
print-DOCKER_IMAGE:
	@echo $(DOCKER_IMAGE)
print-TARGET_ELF:
	@echo $(TARGET_ELF)
print-TARGET_MAP:
	@echo $(BUILD_DIR)/$(CORE_NAME)_core.map
print-CORE_VERSION:
	@echo $(CORE_VERSION)

clean::
	$(V)rm -f $(PACKED_BIN)
ifeq ($(PROJECT_KIND),homebrew)
	$(V)rm -f $(COVER_JPG)
endif

#######################################
# Docker (same image as firmware repo)
#######################################
.PHONY: docker docker_pull docker_shell

RELEASE_VERSION ?= v1.5
DOCKER_REPOSITORY ?= sylverb/retro-go-sd-builder
DOCKER_IMAGE ?= $(DOCKER_REPOSITORY):$(RELEASE_VERSION)

DOCKER_TTY_FLAG := $(shell if [ -t 0 ]; then echo -it; else echo; fi)
# Host UID so build/ artifacts are not root-owned on the bind mount.
DOCKER_USER := $(shell id -u):$(shell id -g)
DOCKER_RUN := docker run --rm $(DOCKER_TTY_FLAG) \
	--user $(DOCKER_USER) \
	-v "$(CURDIR):/opt/workdir" \
	-w /opt/workdir \
	$(DOCKER_IMAGE)

# Compile inside the published builder image (uses the local copy).
# Refresh with `make docker_pull` when you want a newer digest for the tag.
docker:
	$(V)$(ECHO) "[ DOCKER ]" $(DOCKER_IMAGE) "PROJECT_KIND=$(PROJECT_KIND)"
	$(V)$(DOCKER_RUN) make --no-print-directory -j$$(nproc) PROJECT_KIND=$(PROJECT_KIND)

docker_pull:
	$(V)$(ECHO) "[ PULL ]" $(DOCKER_IMAGE)
	$(V)docker pull $(DOCKER_IMAGE)

# Interactive shell with the same image / mount as `make docker`.
docker_shell:
	$(DOCKER_RUN) bash

#######################################
# Host SDL (Linux / macOS)
#######################################
include host/Makefile.host
