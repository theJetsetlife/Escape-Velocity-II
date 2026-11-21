# Read-only diagnostic payload -> diag.bin (raw blob for WebKitty runPayload)
#   make PC_IP=10.0.0.60 PC_PORT=9031
PC_IP   ?= 10.0.0.71
PC_PORT ?= 9031

CC      = clang
LD      = ld.lld
OBJCOPY = llvm-objcopy

IP_DEFS := $(shell echo $(PC_IP) | awk -F. '{printf "-DPC_IP0=%d -DPC_IP1=%d -DPC_IP2=%d -DPC_IP3=%d",$$1,$$2,$$3,$$4}')

CFLAGS  = --target=x86_64-unknown-none-elf -march=btver2 -O2 -std=c11 \
          -ffreestanding -fno-builtin -nostdlib -fPIE -fvisibility=hidden \
          -fno-stack-protector -fno-jump-tables -fno-asynchronous-unwind-tables \
          -fno-unwind-tables -mno-red-zone -mgeneral-regs-only -fcf-protection=none \
          -Wall -Wextra -Werror
ASFLAGS = --target=x86_64-unknown-none-elf $(IP_DEFS) -DPC_PORT=$(PC_PORT)

OBJS = entry.o user.o ring0.o

all: diag.bin

%.o: %.c diag.h rec.h ../ps4.h
	$(CC) $(CFLAGS) -c $< -o $@
entry.o: entry.S Makefile
	$(CC) $(ASFLAGS) -c $< -o $@

diag.elf: $(OBJS) diag.ld
	$(LD) -T diag.ld -static --no-pie -z norelro --build-id=none -o $@ $(OBJS)

# Gate: no absolute relocations may exist in any object (the blob is never relocated),
# and the final image must have no GOT / dynamic sections.
diag.bin: diag.elf
	@for o in $(OBJS); do \
	  if llvm-readelf -r $$o | grep -qE 'R_X86_64_(64|32S?)\b|GOTPCREL'; then \
	    echo "FAIL: absolute/GOT relocation in $$o"; llvm-readelf -r $$o | grep -E 'R_X86_64_(64|32S?)\b|GOTPCREL'; exit 1; fi; done
	@if llvm-readelf -S diag.elf | grep -qE '\.got|\.dynamic|\.rela'; then echo "FAIL: GOT/dynamic section"; exit 1; fi
	$(OBJCOPY) -O binary -j .text $< $@
	@sha256sum $@ | tee $@.sha256
	@echo "PC=$(PC_IP):$(PC_PORT) size=$$(stat -c %s $@)"

clean:
	rm -f *.o diag.elf diag.bin diag.bin.sha256
.PHONY: all clean
