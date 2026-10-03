# Build and ABI notes

The original prototype was built in ARM64 Arch Linux under WSL with Clang 23.1.1.
The phone kernel was built with a different compiler; CFI callback hashes were
checked at runtime, not assumed compatible from the compiler version.

Reference headers: Android common kernel `android17-6.18`, commit
[`b8bafdd92a5f277db70697bb860a781d80873ae4`](https://android.googlesource.com/kernel/common/+/b8bafdd92a5f277db70697bb860a781d80873ae4/),
version 6.18.32. The running phone kernel is 6.18.21. These public headers alone
are not a matching SDK for the phone.

For the experiment, the phone configuration and actual exported-symbol names,
namespaces, flags and CRCs were used to prepare a build tree. BTF sizes and all
member names/offsets were compared for `module`, `kprobe`, `arm_pmu`, `pmu`,
`perf_event_attr`, `perf_event`, and `hw_perf_event`. Such comparisons do not prove
all semantic or type compatibility. No forced module loading or disabled CFI
was used. Kernel images, BTF dumps, device configuration and extracted symbol
tables are intentionally not distributed here.

The build-only protected-module-name list was empty because this experiment
exports no symbols. Runtime signature and protected-symbol enforcement were not
changed. Do not suppress modpost errors or bypass device load failures as a
substitute for checking the target ABI.

## Inspected vendor layout

The vendor `arm_pmu` object is 584 bytes; the host layout is 600 bytes. It cannot
be passed directly to KVM. The bridge constructs host-layout metadata separately.
The vendor fields currently accessed by byte offset are:

| Field | Offset / expected value |
|---|---|
| Supported CPUs, first word | 320 / `0x3ff` |
| PMU version | 336 / observed 8 |
| Enable callback | 352 |
| Disable callback | 360 |
| Reset callback | 424 |
| Counter mask, first word | 440 / `0x8000007f` |

Vendor perf type is 11. Callback addresses must match the inspected vendor
symbols and their CFI hashes must match the wrapper prototypes. A temporary
kprobe resolves `kallsyms_lookup_name` and is removed immediately. KVM's private
registration/list/accounting symbols are dependencies of this experiment, not
a stable module API.

PMCEID bits are intersected over CPUs online at load time. Supporting additional
kernel versions, VHE, CPU hotplug or another vendor driver requires a new audit.
There is currently no automatic build-time BTF validator or exact-release gate.

For a packaging-only build using the existing prepared research tree:

```sh
make KDIR=/home/arch/droidvm-pmu/build ARCH=arm64 LLVM=1
```

That path is an example from the research machine. Users must supply their own
compatible prepared tree. Keep CFI and normal module-loader checks enabled.
