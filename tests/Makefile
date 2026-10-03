# SPDX-License-Identifier: GPL-2.0-only
CFLAGS ?= -O2 -Wall -Wextra
CLANG ?= clang
OBJCOPY ?= llvm-objcopy
PYTHON ?= python3
.PHONY: all guest-header clean
all: kvm-pmu-guest-test perf-guest-filter-probe
kvm-pmu-guest-test: kvm-pmu-guest-test.c pmu-guest.h
	$(CC) $(CFLAGS) $< -o $@
perf-guest-filter-probe: perf-guest-filter-probe.c
	$(CC) $(CFLAGS) $< -o $@
guest-header:
	$(CLANG) --target=aarch64-none-elf -c pmu-guest.S -o pmu-guest.o
	$(OBJCOPY) -O binary --only-section=.text pmu-guest.o pmu-guest.bin
	$(PYTHON) generate_guest_header.py pmu-guest.bin pmu-guest.h
clean:
	rm -f kvm-pmu-guest-test perf-guest-filter-probe pmu-guest.o pmu-guest.bin
