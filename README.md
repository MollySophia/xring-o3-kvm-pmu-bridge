# Xring O3 KVM PMU bridge

Experimental ARM64 kernel module that connects the inspected Xring O3 vendor
PMU driver to the host kernel's KVM PMUv3 support. It was developed while
debugging Windows ARM64 guests in DroidVM.

The vendor PMU is available to host perf, but its registration path does not
populate KVM's host PMU list. The bridge supplies host-layout PMU metadata and
wraps the vendor enable/disable/reset callbacks to maintain KVM accounting.
Guest-only counters are deferred to the existing nVHE context-switch code.

## Status and compatibility

The original prototype enabled a small KVM guest to read PMCCNTR_EL0 and let a
Windows ARM64 VM reach the desktop with 4 vCPUs and 4 GiB RAM. See
[validation](docs/VALIDATION.md) for the evidence and remaining tests.

Only this device/kernel combination has been investigated:

- Xring O3, vendor module `xring_arm_pmu`, 10 CPUs.
- Kernel `6.18.21-android17-5-g284297031c53-abogki553682993-4k`.
- Host running at EL1 with the nVHE KVM path; ordinary KVM guests.

This is **not a generic O3 or Android module**. Vendor offsets and perf type are
hard-coded. The runtime symbol, CFI and object checks are partial safeguards,
not proof of ABI compatibility. Matching the kernel release alone is also
insufficient. Protected guests have not been validated.

The repository contains source and research notes. There is no prebuilt release,
KernelSU/Magisk package, startup service or automatic installation.

## Build

Use Linux/WSL with an ARM64 kernel build tree prepared for the target device,
including its generated configuration and matching exported-symbol CRCs.
CFI-compatible LLVM tooling is required. `modules_prepare` alone does not
produce a usable `Module.symvers` when module versioning is enabled.

```sh
make KDIR=/absolute/path/to/prepared/kernel/build ARCH=arm64 LLVM=1
```

The output is `xring_o3_kvm_pmu_bridge.ko`. A successful build does not establish
that the module is safe to load on a particular device. The prototype used
public Android common headers plus device-specific ABI checks; that preparation
is described in [build notes](docs/BUILD.md). This repository does not recreate
a matching kernel build tree from a stock kernel checkout.

## Manual use on the validated setup

Stop VMs and perf sessions before changing PMU hooks. Reboot first if the old
`droidvm_pmu_bridge` prototype is already registered; the two modules must not
be loaded together. On the validated setup, with a compatible module and root:

```sh
adb push xring_o3_kvm_pmu_bridge.ko /data/local/tmp/
adb shell su -c 'insmod /data/local/tmp/xring_o3_kvm_pmu_bridge.ko register_pmu=1'
adb shell su -c 'dmesg | grep xring_o3_kvm_pmu_bridge'
```

Load before creating the VM, then enable PMU in its KVM configuration.
`register_pmu=1` intentionally pins the module until reboot: KVM retains raw
references and provides no unregister API. **Do not force `rmmod`.** Stop the
guest and reboot to remove the registered bridge. No boot image flashing is
needed for this workflow; it still requires a kernel that permits module loading.

Without `register_pmu=1`, the default is accounting-only diagnostic mode. It does
not enable KVM PMUv3. Prototype unloading was tested only with no active VMs;
unloading under workload is not validated.

## Test programs

`tests/` contains an ARM64 KVM guest probe and a host/guest perf-filter probe.
They assume CPU 0; the perf probe also assumes vendor perf type 11. Build on
ARM64 Linux with `make -C tests`, or specify an Android NDK compiler:

```sh
make -C tests CC=/path/to/ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang
```

`make -C tests guest-header` regenerates the checked-in guest payload from
`pmu-guest.S` using Clang, llvm-objcopy and Python 3. Run device probes manually
with root. They are diagnostics, not a stability or overflow-interrupt test suite.

## License

GPL-2.0-only. See [LICENSE](LICENSE). The bridge relies on Linux/Android kernel
interfaces; see [build notes](docs/BUILD.md) for the reference revision.
