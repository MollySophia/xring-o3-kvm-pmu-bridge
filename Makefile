# SPDX-License-Identifier: GPL-2.0-only
ifneq ($(KERNELRELEASE),)
obj-m := xring_o3_kvm_pmu_bridge.o
else
KDIR ?= /lib/modules/$(shell uname -r)/build
ARCH ?= arm64
LLVM ?= 1
.PHONY: all clean
all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) LLVM=$(LLVM) modules
clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=$(ARCH) LLVM=$(LLVM) clean
endif
