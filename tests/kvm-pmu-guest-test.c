/* SPDX-License-Identifier: GPL-2.0-only */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/kvm.h>
#include <stddef.h>
#include <sched.h>
#include "pmu-guest.h"

static void check(int ret, const char *what) {
    if (ret < 0) { perror(what); exit(1); }
}
static void attr(int fd, uint32_t group, uint64_t key, void *value, const char *what) {
    struct kvm_device_attr a = { .group=group, .attr=key, .addr=(uintptr_t)value };
    check(ioctl(fd, KVM_SET_DEVICE_ATTR, &a), what);
}
static void reg(int fd, uint64_t id, uint64_t value) {
    struct kvm_one_reg r={ .id=id, .addr=(uintptr_t)&value };
    check(ioctl(fd,KVM_SET_ONE_REG,&r),"set reg");
}
int main(void) {
    setbuf(stdout,NULL);
    alarm(15);
    cpu_set_t mask; CPU_ZERO(&mask); CPU_SET(0,&mask);
    check(sched_setaffinity(0,sizeof(mask),&mask),"affinity");
    int kvm=open("/dev/kvm",O_RDWR|O_CLOEXEC); check(kvm,"open kvm");
    printf("PMUv3=%d\n",ioctl(kvm,KVM_CHECK_EXTENSION,KVM_CAP_ARM_PMU_V3));
    int vm=ioctl(kvm,KVM_CREATE_VM,0); check(vm,"create vm");
    void *mem=mmap(NULL,0x10000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(mem==MAP_FAILED){perror("mmap");return 1;}
    memcpy(mem,guest_code,sizeof(guest_code));
    struct kvm_userspace_memory_region mr={.slot=0,.guest_phys_addr=0x40000000,
        .memory_size=0x10000,.userspace_addr=(uintptr_t)mem};
    check(ioctl(vm,KVM_SET_USER_MEMORY_REGION,&mr),"memory");
    struct kvm_create_device dev={.type=KVM_DEV_TYPE_ARM_VGIC_V3};
    check(ioctl(vm,KVM_CREATE_DEVICE,&dev),"create vgic");
    int gic=dev.fd;
    uint64_t dist=0x08000000,redist=0x080a0000;
    attr(gic,KVM_DEV_ARM_VGIC_GRP_ADDR,KVM_VGIC_V3_ADDR_TYPE_DIST,&dist,"dist");
    attr(gic,KVM_DEV_ARM_VGIC_GRP_ADDR,KVM_VGIC_V3_ADDR_TYPE_REDIST,&redist,"redist");
    int vcpu=ioctl(vm,KVM_CREATE_VCPU,0); check(vcpu,"create vcpu");
    struct kvm_vcpu_init init={0};
    check(ioctl(vm,KVM_ARM_PREFERRED_TARGET,&init),"target");
    init.features[0]|=1u<<KVM_ARM_VCPU_PMU_V3;
    check(ioctl(vcpu,KVM_ARM_VCPU_INIT,&init),"vcpu init PMUv3");
    attr(gic,KVM_DEV_ARM_VGIC_GRP_CTRL,KVM_DEV_ARM_VGIC_CTRL_INIT,NULL,"gic init");
    int irq=23;
    attr(vcpu,KVM_ARM_VCPU_PMU_V3_CTRL,KVM_ARM_VCPU_PMU_V3_IRQ,&irq,"PMU irq");
    attr(vcpu,KVM_ARM_VCPU_PMU_V3_CTRL,KVM_ARM_VCPU_PMU_V3_INIT,NULL,"PMU init");
    uint64_t core=KVM_REG_ARM64|KVM_REG_SIZE_U64|KVM_REG_ARM_CORE;
    reg(vcpu,core|(offsetof(struct kvm_regs,regs.pc)/4),0x40000000);
    reg(vcpu,core|(offsetof(struct kvm_regs,regs.pstate)/4),0x3c5);
    int runsize=ioctl(kvm,KVM_GET_VCPU_MMAP_SIZE,0); check(runsize,"run size");
    struct kvm_run *run=mmap(NULL,runsize,PROT_READ|PROT_WRITE,MAP_SHARED,vcpu,0);
    if(run==MAP_FAILED){perror("run mmap");return 1;}
    const char *names[]={"DFR0","PMCR","initial cycle","after loop","after host sleep","done"};
    uint64_t values[6]={0};
    for(int i=0;i<6;i++) {
        check(ioctl(vcpu,KVM_RUN,0),"KVM_RUN");
        if(run->exit_reason!=KVM_EXIT_MMIO || !run->mmio.is_write || run->mmio.len!=8) {
            printf("unexpected exit=%u\n",run->exit_reason);return 2;
        }
        memcpy(&values[i],run->mmio.data,8);
        printf("%s=0x%llx (%llu)\n",names[i],(unsigned long long)values[i],(unsigned long long)values[i]);
        if(i==3)usleep(200000);
    }
    printf("host-sleep delta=%llu\n",(unsigned long long)(values[4]-values[3]));
    close(vcpu);close(gic);close(vm);close(kvm);
    return values[3]>0 && values[5]==0x600d ? 0 : 3;
}
