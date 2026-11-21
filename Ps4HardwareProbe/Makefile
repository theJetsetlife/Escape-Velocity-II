# Ps4HardwareProbe - standalone GoldHEN payload built with the PS4 Payload SDK (libPS4).
#   make            -> ../payloads/Ps4HardwareProbe.bin (+ .sha256), then `make check`
#   make hosttest   -> mock-hardware tests of every backend (tests/)
#   make PC_IP=a.b.c.d PC_PORT=n ENV=2 LEVEL=1 STRICT=1 FW_HINT=0
# No dependency on Linux, EDK II, OpenCore or Directboot: the firmware table is vendored
# (firmware/profiles), the SDK is built out of tree from PS4SDK.
PS4SDK  ?= $(abspath ../reference/payload-sdk-zip/ps4-payload-sdk-main)
PC_IP   ?= 10.0.0.71
PC_PORT ?= 9033
ENV     ?= 2
LEVEL   ?= 1
STRICT  ?= 0
FW_HINT ?= 0

CC      := gcc
OBJCOPY := objcopy
B       := build
LIBPS4  := $(B)/libPS4
OUTDIR  := ../payloads
OUT     := $(OUTDIR)/Ps4HardwareProbe.bin

RING0 := core/ring0.c core/gates.c firmware/detection/fwdetect.c firmware/profiles/fwprofile.c \
         firmware/profiles/fwsel.c firmware/validation/dmap.c platform/detection/platform.c \
         platform/fat/fat.c platform/slim/slim.c platform/pro/pro.c southbridge/sb.c southbridge/sbregs.c \
         southbridge/aeolia/aeolia.c southbridge/belize/belize.c southbridge/baikal/baikal.c \
         diagnostics/cpu/cpu.c diagnostics/memory/memory.c diagnostics/pci/pci.c diagnostics/interrupts/irq.c \
         diagnostics/gpu/gpu.c diagnostics/acpi/acpi.c diagnostics/usb/usb.c diagnostics/storage/storage.c \
         diagnostics/timers/timers.c
USER  := core/main.c adapters/goldhen/goldhen.c adapters/other-supported-loaders/webkitty.c

IP_DEFS := $(shell echo $(PC_IP) | awk -F. '{printf "-DPC_IP0=%d -DPC_IP1=%d -DPC_IP2=%d -DPC_IP3=%d",$$1,$$2,$$3,$$4}')
BASE   := -I$(LIBPS4)/include -Icore -Ifirmware/profiles -Os -std=c11 -ffunction-sections -fdata-sections \
          -fno-builtin -nostartfiles -nostdlib -ffreestanding -march=btver2 -mtune=btver2 -m64 -mabi=sysv \
          -mcmodel=small -fpie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-jump-tables \
          -fcf-protection=none -Wall -Wextra -Werror
R0FLAGS := $(BASE) -mgeneral-regs-only -mno-red-zone      # ring0: never touch FPU/SSE state, no red zone
UFLAGS  := $(BASE) -Wno-unused-parameter
LFLAGS  := -nostartfiles -nostdlib -static-pie -Wl,--no-dynamic-linker -Wl,-z,norelro -Wl,--hash-style=sysv -Wl,-T,core/probe.ld -Wl,--build-id=none \
           -Wl,--gc-sections -Wl,-q -Wl,--no-warn-rwx-segments -L$(LIBPS4)

R0OBJ := $(patsubst %.c,$(B)/%.o,$(RING0))
UOBJ  := $(patsubst %.c,$(B)/%.o,$(USER))

all: $(OUT) check

$(LIBPS4)/libPS4.a:
	@rm -rf $(LIBPS4) && mkdir -p $(B) && cp -r $(PS4SDK)/libPS4 $(LIBPS4)
	$(MAKE) -C $(LIBPS4) >$(B)/libPS4.log 2>&1 || { tail -20 $(B)/libPS4.log; exit 1; }

$(R0OBJ): $(B)/%.o: %.c $(LIBPS4)/libPS4.a $(wildcard */*.h */*/*.h core/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(R0FLAGS) -c $< -o $@
$(UOBJ): $(B)/%.o: %.c $(LIBPS4)/libPS4.a $(wildcard */*.h */*/*.h core/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(UFLAGS) -c $< -o $@
$(B)/crt0.o: core/crt0.s Makefile
	@mkdir -p $(B)
	$(CC) -x assembler-with-cpp $(IP_DEFS) -DPC_PORT=$(PC_PORT) -DPRB_ENV=$(ENV) -DPRB_LEVEL=$(LEVEL) \
	      -DPRB_STRICT=$(STRICT) -DPRB_FW_HINT=$(FW_HINT) -c $< -o $@

$(B)/probe.elf: $(B)/crt0.o $(R0OBJ) $(UOBJ) core/probe.ld
	$(CC) $(B)/crt0.o $(R0OBJ) $(UOBJ) -o $@ $(LFLAGS) -lPS4 -Wl,-Map=$(B)/probe.map

$(OUT): $(B)/probe.elf
	@mkdir -p $(OUTDIR)
	$(OBJCOPY) -O binary $< $@
	@cd $(OUTDIR) && sha256sum Ps4HardwareProbe.bin > Ps4HardwareProbe.bin.sha256
	@echo "built $@ ($$(stat -c %s $@) bytes) PC=$(PC_IP):$(PC_PORT) env=$(ENV) level=$(LEVEL) strict=$(STRICT)"

# Gates on the final image (not just our sources):
#  - entry at offset 0 is a jmp, config block "PRBCFG01" at 0x10
#  - no absolute relocations (the loader does not relocate), no GOT/dynamic sections
#  - no wrmsr / out / move-to-control-register anywhere in the linked code
#  - ring0 objects do not reference libc (libPS4 memcpy/memset are userland pointers)
check: $(OUT)
	@python3 -I -c "import sys;b=open('$(OUT)','rb').read();assert b[0]==0xe9,'entry is not jmp rel32';assert b[16:24]==b'PRBCFG01','config block not at 0x10';print('format: raw x86-64 blob, entry jmp at 0, config at 0x10, %d bytes'%len(b))"
	@if readelf -r $(B)/probe.elf | grep -E 'R_X86_64_(64|32|32S|GOTPCREL|GLOB_DAT|RELATIVE) ' | grep -v '\.debug' | grep -q .; then \
	   echo "FAIL: absolute/GOT relocations in the image:"; readelf -r $(B)/probe.elf | grep -E 'R_X86_64_(64|32|32S|GOTPCREL|GLOB_DAT|RELATIVE) ' | head; exit 1; fi
	@if readelf -S $(B)/probe.elf | grep -qE ' \.(got|got\.plt|dynamic|interp) '; then echo "FAIL: GOT/dynamic section"; exit 1; fi
	@if objdump -d $(B)/probe.elf | grep -qE '\s(wrmsr|out|outs[bwl]?)\s|mov\s+%[a-z0-9]+,%cr[0-9]'; then \
	   echo "FAIL: forbidden instruction:"; objdump -d $(B)/probe.elf | grep -E '\s(wrmsr|out|outs[bwl]?)\s|mov\s+%[a-z0-9]+,%cr[0-9]' | head; exit 1; fi
	@for o in $(R0OBJ); do if nm -u $$o | grep -qwE 'memcpy|memset|memmove|memcmp'; then echo "FAIL: libc call in ring0 object $$o"; exit 1; fi; done
	@file -b $(B)/probe.elf | cut -d, -f1-2
	@cat $(OUT).sha256
	@echo "check: PASS"

# Host tests: the same ring0 sources against tests/mock_hal.c (fake CPU/page tables/ECAM/MMIO)
HOSTSRC := $(RING0) adapters/goldhen/goldhen.c adapters/other-supported-loaders/webkitty.c tests/mock_hal.c tests/probe_hosttest.c
hosttest:
	@mkdir -p $(B)
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Werror -DPROBE_HOST -Itests/hostinc -Icore -Ifirmware/profiles \
	      $(HOSTSRC) -o $(B)/probe_hosttest
	$(B)/probe_hosttest $(B)/hosttest $(KDUMP)
	python3 -I logging/test_decode.py $(B)/hosttest

fwident:
	python3 -I firmware/profiles/mkfwident.py $(abspath ../reference/open-zip/OpenGoldHEN-main) $(KDUMP)

vendor-check:
	@for f in fwsel.c fwtable.h; do diff <(grep -v '^ \* Refresh\|^/\* VENDORED' ../src/directboot/$$f) <(grep -v '^ \* Refresh\|^/\* VENDORED' firmware/profiles/$$f) >/dev/null \
	   && echo "$$f: same as src/directboot" || echo "$$f: DIFFERS from src/directboot"; done

clean:
	rm -rf $(B)

.PHONY: all check hosttest fwident vendor-check clean
SHELL := /bin/bash
