# Standalone research scaffold for PS4 WebKitty Bin Loader (no UEFI).
# make PC_IP=<receiver IPv4> PC_PORT=9031
PC_IP ?= 10.0.0.71
PC_PORT ?= 9031
CC = clang
LD = ld.lld
OBJCOPY = llvm-objcopy
READELF := $(shell command -v llvm-readelf || command -v readelf)
OBJDUMP := $(shell command -v llvm-objdump || command -v objdump)
IP_DEFS := $(shell printf '%s\n' '$(PC_IP)' | awk -F. '{printf "-DPC_IP0=%d -DPC_IP1=%d -DPC_IP2=%d -DPC_IP3=%d",$$1,$$2,$$3,$$4}')
CFLAGS = --target=x86_64-unknown-none-elf -march=btver2 -O2 -std=c11 \
         -ffreestanding -fno-builtin -nostdlib -fPIE -fvisibility=hidden \
         -fno-stack-protector -fno-jump-tables -fno-asynchronous-unwind-tables \
         -fno-unwind-tables -mno-red-zone -mgeneral-regs-only -fcf-protection=none \
         -Wall -Wextra -Werror
ASFLAGS = --target=x86_64-unknown-none-elf $(IP_DEFS) -DPC_PORT=$(PC_PORT)
OBJS = entry.o main.o experiment.o
all: native-research.bin

%.o: %.c native.h Makefile
	$(CC) $(CFLAGS) -c $< -o $@
entry.o: entry.S Makefile FORCE
	$(CC) $(ASFLAGS) -c $< -o $@

native-research.elf: $(OBJS) native.ld
	$(LD) -T native.ld -static --no-pie -z norelro --build-id=none -o $@ $(OBJS)

native-research.bin: native-research.elf
	@for o in $(OBJS); do \
	  if $(READELF) -r $$o | grep -Eq 'R_X86_64_(64|32S?)\b|GOTPCREL'; then \
	    echo 'FAIL: absolute or GOT relocation in' $$o; exit 1; fi; done
	@if $(READELF) -S $< | grep -Eq '\.got|\.dynamic|\.rela'; then \
	  echo 'FAIL: GOT/dynamic/relocation section'; exit 1; fi
	@if $(OBJDUMP) -d $< | grep -Eq 'xmm|ymm|zmm'; then \
	  echo 'FAIL: vector instructions'; exit 1; fi
	$(OBJCOPY) -O binary -j .text $< $@
	@sha256sum $@ > $@.sha256
	@echo "Built $@ ($$(stat -c %s $@) bytes) for $(PC_IP):$(PC_PORT)"

clean:
	rm -f $(OBJS) native-research.elf native-research.bin native-research.bin.sha256
FORCE:

.PHONY: all clean FORCE
