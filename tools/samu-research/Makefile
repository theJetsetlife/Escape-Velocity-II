# All targets execute exclusively on the workstation. No EFI/PS4 build steps.
CC ?= cc
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Werror -pedantic
PYTHON ?= python3
.PHONY: test verify-kernel clean

sbl_offline_test: sbl_host_model.c
	$(CC) $(CFLAGS) -DSBL_OFFLINE_TEST $< -o $@

test: sbl_offline_test
	./sbl_offline_test
	$(PYTHON) -m unittest -v test_verifier.py

verify-kernel:
	@test -n "$(KDUMP)" || (echo 'Usage: make verify-kernel KDUMP=/path/to/kernel.bin' >&2; exit 2)
	$(PYTHON) verify_kernel_900.py "$(KDUMP)"

clean:
	rm -f sbl_offline_test
