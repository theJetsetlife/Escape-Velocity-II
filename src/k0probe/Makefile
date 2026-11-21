# k0probe -> k0probe.bin (raw blob, GoldHEN slot like diag.bin)
#   make PC_IP=10.0.0.71 PC_PORT=9031
PC_IP   ?= 10.0.0.71
PC_PORT ?= 9031
CC = clang
LD = ld.lld
OBJCOPY = llvm-objcopy
IP_DEFS := $(shell echo $(PC_IP) | awk -F. '{printf "-DPC_IP0=%d -DPC_IP1=%d -DPC_IP2=%d -DPC_IP3=%d",$$1,$$2,$$3,$$4}')
CFLAGS = --target=x86_64-unknown-none-elf -march=btver2 -O2 -std=gnu11 \
         -ffreestanding -fno-builtin -nostdlib -fPIE -fvisibility=hidden \
         -fno-stack-protector -fno-jump-tables -fno-asynchronous-unwind-tables \
         -fno-unwind-tables -mno-red-zone -mgeneral-regs-only -fcf-protection=none \
         -Wall -Wextra -Werror
ASFLAGS = --target=x86_64-unknown-none-elf $(IP_DEFS) -DPC_PORT=$(PC_PORT)
OBJS = entry.o user.o ring0.o
HDRS = k0.h ../diag/diag.h ../diag/rec.h ../ps4.h

all: k0probe.bin

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@
entry.o: entry.S Makefile
	$(CC) $(ASFLAGS) -c $< -o $@

k0probe.elf: $(OBJS) k0.ld
	$(LD) -T k0.ld -static --no-pie -z norelro --build-id=none -o $@ $(OBJS)

# Gates: the blob runs from two addresses (user mapping + kernel copy): no absolute/GOT relocs,
# no GOT/dynamic sections, no SSE (kernel context, no FPU save).
k0probe.bin: k0probe.elf
	@for o in $(OBJS); do \
	  if llvm-readelf -r $$o | grep -qE 'R_X86_64_(64|32S?)\b|GOTPCREL'; then \
	    echo "FAIL: absolute/GOT relocation in $$o"; llvm-readelf -r $$o | grep -E 'R_X86_64_(64|32S?)\b|GOTPCREL'; exit 1; fi; done
	@if llvm-readelf -S $< | grep -qE '\.got|\.dynamic|\.rela'; then echo "FAIL: GOT/dynamic section"; exit 1; fi
	@if llvm-objdump -d $< | grep -qE 'xmm|ymm'; then echo "FAIL: SSE in blob"; exit 1; fi
	$(OBJCOPY) -O binary -j .text $< $@
	@sha256sum $@ | tee $@.sha256
	@echo "PC=$(PC_IP):$(PC_PORT) size=$$(stat -c %s $@)"

clean:
	rm -f *.o k0probe.elf k0probe.bin k0probe.bin.sha256
.PHONY: all clean
