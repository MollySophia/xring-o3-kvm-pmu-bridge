/* SPDX-License-Identifier: GPL-2.0-only */
#define _GNU_SOURCE
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sched.h>
int main(){cpu_set_t mask;CPU_ZERO(&mask);CPU_SET(0,&mask);if(sched_setaffinity(0,sizeof(mask),&mask))perror("affinity");for(int guest=0;guest<2;guest++){struct perf_event_attr a;memset(&a,0,sizeof(a));a.type=11;a.size=sizeof(a);a.config=0x11;a.disabled=1;a.exclude_hv=1;a.exclude_host=guest;int fd=syscall(__NR_perf_event_open,&a,0,-1,-1,0);if(fd<0){printf("exclude_host=%d open: %s\n",guest,strerror(errno));continue;}ioctl(fd,PERF_EVENT_IOC_RESET,0);ioctl(fd,PERF_EVENT_IOC_ENABLE,0);volatile uint64_t z=1;for(unsigned i=0;i<5000000;i++)z=z*1664525+1013904223;ioctl(fd,PERF_EVENT_IOC_DISABLE,0);uint64_t v=0;ssize_t n=read(fd,&v,sizeof(v));printf("exclude_host=%d bytes=%zd cycles=%llu\n",guest,n,(unsigned long long)v);close(fd);}return 0;}
