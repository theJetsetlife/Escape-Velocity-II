# One-command entry points (details: docs/BUILD.md, docs/TESTING.md, docs/HARDWARE.md).
#   make setup     fetch pinned upstream edk2 + build BaseTools (once)
#   make stage     PS4 directboot stage  -> src/directboot/directboot.bin (needs PC_IP from server/.env)
#   make upl       EDK II Universal Payload -> out/upl-ps4.elf
#   make hosttest  host-side unit tests (UPL loader, memory map rules, DS4 report parser)
#   make qemu      QEMU regression matrix (must all print RESULT: PASS before any PS4 run)
#   make server    start the PC-side containers (WebKitty + receiver)
#   make deploy    put stage into the WebKitty slot, verify both served hashes
-include server/.env
PC_IP   ?= 192.168.1.10
PC_PORT ?= 9031
T := out/hosttest

.PHONY: all setup stage upl hosttest qemu server deploy dsdt clean
all: stage upl hosttest

setup:
	scripts/setup-edk2.sh

stage:
	$(MAKE) -C src/directboot PC_IP=$(PC_IP) PC_PORT=$(PC_PORT)

upl:
	Ps4Pkg/build.sh

dsdt:
	tools/mkdsdt.sh

hosttest:
	@mkdir -p $(T)
	cc -O1 -DUPL_HOST_TEST -I src tools/upl_hosttest.c src/upl_load.c -o $(T)/upl_hosttest
	cc -O1 -I src -I src/directboot tools/memmap_hosttest.c src/directboot/memmap.c -o $(T)/memmap_hosttest
	cc -std=c11 -Wall -Wextra -Werror -DDS4_HOST_TEST -I Ps4Pkg/Ps4Ds4Dxe tools/ds4_hosttest.c Ps4Pkg/Ps4Ds4Dxe/Ds4Report.c -o $(T)/ds4_hosttest
	$(T)/upl_hosttest out/upl-ps4.elf | tail -1
	$(T)/memmap_hosttest | tail -1
	$(T)/ds4_hosttest | tail -1

qemu:
	qemu/matrix.sh

server:
	cd server && docker compose --env-file .env up -d --force-recreate

deploy:
	tools/deploy.sh

clean:
	rm -f src/directboot/*.o src/directboot/directboot.elf
	rm -rf out/hosttest qemu/out
