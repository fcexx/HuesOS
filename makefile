SHELL := /bin/bash

ASM := nasm
ASM_ELF_FLAGS := -f elf64
ASM_BIN_FLAGS := -f bin
BUILD_DIR := build
ISO_DIR := iso
ISO_BOOT := $(ISO_DIR)/boot
GRUB_DIR := $(ISO_BOOT)/grub
KERNEL_SRC := kernel.asm
KERNEL_OBJ := $(BUILD_DIR)/kernel.o
KERNEL_ELF := $(BUILD_DIR)/axonos.elf
KERNEL_BIN := $(BUILD_DIR)/kernel.bin
MULTIBOOT_SRC := multiboot.asm
MULTIBOOT_OBJ := $(BUILD_DIR)/multiboot.o
MULTIBOOT_BIN := $(BUILD_DIR)/multiboot.bin
ISO_IMAGE := $(BUILD_DIR)/axonos.iso

CC := gcc -m64
CFLAGS := -ffreestanding -O2 -nostdlib -fno-builtin -fno-stack-protector -fno-pic -mno-red-zone -mcmodel=kernel -Iinc -w

# Set USE_MBEDTLS=1 to enable TLS (requires third_party/mbedtls)
USE_MBEDTLS ?= 0
ifeq ($(USE_MBEDTLS),1)
# By default, prefer mbedTLS v2 if available (less deps, no PSA)
USE_MBEDTLS_V2 ?= 0
ifeq ($(USE_MBEDTLS_V2),1)
MBEDTLS_V2_DIR := third_party/mbedtls_v2
 CFLAGS += -DUSE_MBEDTLS -Iinc -I$(MBEDTLS_V2_DIR)/include -DMBEDTLS_CONFIG_FILE=\"mbedtls_config_v2.h\"
# Minimal subset of mbedTLS v2 needed for TLS client
MBEDTLS_SRCS := \
 $(MBEDTLS_V2_DIR)/library/asn1parse.c \
 $(MBEDTLS_V2_DIR)/library/asn1write.c \
 $(MBEDTLS_V2_DIR)/library/base64.c \
 $(MBEDTLS_V2_DIR)/library/oid.c \
 $(MBEDTLS_V2_DIR)/library/bignum.c \
 $(MBEDTLS_V2_DIR)/library/md.c \
 $(MBEDTLS_V2_DIR)/library/sha256.c \
 $(MBEDTLS_V2_DIR)/library/sha1.c \
 $(MBEDTLS_V2_DIR)/library/sha512.c \
 $(MBEDTLS_V2_DIR)/library/aes.c \
 $(MBEDTLS_V2_DIR)/library/cipher.c \
 $(MBEDTLS_V2_DIR)/library/cipher_wrap.c \
 $(MBEDTLS_V2_DIR)/library/gcm.c \
 $(MBEDTLS_V2_DIR)/library/chacha20.c \
 $(MBEDTLS_V2_DIR)/library/poly1305.c \
 $(MBEDTLS_V2_DIR)/library/chachapoly.c \
 $(MBEDTLS_V2_DIR)/library/entropy.c \
 $(MBEDTLS_V2_DIR)/library/ctr_drbg.c \
 $(MBEDTLS_V2_DIR)/library/pk.c \
 $(MBEDTLS_V2_DIR)/library/pkparse.c \
 $(MBEDTLS_V2_DIR)/library/pk_wrap.c \
 $(MBEDTLS_V2_DIR)/library/ecp.c \
 $(MBEDTLS_V2_DIR)/library/ecp_curves.c \
 $(MBEDTLS_V2_DIR)/library/ecdsa.c \
 $(MBEDTLS_V2_DIR)/library/ecdh.c \
 $(MBEDTLS_V2_DIR)/library/rsa.c \
 $(MBEDTLS_V2_DIR)/library/rsa_internal.c \
 $(MBEDTLS_V2_DIR)/library/x509.c \
 $(MBEDTLS_V2_DIR)/library/x509_crt.c \
 $(MBEDTLS_V2_DIR)/library/ssl_ciphersuites.c \
 $(MBEDTLS_V2_DIR)/library/ssl_cli.c \
 $(MBEDTLS_V2_DIR)/library/ssl_srv.c \
 $(MBEDTLS_V2_DIR)/library/ssl_msg.c \
 $(MBEDTLS_V2_DIR)/library/ssl_tls.c \
 $(MBEDTLS_V2_DIR)/library/debug.c \
 $(MBEDTLS_V2_DIR)/library/platform.c \
 $(MBEDTLS_V2_DIR)/library/platform_util.c \
 $(MBEDTLS_V2_DIR)/library/constant_time.c \
 $(MBEDTLS_V2_DIR)/library/version.c
MBEDTLS_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.c.o,$(MBEDTLS_SRCS))
else
# Fallback to vendored mbedTLS v3 (requires PSA; not recommended)
CFLAGS += -DUSE_MBEDTLS -Ithird_party/mbedtls/include -DMBEDTLS_CONFIG_FILE=\"mbedtls_config.h\"
MBEDTLS_SRCS := \
 third_party/mbedtls/library/ssl_cache.c \
 third_party/mbedtls/library/ssl_ciphersuites.c \
 third_party/mbedtls/library/ssl_client.c \
 third_party/mbedtls/library/ssl_msg.c \
 third_party/mbedtls/library/ssl_tls.c \
 third_party/mbedtls/library/ssl_tls12_client.c \
 third_party/mbedtls/library/timing.c \
 third_party/mbedtls/library/version.c \
 third_party/mbedtls/library/x509.c \
 third_party/mbedtls/library/x509_crt.c \
 third_party/mbedtls/library/x509_crl.c \
 third_party/mbedtls/library/x509_csr.c \
 third_party/mbedtls/library/x509_create.c \
 third_party/mbedtls/library/x509_write.c
MBEDTLS_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.c.o,$(MBEDTLS_SRCS))
endif
else
MBEDTLS_OBJS :=
endif

CSRCS := $(shell find . -path './build' -prune -o -path './iso' -prune -o -path './third_party' -prune -o -type f -name '*.c' -print | sed 's|^\./||')
COBJS := $(patsubst %.c,$(BUILD_DIR)/%.c.o,$(CSRCS))

ASMSRCS := $(shell find . -path './build' -prune -o -path './iso' -prune -o -path './third_party' -prune -o -type f -name '*.asm' -print | sed 's|^\./||')
ASMOBJS := $(patsubst %.asm,$(BUILD_DIR)/%.asm.o,$(ASMSRCS))

# GAS (preprocessed) assembly sources
SSRCS := $(shell find . -path './build' -prune -o -path './iso' -prune -o -path './third_party' -prune -o -type f -name '*.S' -print | sed 's|^\./||')
SOBJS := $(patsubst %.S,$(BUILD_DIR)/%.S.o,$(SSRCS))

LWIP_SRCS := \
	third_party/lwip/src/core/init.c \
	third_party/lwip/src/core/def.c \
	third_party/lwip/src/core/inet_chksum.c \
	third_party/lwip/src/core/ip.c \
	third_party/lwip/src/core/mem.c \
	third_party/lwip/src/core/memp.c \
	third_party/lwip/src/core/netif.c \
	third_party/lwip/src/core/pbuf.c \
	third_party/lwip/src/core/raw.c \
	third_party/lwip/src/core/stats.c \
	third_party/lwip/src/core/sys.c \
	third_party/lwip/src/core/tcp.c \
	third_party/lwip/src/core/tcp_in.c \
	third_party/lwip/src/core/tcp_out.c \
	third_party/lwip/src/core/timeouts.c \
	third_party/lwip/src/core/udp.c \
	third_party/lwip/src/core/ipv4/ip4.c \
	third_party/lwip/src/core/ipv4/ip4_addr.c \
	third_party/lwip/src/core/ipv4/icmp.c \
	third_party/lwip/src/netif/ethernet.c \
	third_party/lwip/src/core/ipv4/etharp.c
LWIP_OBJS :=
MBEDTLS_OBJS :=

MULTIBOOT_SRC := $(shell find . -path './build' -prune -o -path './iso' -prune -o -type f -name 'multiboot.asm' -print | sed 's|^\./||')
MULTIBOOT_OBJ := $(if $(MULTIBOOT_SRC),$(BUILD_DIR)/$(MULTIBOOT_SRC:.asm=.asm.o),)
OTHER_ASM_OBJS := $(filter-out $(MULTIBOOT_OBJ),$(ASMOBJS))

.PHONY: all kernel iso clean run

all: iso

kernel: $(KERNEL_BIN)

$(BUILD_DIR)/%.asm.o: %.asm
	@mkdir -p $(dir $@)
	@$(ASM) $(ASM_ELF_FLAGS) -o $@ $<

$(BUILD_DIR)/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# Build rule for GAS .S files (with C preprocessor)
$(BUILD_DIR)/%.S.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(KERNEL_ELF): $(MULTIBOOT_OBJ) $(OTHER_ASM_OBJS) $(SOBJS) $(COBJS) $(LWIP_OBJS) $(MBEDTLS_OBJS)
	@mkdir -p $(BUILD_DIR)
	@ld -m elf_x86_64 -T linker.ld --allow-multiple-definition -o $@ $^

$(KERNEL_BIN): $(KERNEL_ELF)
	@objcopy -O binary $< $@

$(GRUB_DIR)/grub.cfg: | $(GRUB_DIR)
	

$(GRUB_DIR):
	@mkdir -p $(GRUB_DIR)




iso: $(KERNEL_ELF) $(GRUB_DIR)/grub.cfg
	@cp $(KERNEL_ELF) $(ISO_BOOT)/axonos.elf
	@grub-mkrescue -o $(ISO_IMAGE) $(ISO_DIR) 2>/dev/null || { \
		@echo "grub-mkrescue failed: try installing grub-pc-bin or xorriso" >&2; exit 1; \
	}

run: iso
	@qemu-system-x86_64 $(ISO_IMAGE) -netdev user,id=n1 -device e1000,netdev=n1 -serial stdio -d int,guest_errors -no-reboot -no-shutdown -D qemu.log

clean:
	@rm -rf $(BUILD_DIR)
