# Retro-Go SD Core SDK — shared build rules
#
# The project Makefile (repo root) sets CORE_* variables, then:
#
#   include $(GNW_CORE_SDK)/Makefile
#
# Required before include:
#   CORE_NAME        short id (output: build/$(CORE_NAME)_core.elf)
#   CORE_ENTRY       C symbol entered after constructors (extern "C" if C++)
#   CORE_C_SOURCES   .c files (paths relative to the project Makefile cwd)
#
# Optional: CORE_C_INCLUDES, CORE_C_DEFS, CORE_CXX_SOURCES, CORE_ASM_SOURCES,
#           CORE_ASFLAGS, CORE_EXTRA_REDEFINE_SYMS, CORE_LDSCRIPT, CORE_LDLIBS,
#           CORE_EXTRA_SEGMENTS, GCC_PATH, OPT
#
# Extra ITCM/AHB segments: if your linker script defines the usual
# __CORE_ITCM_* / __CORE_AHB_* symbols and .core_itcm / .core_ahb sections,
# pack_core.py embeds them automatically. CORE_EXTRA_SEGMENTS remains only
# for producing intermediate build/<name>_core_<region>.bin files (optional).
#
# GNW_CORE_SDK defaults to the directory containing this Makefile.

ifeq ($(CORE_NAME),)
$(error CORE_NAME must be set before including sdk/Makefile)
endif
ifeq ($(CORE_ENTRY),)
$(error CORE_ENTRY must be set before including sdk/Makefile)
endif

GNW_CORE_SDK ?= $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
BUILD_DIR ?= build

CORE_LDSCRIPT ?= $(GNW_CORE_SDK)/ld/core_ram_emu.ld
CORE_ASFLAGS ?=
CORE_EXTRA_REDEFINE_SYMS ?=

#######################################
# toolchain
#######################################
PREFIX = arm-none-eabi-
ifdef GCC_PATH
CC   = $(GCC_PATH)/$(PREFIX)gcc
CXX  = $(GCC_PATH)/$(PREFIX)g++
AS   = $(GCC_PATH)/$(PREFIX)gcc -x assembler-with-cpp
CP   = $(GCC_PATH)/$(PREFIX)objcopy
SZ   = $(GCC_PATH)/$(PREFIX)size
else
CC   = $(PREFIX)gcc
CXX  = $(PREFIX)g++
AS   = $(PREFIX)gcc -x assembler-with-cpp
CP   = $(PREFIX)objcopy
SZ   = $(PREFIX)size
endif

# Quiet recipes by default (same pattern as firmware Makefile: V = @).
# Force full command echo with: make V=
V ?= @
ECHO ?= echo

#######################################
# MCU flags — MUST match firmware (hard-float AAPCS)
#######################################
CPU       = -mcpu=cortex-m7 -mtune=cortex-m7
FPU       = -mfpu=fpv5-d16
FLOAT_ABI = -mfloat-abi=hard
MCU       = $(CPU) -mthumb -mno-unaligned-access $(FPU) $(FLOAT_ABI)

OPT ?= -Os

INC_ROOT := $(GNW_CORE_SDK)/include

CORE_COMMON_C_DEFS := \
-DUSE_HAL_DRIVER \
-DSTM32H7B0xx \
-DSD_CARD=1 \
-DGNW_DISABLE_COMPRESSION \
-DIS_LITTLE_ENDIAN \
$(if $(filter -DCOVERFLOW=%,$(CORE_C_DEFS)),,-DCOVERFLOW=0) \
$(if $(filter -DCHEAT_CODES=%,$(CORE_C_DEFS)),,-DCHEAT_CODES=0) \
-DGNW_CORE_ENTRY_TARGET=$(CORE_ENTRY)

CORE_COMMON_C_INCLUDES := \
-I$(GNW_CORE_SDK)/src \
-I$(INC_ROOT)/Core/Inc \
-I$(INC_ROOT)/Core/Inc/retro-go \
-I$(INC_ROOT)/Core/Inc/porting \
-I$(INC_ROOT)/Core/Src/porting/lib \
-I$(INC_ROOT)/Core/Src/porting/lib/FatFs \
-I$(INC_ROOT)/retro-go-stm32/components/odroid \
-I$(INC_ROOT)/Drivers/STM32H7xx_HAL_Driver/Inc \
-I$(INC_ROOT)/Drivers/STM32H7xx_HAL_Driver/Inc/Legacy \
-I$(INC_ROOT)/Drivers/CMSIS/Device/ST/STM32H7xx/Include \
-I$(INC_ROOT)/Drivers/CMSIS/Include

# -g keeps DWARF in the ELF for crash PC/LR → source (addr2line). The packed
# .bin from objcopy does not include debug sections, so SD size is unchanged.
CFLAGS = $(MCU) $(OPT) -g -Wall -fdata-sections -ffunction-sections \
-fno-common -MMD -MP -MF"$(@:%.o=%.d)" \
$(CORE_COMMON_C_DEFS) $(CORE_C_DEFS) \
$(CORE_COMMON_C_INCLUDES) $(CORE_C_INCLUDES)

ASFLAGS = $(MCU) $(OPT) $(CORE_COMMON_C_DEFS) $(CORE_C_DEFS) $(CORE_ASFLAGS)

CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit

LDFLAGS = $(MCU) -nostdlib $(CORE_LDLIBS) -lgcc -T$(CORE_LDSCRIPT) -L$(GNW_CORE_SDK)/ld \
-Wl,-Map=$(BUILD_DIR)/$(CORE_NAME)_core.map,--cref -Wl,--gc-sections \
-Wl,--no-check-sections

#######################################
# objects
#######################################
BRIDGE_C_SOURCES := \
$(GNW_CORE_SDK)/src/gw_core_bridge.c \
$(GNW_CORE_SDK)/src/gw_core_i18n.c
BRIDGE_ASM_SOURCES := $(GNW_CORE_SDK)/src/gw_core_entry.S
REDEFINE_SYMS_FILE := $(GNW_CORE_SDK)/src/gw_core_bridge_redefine_syms.txt

ifneq ($(CORE_CXX_SOURCES),)
CORE_CXX_SOURCES += $(GNW_CORE_SDK)/src/gw_core_cxx_support.cpp
endif

ALL_C_SOURCES   := $(CORE_C_SOURCES) $(BRIDGE_C_SOURCES)
ALL_CXX_SOURCES := $(CORE_CXX_SOURCES)
ALL_ASM_SOURCES := $(CORE_ASM_SOURCES) $(BRIDGE_ASM_SOURCES)
C_OBJECTS   := $(addprefix $(BUILD_DIR)/,$(notdir $(ALL_C_SOURCES:.c=.o)))
CXX_OBJECTS := $(addprefix $(BUILD_DIR)/,$(notdir $(patsubst %.cc,%.o,$(patsubst %.cxx,%.o,$(patsubst %.cpp,%.o,$(ALL_CXX_SOURCES))))))
ASM_OBJECTS := $(addprefix $(BUILD_DIR)/,$(notdir $(ALL_ASM_SOURCES:.S=.o)))
BRIDGE_OBJECTS := $(addprefix $(BUILD_DIR)/,$(notdir $(BRIDGE_C_SOURCES:.c=.o)))
BRIDGE_NO_REDEFINE := $(BUILD_DIR)/gw_core_bridge.o
BRIDGE_ASM_OBJECTS := $(addprefix $(BUILD_DIR)/,$(notdir $(BRIDGE_ASM_SOURCES:.S=.o)))

define CORE_APPLY_EXTRA_REDEFINE
	if [ -n "$(CORE_EXTRA_REDEFINE_SYMS)" ]; then \
		$(CP) --redefine-syms=$(CORE_EXTRA_REDEFINE_SYMS) $1 ; \
	fi
endef

vpath %.c $(sort $(dir $(ALL_C_SOURCES)))
vpath %.cpp $(sort $(dir $(ALL_CXX_SOURCES)))
vpath %.cxx $(sort $(dir $(ALL_CXX_SOURCES)))
vpath %.cc $(sort $(dir $(ALL_CXX_SOURCES)))
vpath %.S $(sort $(dir $(ALL_ASM_SOURCES)))

TARGET_ELF := $(BUILD_DIR)/$(CORE_NAME)_core.elf
TARGET_BIN := $(BUILD_DIR)/$(CORE_NAME)_core.bin

CORE_EXTRA_SEGMENTS ?=
CORE_EXTRA_SEGMENT_REGIONS := $(foreach seg,$(CORE_EXTRA_SEGMENTS),$(word 1,$(subst :, ,$(seg))))
CORE_EXTRA_SEGMENT_BINS := $(foreach r,$(CORE_EXTRA_SEGMENT_REGIONS),$(BUILD_DIR)/$(CORE_NAME)_core_$(r).bin)

.PHONY: all clean
all: $(TARGET_BIN) $(CORE_EXTRA_SEGMENT_BINS)

$(BUILD_DIR):
	$(V)mkdir -p $(BUILD_DIR)

$(BRIDGE_OBJECTS): CFLAGS += -fno-builtin-memcpy -fno-builtin-memmove -fno-builtin-memset

$(BUILD_DIR)/%.o: %.c $(REDEFINE_SYMS_FILE) | $(BUILD_DIR)
	$(V)$(ECHO) [ CC ] $(notdir $<)
	$(V)$(CC) -c $(CFLAGS) $< -o $@
	$(V)if [ "$@" != "$(BRIDGE_NO_REDEFINE)" ]; then \
		$(CP) --redefine-syms=$(REDEFINE_SYMS_FILE) $@ ; \
		$(call CORE_APPLY_EXTRA_REDEFINE,$@) \
	fi

$(BUILD_DIR)/%.o: %.S $(REDEFINE_SYMS_FILE) | $(BUILD_DIR)
	$(V)$(ECHO) [ AS ] $(notdir $<)
	$(V)$(AS) -c $(ASFLAGS) $< -o $@
	$(V)if [ "$@" != "$(BRIDGE_ASM_OBJECTS)" ]; then \
		$(CP) --redefine-syms=$(REDEFINE_SYMS_FILE) $@ ; \
		$(call CORE_APPLY_EXTRA_REDEFINE,$@) \
	fi

$(BUILD_DIR)/%.o: %.cpp $(REDEFINE_SYMS_FILE) | $(BUILD_DIR)
	$(V)$(ECHO) [ CXX ] $(notdir $<)
	$(V)$(CXX) -c $(CXXFLAGS) $< -o $@
	$(V)$(CP) --redefine-syms=$(REDEFINE_SYMS_FILE) $@
	$(V)$(call CORE_APPLY_EXTRA_REDEFINE,$@)

$(BUILD_DIR)/%.o: %.cxx $(REDEFINE_SYMS_FILE) | $(BUILD_DIR)
	$(V)$(ECHO) [ CXX ] $(notdir $<)
	$(V)$(CXX) -c $(CXXFLAGS) $< -o $@
	$(V)$(CP) --redefine-syms=$(REDEFINE_SYMS_FILE) $@
	$(V)$(call CORE_APPLY_EXTRA_REDEFINE,$@)

$(BUILD_DIR)/%.o: %.cc $(REDEFINE_SYMS_FILE) | $(BUILD_DIR)
	$(V)$(ECHO) [ CXX ] $(notdir $<)
	$(V)$(CXX) -c $(CXXFLAGS) $< -o $@
	$(V)$(CP) --redefine-syms=$(REDEFINE_SYMS_FILE) $@
	$(V)$(call CORE_APPLY_EXTRA_REDEFINE,$@)

$(TARGET_ELF): $(C_OBJECTS) $(CXX_OBJECTS) $(ASM_OBJECTS) $(CORE_LDSCRIPT)
	$(V)$(ECHO) [ LD ] $(notdir $@)
	$(V)$(CC) $(C_OBJECTS) $(CXX_OBJECTS) $(ASM_OBJECTS) $(LDFLAGS) -o $@
	$(V)$(SZ) $@

$(TARGET_BIN): $(TARGET_ELF)
	$(V)$(ECHO) [ BIN ] $(notdir $@)
	$(V)$(CP) -O binary --only-section=.core_entry --only-section=.data $< $@

define CORE_EXTRA_SEGMENT_RULE
$(BUILD_DIR)/$(CORE_NAME)_core_$(word 1,$(subst :, ,$(1))).bin: $(TARGET_ELF)
	$$(V)$$(ECHO) [ BIN ] $$(notdir $$@)
	$$(V)$$(CP) -O binary --only-section=.$(word 2,$(subst :, ,$(1))) $$< $$@
endef
$(foreach seg,$(CORE_EXTRA_SEGMENTS),$(eval $(call CORE_EXTRA_SEGMENT_RULE,$(seg))))

clean::
	$(V)rm -rf $(BUILD_DIR)

# Host↔Docker remount: drop .d files whose absolute paths don't belong to
# this checkout (local /Users/... then `make docker`, or the reverse).
$(shell \
	if ls $(BUILD_DIR)/*.d >/dev/null 2>&1; then \
		sdk_abs=$$(cd $(GNW_CORE_SDK) && pwd); \
		if grep -E -l -- ' /|:[ ]+/' $(BUILD_DIR)/*.d >/dev/null 2>&1 && \
		   ! grep -F -q -- "$$sdk_abs" $(BUILD_DIR)/*.d; then \
			rm -f $(BUILD_DIR)/*.d; \
		fi; \
	fi \
)

-include $(wildcard $(BUILD_DIR)/*.d)
