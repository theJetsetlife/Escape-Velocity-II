# QEMU handoff harness: out/stub.elf (multiboot, 32-bit) + out/h64.bin (64-bit, links the stage's
# src/upl_load.c and src/directboot/memmap.c unchanged). Run with ./run.sh.
STUB_CFLAGS := -m32 -march=i686 -O1 -ffreestanding -fno-builtin -fno-pic -fno-stack-protector -mno-sse \
               -malign-double -fno-asynchronous-unwind-tables -Wall -Wextra -Werror -Wno-int-to-pointer-cast
H64_CFLAGS  := --target=x86_64-unknown-none-elf -march=btver2 -O2 -std=gnu11 -ffreestanding -fno-builtin \
               -nostdlib -fno-pic -mcmodel=kernel -mno-red-zone -mgeneral-regs-only -fno-stack-protector \
               -fno-asynchronous-unwind-tables -fcf-protection=none -Wall -Wextra -Werror -I../src
H64_SRCS    := h64.c ../src/upl_load.c ../src/directboot/memmap.c ../src/directboot/hda.c

all: out/stub.elf out/h64.bin

out/stub.elf: stub.S stub.c stub.ld hinfo.h
	mkdir -p out
	clang $(STUB_CFLAGS) -c -o out/stub_s.o stub.S
	clang $(STUB_CFLAGS) -c -o out/stub_c.o stub.c
	ld.lld -m elf_i386 -T stub.ld -static -nostdlib -o $@ out/stub_s.o out/stub_c.o

out/h64.elf: $(H64_SRCS) h64.ld hinfo.h ../src/upl_load.h ../src/upl_hob.h ../src/directboot/memmap.h ../src/directboot/hda.h
	mkdir -p out
	clang $(H64_CFLAGS) -c -o out/h64.o h64.c
	clang $(H64_CFLAGS) -c -o out/upl_load.o ../src/upl_load.c
	clang $(H64_CFLAGS) -c -o out/memmap.o ../src/directboot/memmap.c
	clang $(H64_CFLAGS) -c -o out/hda.o ../src/directboot/hda.c
	ld.lld -T h64.ld -static -nostdlib --no-pie -o $@ out/h64.o out/upl_load.o out/memmap.o out/hda.o

out/h64.bin: out/h64.elf
	llvm-objcopy -O binary $< $@

clean:
	rm -rf out
.PHONY: all clean
