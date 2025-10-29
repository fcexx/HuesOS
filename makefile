SHELL := /bin/bash

ASM := nasm
ASM_BIN_FLAGS := -f bin
BUILD_DIR := build

# find all .asm sources recursively (strip leading ./)
SRCS := $(shell find . -type f -name '*.asm' -print | sed 's|^\./||')
# flat binaries live in $(BUILD_DIR)/<path>.bin
BINS := $(patsubst %.asm,$(BUILD_DIR)/%.bin,$(SRCS))

# special detection for bootloader (optional)
BOOT_SRC := $(shell find . -type f -name 'bootloader.asm' -print | sed 's|^\./||')

IMAGE := $(BUILD_DIR)/os-image.bin

.PHONY: all clean dirs

all: dirs $(IMAGE)

run: $(IMAGE)
	qemu-system-x86_64 -fda $<

dirs:
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.bin: %.asm
	mkdir -p $(dir $@)
	$(ASM) $(ASM_BIN_FLAGS) -o $@ $<

ifeq ($(BOOT_SRC),)
$(IMAGE): $(BINS)
	cat $(BINS) > $@
else
$(IMAGE): $(BUILD_DIR)/$(BOOT_SRC:.asm=.bin) $(filter-out $(BUILD_DIR)/$(BOOT_SRC:.asm=.bin),$(BINS))
	cat $^ > $@
endif

clean:
	rm -rf $(BUILD_DIR)
