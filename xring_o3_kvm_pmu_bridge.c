// SPDX-License-Identifier: GPL-2.0
/* Device-specific experiment, guarded against the inspected Xring ABI. */
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/perf/arm_pmu.h>
#include <linux/stop_machine.h>
#include <linux/cfi.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>

static bool register_pmu;
module_param(register_pmu, bool, 0444);
MODULE_PARM_DESC(register_pmu, "Register with KVM and retain module until reboot");
static struct arm_pmu adapter;
struct bridge_pmu_entry { struct list_head entry; struct arm_pmu *pmu; };
static u64 cpu_ceid[NR_CPUS][2];

static struct pmu *vendor;
static struct module *vendor_owner;
static unsigned long __percpu *counter_mask;
static void (*original_enable)(struct perf_event *);
static void (*original_disable)(struct perf_event *);
static void (*original_reset)(void *);
static void (*set_events)(u64, struct perf_event_attr *);
static void (*clear_events)(u64);
static unsigned int vendor_version;

static unsigned long resolve_probe(const char *name)
{
	struct kprobe kp = { .symbol_name = name };
	unsigned long addr;
	if (register_kprobe(&kp))
		return 0;
	addr = (unsigned long)kp.addr;
	unregister_kprobe(&kp);
	return addr;
}

static noinline unsigned long model_lookup(const char *name) { return 0; }
static noinline void model_set(u64 mask, struct perf_event_attr *attr) {}
static noinline void model_clear(u64 mask) {}
static noinline void model_register(struct arm_pmu *pmu) {}

static void read_ceid(void *unused)
{
	unsigned int cpu = smp_processor_id();
	cpu_ceid[cpu][0] = read_sysreg(pmceid0_el0);
	cpu_ceid[cpu][1] = read_sysreg(pmceid1_el0);
}

static bool same_cfi(void *actual, void *model)
{
	u32 a = cfi_get_func_hash(actual), b = cfi_get_func_hash(model);
	if (!a || a != b) {
		pr_err("xring_o3_kvm_pmu_bridge: CFI mismatch %08x / %08x\n", a, b);
		return false;
	}
	return true;
}

static u64 event_mask(struct perf_event *event)
{
	int idx = event->hw.idx;
	u64 mask = BIT_ULL(idx);
	if (!(event->hw.flags & PERF_EVENT_FLAG_USER_READ_CNT) &&
	    (event->attr.config1 & 1) && vendor_version < 6 && idx > 0 && idx < 31)
		mask |= BIT_ULL(idx - 1);
	return mask;
}

static void bridge_enable(struct perf_event *event)
{
	u64 mask = event_mask(event);
	original_enable(event);
	set_events(mask, &event->attr);
	/* This inspected device runs the host at EL1 (nVHE). The vendor's
	 * start callback must not enable guest-only counters while on the host.
	 */
	if (event->attr.exclude_host)
		this_cpu_and(*counter_mask, ~mask);
}

static void bridge_disable(struct perf_event *event)
{
	u64 mask = event_mask(event);
	original_disable(event);
	clear_events(mask);
}

static void bridge_reset(void *pmu)
{
	original_reset(pmu);
	clear_events(~0ULL);
}

static int swap_callbacks(void *install)
{
	if (smp_processor_id() == cpumask_first(cpu_online_mask)) {
		WRITE_ONCE(*(void **)((char *)vendor + 352), install ? bridge_enable : original_enable);
		WRITE_ONCE(*(void **)((char *)vendor + 360), install ? bridge_disable : original_disable);
		WRITE_ONCE(*(void **)((char *)vendor + 424), install ? bridge_reset : original_reset);
	}
	if (!install)
		clear_events(~0ULL);
	return 0;
}

static int __init bridge_init(void)
{
	unsigned long (*lookup)(const char *);
	struct perf_event_attr attr = { .type = 11, .size = sizeof(attr),
		.config = 0x11, .disabled = 1, .exclude_hv = 1 };
	struct perf_event *event;
	u64 snapshot[73];
	void (*host_register)(struct arm_pmu *) = NULL;
	struct list_head *pmus = NULL;
	struct mutex *pmus_lock = NULL;
	struct bridge_pmu_entry *entry;
	bool found = false;
	u64 ceid0 = ~0ULL, ceid1 = ~0ULL;
	int cpu;
	int ret;

	if (read_sysreg(CurrentEL) != 4)
		return -EOPNOTSUPP;
	lookup = (void *)resolve_probe("kallsyms_lookup_name");
	if (!lookup || !same_cfi(lookup, model_lookup))
		return -EINVAL;
	if (register_pmu) {
		host_register = (void *)lookup("kvm_host_pmu_init");
		pmus = (void *)lookup("arm_pmus");
		pmus_lock = (void *)lookup("arm_pmus_lock");
		if (!host_register || !pmus || !pmus_lock || !same_cfi(host_register, model_register))
			return -EINVAL;
		mutex_lock(pmus_lock);
		found = list_empty(pmus);
		mutex_unlock(pmus_lock);
		if (!found)
			return -EBUSY;
		found = false;
	}
	set_events = (void *)lookup("kvm_set_pmu_events");
	clear_events = (void *)lookup("kvm_clr_pmu_events");
	counter_mask = (void *)lookup("xring_arm_pmu:pmu_counter_mask");
	if (!set_events || !clear_events || !counter_mask ||
	    !same_cfi(set_events, model_set) || !same_cfi(clear_events, model_clear))
		return -EINVAL;
	event = perf_event_create_kernel_counter(&attr, 0, NULL, NULL, NULL);
	if (IS_ERR(event))
		return PTR_ERR(event);
	vendor = event->pmu;
	ret = copy_from_kernel_nofault(snapshot, vendor, sizeof(snapshot));
	if (ret)
		goto release;
	original_enable = (void *)snapshot[44];
	original_disable = (void *)snapshot[45];
	original_reset = (void *)snapshot[53];
	vendor_version = *(u32 *)((char *)snapshot + 336);
	if (vendor->type != 11 || snapshot[40] != 0x3ff || snapshot[55] != 0x8000007f ||
	    (unsigned long)original_enable != lookup("xring_arm_pmu:armv8pmu_enable_event") ||
	    (unsigned long)original_disable != lookup("xring_arm_pmu:armv8pmu_disable_event") ||
	    (unsigned long)original_reset != lookup("xring_arm_pmu:armv8pmu_reset") ||
	    !same_cfi(original_enable, bridge_enable) ||
	    !same_cfi(original_disable, bridge_disable) ||
	    !same_cfi(original_reset, bridge_reset)) {
		ret = -EINVAL;
		goto release;
	}
	vendor_owner = vendor->module;
	if (!try_module_get(vendor_owner)) {
		ret = -ENODEV;
		goto release;
	}
	if (register_pmu) {
		adapter.pmu.type = vendor->type;
		adapter.name = "xring-o3-kvm-pmu-bridge";
		adapter.pmuver = vendor_version;
		adapter.cntr_mask[0] = snapshot[55];
		cpus_read_lock();
		cpumask_copy(&adapter.supported_cpus, cpu_online_mask);
		on_each_cpu(read_ceid, NULL, 1);
		for_each_online_cpu(cpu) {
			ceid0 &= cpu_ceid[cpu][0];
			ceid1 &= cpu_ceid[cpu][1];
		}
		cpus_read_unlock();
		adapter.pmceid_bitmap[0] = (ceid0 & 0xffffffff) | (ceid1 << 32);
		adapter.pmceid_ext_bitmap[0] = (ceid0 >> 32) | (ceid1 & 0xffffffff00000000ULL);
	}
	ret = stop_machine(swap_callbacks, (void *)1, cpu_online_mask);
	if (ret)
		module_put(vendor_owner);
	else if (!register_pmu)
		pr_info("xring_o3_kvm_pmu_bridge: accounting hooks installed; vendor PMU version=%u; KVM registration unchanged\n", vendor_version);
	else {
		/* KVM retains raw metadata references. There is no unregister API;
		 * do not permit this module's storage or callbacks to be unloaded.
		 */
		__module_get(THIS_MODULE);
		host_register(&adapter);
		mutex_lock(pmus_lock);
		list_for_each_entry(entry, pmus, entry)
			if (entry->pmu == &adapter)
				found = true;
		mutex_unlock(pmus_lock);
		if (!found) {
			module_put(THIS_MODULE);
			stop_machine(swap_callbacks, NULL, cpu_online_mask);
			module_put(vendor_owner);
			ret = -ENODEV;
		} else {
			pr_info("xring_o3_kvm_pmu_bridge: KVM PMU registered type=%d mask=%lx ceid=%llx/%llx; retained until reboot\n",
				adapter.pmu.type, adapter.cntr_mask[0], ceid0, ceid1);
		}
	}
release:
	perf_event_release_kernel(event);
	return ret;
}

static void __exit bridge_exit(void)
{
	stop_machine(swap_callbacks, NULL, cpu_online_mask);
	module_put(vendor_owner);
	pr_info("xring_o3_kvm_pmu_bridge: original callbacks restored\n");
}

module_init(bridge_init);
module_exit(bridge_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Experimental Xring O3 KVM PMU accounting and registration bridge");
