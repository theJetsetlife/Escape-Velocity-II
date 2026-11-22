# Host-only security analysis; no PS4, EDK II, flasher, or network targets.
CC ?= cc
CLANG ?= clang
PYTHON ?= python3
BUILD := .build
CORPUS ?= .local-corpus
# User-supplied 9.00 kernel and NOR are local inputs, not a network download.
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Werror -pedantic
SAN_FLAGS := -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined
.PHONY: test cleanroom guard fuzz sanitizers static-analysis verify-kernel corpus-init test-real clean

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/sbl_offline_test: sbl_host_model.c | $(BUILD)
	$(CC) $(CFLAGS) -DSBL_OFFLINE_TEST $< -o $@

$(BUILD)/sbl_offline_san: sbl_host_model.c | $(BUILD)
	$(CLANG) $(CFLAGS) $(SAN_FLAGS) -DSBL_OFFLINE_TEST $< -o $@

$(BUILD)/sbl_fuzzer: fuzz_sbl_host.c sbl_host_model.c | $(BUILD)
	$(CLANG) $(CFLAGS) -Wno-unused-function $(SAN_FLAGS) -fsanitize=fuzzer $< -o $@

test: $(BUILD)/sbl_offline_test
	./$(BUILD)/sbl_offline_test
	$(PYTHON) -m unittest discover -p 'test_*.py' -v

guard:
	$(PYTHON) cleanroom_guard.py

sanitizers: $(BUILD)/sbl_offline_san
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 ./$(BUILD)/sbl_offline_san

fuzz: $(BUILD)/sbl_fuzzer
	$(PYTHON) fuzz_elf.py --iterations 5000 --seed 900
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 ./$(BUILD)/sbl_fuzzer -seed=900 -runs=3000 -max_len=8192 -timeout=10 -rss_limit_mb=512 -artifact_prefix=$(BUILD)/

static-analysis: | $(BUILD)
	$(CLANG) --analyze -std=c11 -Wall -Wextra -DSBL_OFFLINE_TEST sbl_host_model.c -o $(BUILD)/static_analysis.plist

cleanroom: guard test sanitizers fuzz static-analysis

verify-kernel:
	@test -n "$(KDUMP)" || (echo 'Usage: make verify-kernel KDUMP=/path/to/kernel.bin' >&2; exit 2)
	mkdir -p $(BUILD)
	$(PYTHON) verify_kernel_900.py "$(KDUMP)" --out $(BUILD)/local_kernel_verification.json
	$(PYTHON) verify_call_graph.py "$(KDUMP)" --out $(BUILD)/local_call_graph_verification.json

corpus-init:
	@test -n "$(KDUMP)" && test -n "$(NOR)" || (echo 'Usage: make corpus-init KDUMP=/path/kernel.bin NOR=/path/sflash0.bin' >&2; exit 2)
	$(PYTHON) real_corpus.py init --kernel "$(KDUMP)" --nor "$(NOR)" --corpus "$(CORPUS)"

test-real:
	$(PYTHON) real_corpus.py verify --corpus "$(CORPUS)"
	$(PYTHON) verify_kernel_900.py "$(CORPUS)/kernel_900.bin" --out "$(CORPUS)/kernel_verification.json"
	$(PYTHON) verify_call_graph.py "$(CORPUS)/kernel_900.bin" --out "$(CORPUS)/call_graph_verification.json"

clean:
	rm -rf $(BUILD)
