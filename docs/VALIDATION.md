# Validation record

Date: 2026-10-03. Kernel:
`6.18.21-android17-5-g284297031c53-abogki553682993-4k`.

These runtime results belong to the original `droidvm_pmu_bridge` prototype.
The initial repository renames the source, module and log prefix to
`xring_o3_kvm_pmu_bridge`; a fresh device run of the renamed build is pending.

Repository packaging checks: the renamed module compiled with the existing
prepared kernel build tree and Clang 23.1.1. Both diagnostic executables compiled
on ARM64 Linux. Regenerating the guest header preserved the original 104-byte
payload. Module BTF generation was skipped because that tree lacks vmlinux;
this build does not replace the earlier independent ABI comparison.

- Accounting-only hooks: host loop counted 30,283,750 cycles, while the same
  host workload with `exclude_host` counted zero. With the hooks removed,
  `exclude_host` incorrectly counted approximately 30 million cycles again.
- Registration enabled: `KVM_CAP_ARM_PMU_V3` changed from 0 to 1 for KVM and an
  ordinary VM. The registered metadata used perf type 11 and mask `0x8000007f`.
- CPU 0 guest: PMCCNTR_EL0 reads succeeded; loop count was 1,000,403. After a
  200 ms host sleep, the count was 1,000,926 (delta 523). Completion marker was
  `0x600d`. Initial uninitialized counter contents are not useful timing data.
- DroidVM: Windows ARM64 reached the desktop after its first-boot restart,
  using KVM with PMU enabled, 4 vCPUs and 4 GiB RAM. No TCG was involved.
- The registration-enabled module retained one reference intentionally.

Original tested binary SHA-256 (not a hash of the renamed build):
`fb4ebff41fe7e347dc32ab9b29facd9473c193b1523bdfc2428e00313e03c654`.

Still unvalidated: PMU overflow interrupts, CPU hotplug, repeated VM lifecycles,
sustained load, pre-existing perf events across hook installation, protected
guests, and other firmware/kernel versions. The runtime checks are partial;
this record does not establish production stability.
