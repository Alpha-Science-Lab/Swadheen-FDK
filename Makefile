# ==============================================================================
# Copyright (c) 2024 Tobias Scheipel, David Beikircher, Florian Riedl
# Embedded Architectures & Systems Group, Graz University of Technology
# SPDX-License-Identifier: MIT
#
# Modified by Monjurul Islam Bhuiyan
# Organization: Alpha Science Lab
# ==============================================================================


# ==============================================================================
#                         Build Directory Validation
# ==============================================================================

# GNU Make has issues when building from directories containing spaces.
ifneq ($(words $(CURDIR)),1)
    $(error Unsupported: Cannot build in directories having spaces: '$(CURDIR)')
endif


# ==============================================================================
#                              Toolchain Configuration
# ==============================================================================

CC           = riscv32-unknown-elf-gcc
OBJCOPY      = riscv32-unknown-elf-objcopy
OBJDUMP      = riscv32-unknown-elf-objdump
PYTHON       = python3


# ==============================================================================
#                              Build Configuration
# ==============================================================================

BOOTLOADER   ?= bootloader
M_EXT        ?= 0
FIRMWARE     ?= running_led
TTYPORT      ?= /dev/ttyUSB1
BAUD         ?= 115200

EXAMPLES     = examples
STD_LIB_DIR  = std
C_DIR        = $(EXAMPLES)/c
ASM_DIR      = $(EXAMPLES)/asm
BUILD_DIR    = build


# ==============================================================================
#                              RISC-V ISA Configuration
# ==============================================================================

ifeq ($(M_EXT),1)
# M extension support
RISCV_ARCH   = -march=rv32im_zicsr_zifencei -mabi=ilp32

else
# M extension ignored 
RISCV_ARCH   = -march=rv32i_zicsr_zifencei -mabi=ilp32

endif


# ==============================================================================
#                                 Clean Project
# ==============================================================================

.PHONY: clean

clean:
	rm -rf $(BUILD_DIR)


# ==============================================================================
#                              Assembly Examples
# ==============================================================================

# Discover all assembly examples
ASM_EXAMPLES      = $(wildcard $(ASM_DIR)/*.s)
ASM_EXAMPLE_NAMES = $(patsubst $(ASM_DIR)/%.s,$(ASM_DIR)/%,$(ASM_EXAMPLES))


# ------------------------------------------------------------------------------
# Compile Assembly to ELF
# ------------------------------------------------------------------------------

$(BUILD_DIR)/$(ASM_DIR)/%/out.elf: $(ASM_DIR)/%.s $(STD_LIB_DIR)/hades-v.ld
	@mkdir -p $(BUILD_DIR)/$(ASM_DIR)/$*

	$(CC) \
		$(RISCV_ARCH) \
		-nostdlib \
		-nostartfiles \
		-T $(STD_LIB_DIR)/hades-v.ld \
		-o $@ \
		$<

	$(OBJDUMP) -d -r -t -S $@ > $(@:.elf=.dis)

# ------------------------------------------------------------------------------
# Convert ELF to Intel HEX
# ------------------------------------------------------------------------------

$(BUILD_DIR)/$(ASM_DIR)/%/out.hex: $(BUILD_DIR)/$(ASM_DIR)/%/out.elf
	$(OBJCOPY) -O ihex $< $@

# ------------------------------------------------------------------------------
# Convert ELF to Raw Binary
# ------------------------------------------------------------------------------

$(BUILD_DIR)/$(ASM_DIR)/%/out.bin: $(BUILD_DIR)/$(ASM_DIR)/%/out.elf
	$(OBJCOPY) -O binary $< $@


# ------------------------------------------------------------------------------
# Convert Binary to Verilog Memory Initialization File
# ------------------------------------------------------------------------------

$(BUILD_DIR)/$(ASM_DIR)/%/init.mem: $(BUILD_DIR)/$(ASM_DIR)/%/out.bin
	$(OBJCOPY) \
		-I binary \
		-O verilog \
		--verilog-data-width 4 \
		--reverse-bytes=4 \
		$< $@


# ------------------------------------------------------------------------------
# Assembly Build Targets
# ------------------------------------------------------------------------------

.PHONY: $(ASM_EXAMPLE_NAMES)

$(ASM_EXAMPLE_NAMES): $(ASM_DIR)/%: \
	$(BUILD_DIR)/$(ASM_DIR)/%/out.elf \
	$(BUILD_DIR)/$(ASM_DIR)/%/out.hex \
	$(BUILD_DIR)/$(ASM_DIR)/%/init.mem
	@echo "Done!"


# ==============================================================================
#                                C Examples
# ==============================================================================

# Discover all C examples.
C_EXAMPLES      = $(wildcard $(C_DIR)/*.c)
C_EXAMPLE_NAMES = $(patsubst $(C_DIR)/%.c,$(C_DIR)/%,$(C_EXAMPLES))


# ==============================================================================
#                              Standard Library
# ==============================================================================

# Discover standard library source files.
C_LIB_SRC = $(wildcard $(STD_LIB_DIR)/src/*.c)

# Map source files to object files in the build directory.
C_LIB_OBJ = $(patsubst \
	$(STD_LIB_DIR)/src/%.c,\
	$(BUILD_DIR)/$(STD_LIB_DIR)/%.o,\
	$(C_LIB_SRC))


# ------------------------------------------------------------------------------
# Compile Standard Library Sources
# ------------------------------------------------------------------------------

$(BUILD_DIR)/$(STD_LIB_DIR)/%.o: $(STD_LIB_DIR)/src/%.c
	@mkdir -p $(BUILD_DIR)/$(STD_LIB_DIR)

	$(CC) \
		$(RISCV_ARCH) \
		-fdata-sections \
		-ffunction-sections \
		-I $(STD_LIB_DIR)/include \
		-c \
		-o $@ \
		$<


# ==============================================================================
#                               C Example Build
# ==============================================================================

# ------------------------------------------------------------------------------
# Compile C Source to Object File
# ------------------------------------------------------------------------------

$(BUILD_DIR)/$(C_DIR)/%/out.o: $(C_DIR)/%.c
	@mkdir -p $(BUILD_DIR)/$(C_DIR)/$*

	$(CC) \
		$(RISCV_ARCH) \
		-fdata-sections \
		-ffunction-sections \
		-I $(STD_LIB_DIR)/include \
		-c \
		-o $@ \
		$<


# ------------------------------------------------------------------------------
# Link Object Files to ELF
# ------------------------------------------------------------------------------

$(BUILD_DIR)/$(C_DIR)/%/out.elf: \
	$(BUILD_DIR)/$(C_DIR)/%/out.o \
	$(C_LIB_OBJ) \
	$(STD_LIB_DIR)/hades-v.ld

	$(CC) \
		$(RISCV_ARCH) \
		-nostdlib \
		-nostartfiles \
		-T $(STD_LIB_DIR)/hades-v.ld \
		-o $@ \
		$< \
		$(C_LIB_OBJ) \
		-lgcc \
		-Wl,--no-warn-rwx-segments \
		-Wl,--gc-sections


# ------------------------------------------------------------------------------
# Convert ELF to Intel HEX
#
# Used for transferring the program to the bootloader
# ------------------------------------------------------------------------------

$(BUILD_DIR)/$(C_DIR)/%/out.hex: $(BUILD_DIR)/$(C_DIR)/%/out.elf
	$(OBJCOPY) -O ihex $< $@


# ------------------------------------------------------------------------------
# Convert ELF to Raw Binary
#
# Intermediate step for generating the Verilog memory initialization file
# ------------------------------------------------------------------------------

$(BUILD_DIR)/$(C_DIR)/%/out.bin: $(BUILD_DIR)/$(C_DIR)/%/out.elf
	$(OBJCOPY) -O binary $< $@


# ------------------------------------------------------------------------------
# Convert Binary to Verilog Memory Initialization File
#
# Used for simulation and synthesis.
# ------------------------------------------------------------------------------

$(BUILD_DIR)/$(C_DIR)/%/init.mem: $(BUILD_DIR)/$(C_DIR)/%/out.bin
	$(OBJCOPY) \
		-I binary \
		-O verilog \
		-S \
		--verilog-data-width 4 \
		--reverse-bytes=4 \
		$< $@


# ------------------------------------------------------------------------------
# Generate Disassembly
#
# Used for debugging and inspecting the generated executable
# ------------------------------------------------------------------------------

$(BUILD_DIR)/$(C_DIR)/%/out.dis: $(BUILD_DIR)/$(C_DIR)/%/out.elf
	$(OBJDUMP) -d -x $< > $@


# ------------------------------------------------------------------------------
# C Example Build Targets
# ------------------------------------------------------------------------------

.PHONY: $(C_EXAMPLE_NAMES)

$(C_EXAMPLE_NAMES): $(C_DIR)/%: \
	$(BUILD_DIR)/$(C_DIR)/%/out.elf \
	$(BUILD_DIR)/$(C_DIR)/%/out.dis \
	$(BUILD_DIR)/$(C_DIR)/%/out.hex \
	$(BUILD_DIR)/$(C_DIR)/%/init.mem	
	@echo "Done!"


# ------------------------------------------------------------------------------
# Firmware Update Over UART
# ------------------------------------------------------------------------------

.PHONY: fw_upd

fw_upd: $(BUILD_DIR)/$(C_DIR)/$(FIRMWARE)/out.hex
	@ $(PYTHON) fw_upd.py $(TTYPORT) $(BAUD) \
		$(BUILD_DIR)/$(C_DIR)/$(FIRMWARE)/out.hex

