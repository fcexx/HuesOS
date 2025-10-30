SHELL := /bin/bash

ASM := nasm
ASM_ELF_FLAGS := -f elf32
ASM_BIN_FLAGS := -f bin
BUILD_DIR := build
ISO_DIR := iso
ISO_BOOT := $(ISO_DIR)/boot
GRUB_DIR := $(ISO_BOOT)/grub
KERNEL_SRC := kernel.asm
KERNEL_OBJ := $(BUILD_DIR)/kernel.o
KERNEL_ELF := $(BUILD_DIR)/huesos.elf
KERNEL_BIN := $(BUILD_DIR)/kernel.bin
MULTIBOOT_SRC := multiboot.asm
MULTIBOOT_OBJ := $(BUILD_DIR)/multiboot.o
MULTIBOOT_BIN := $(BUILD_DIR)/multiboot.bin
ISO_IMAGE := $(BUILD_DIR)/huesos.iso

CC := gcc
CFLAGS := -m32 -ffreestanding -O2 -nostdlib -fno-builtin -fno-stack-protector -Iinc

CSRCS := $(shell find . -type f -name '*.c' -print | sed 's|^\./||')
COBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(CSRCS))

ASMSRCS := $(shell find . -type f -name '*.asm' -print | sed 's|^\./||')
ASMOBJS := $(patsubst %.asm,$(BUILD_DIR)/%.o,$(ASMSRCS))

MULTIBOOT_SRC := $(shell find . -type f -name 'multiboot.asm' -print | sed 's|^\./||')
MULTIBOOT_OBJ := $(if $(MULTIBOOT_SRC),$(BUILD_DIR)/$(MULTIBOOT_SRC:.asm=.o),)
OTHER_ASM_OBJS := $(filter-out $(MULTIBOOT_OBJ),$(ASMOBJS))

.PHONY: all kernel iso clean run

all: iso

kernel: $(KERNEL_BIN)

$(BUILD_DIR)/%.o: %.asm
	@mkdir -p $(dir $@)
	@$(ASM) $(ASM_ELF_FLAGS) -o $@ $<

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c -o $@ $<

$(KERNEL_ELF): $(MULTIBOOT_OBJ) $(OTHER_ASM_OBJS) $(COBJS)
	@mkdir -p $(BUILD_DIR)
	@ld -m elf_i386 -T linker.ld -o $@ $^

$(KERNEL_BIN): $(KERNEL_ELF)
	@objcopy -O binary $< $@

$(GRUB_DIR)/grub.cfg: | $(GRUB_DIR)
	

$(GRUB_DIR):
	@mkdir -p $(GRUB_DIR)


$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c -o $@ $<

iso: $(KERNEL_ELF) $(GRUB_DIR)/grub.cfg
	@cp $(KERNEL_ELF) $(ISO_BOOT)/huesos.elf
	@grub-mkrescue -o $(ISO_IMAGE) $(ISO_DIR) 2>/dev/null || { \
		@echo "grub-mkrescue failed: try installing grub-pc-bin or xorriso" >&2; exit 1; \
	}

run: iso
	@qemu-system-x86_64 -cdrom $(ISO_IMAGE) -m 512M

clean:
	@rm -rf $(BUILD_DIR)
