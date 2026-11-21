# Directboot stage -> directboot.bin (raw blob, GoldHEN slot like diag.bin)
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
         -include ../diag/rec.h -Wall -Wextra -Werror $(EXTRA_CFLAGS)
ASFLAGS = --target=x86_64-unknown-none-elf $(IP_DEFS) -DPC_PORT=$(PC_PORT)
OBJS = entry.o user.o arm.o fwsel.o hook.o hook_entry.o upl_load.o memmap.o hda.o
HDRS = db.h fwsel.h fwtable.h memmap.h hda.h ../diag/diag.h ../diag/rec.h ../upl_load.h ../upl_hob.h ../ps4.h

all: directboot.bin

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@
upl_load.o: ../upl_load.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@
# code review: PC_IP/PC_PORT come from the command line; the stamp changes only when they do, so
# a different address really rebuilds entry.o (it holds db_cfg).
.pcaddr: FORCE
	@echo "$(PC_IP):$(PC_PORT)" | cmp -s - $@ || echo "$(PC_IP):$(PC_PORT)" > $@
FORCE:
entry.o: entry.S Makefile .pcaddr
	$(CC) $(ASFLAGS) -c $< -o $@
hook_entry.o: hook_entry.S
	$(CC) --target=x86_64-unknown-none-elf -c $< -o $@

directboot.elf: $(OBJS) db.ld
	$(LD) -T db.ld -static --no-pie -z norelro --build-id=none -o $@ $(OBJS)

# Gates: no absolute/GOT relocations (the blob runs from two different addresses: the user
# mapping and its kernel copy), no GOT/dynamic sections, no SSE (kernel context, no FPU save).
directboot.bin: directboot.elf
	@for o in $(OBJS); do \
	  if llvm-readelf -r $$o | grep -qE 'R_X86_64_(64|32S?)\b|GOTPCREL'; then \
	    echo "FAIL: absolute/GOT relocation in $$o"; llvm-readelf -r $$o | grep -E 'R_X86_64_(64|32S?)\b|GOTPCREL'; exit 1; fi; done
	@if llvm-readelf -S $< | grep -qE '\.got|\.dynamic|\.rela'; then echo "FAIL: GOT/dynamic section"; exit 1; fi
	@if llvm-objdump -d $< | grep -qE 'xmm|ymm'; then echo "FAIL: SSE in blob"; exit 1; fi
	$(OBJCOPY) -O binary -j .text $< $@
	@sha256sum $@ | tee $@.sha256
	@echo "PC=$(PC_IP):$(PC_PORT) size=$$(stat -c %s $@)"

.PHONY: all FORCE
