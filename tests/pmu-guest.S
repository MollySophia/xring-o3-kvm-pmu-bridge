/* SPDX-License-Identifier: GPL-2.0-only */
.text
.global _start
_start:
    mov x0, #0x10000000
    mrs x1, id_aa64dfr0_el1
    str x1, [x0]
    mrs x1, pmcr_el0
    str x1, [x0]
    mrs x1, pmccntr_el0
    str x1, [x0]
    mov x1, #0x47
    msr pmcr_el0, x1
    mov x1, #0x80000000
    msr pmcntenset_el0, x1
    isb
    mov x2, #0x4240
    movk x2, #0xf, lsl #16
1:  subs x2, x2, #1
    b.ne 1b
    mrs x1, pmccntr_el0
    str x1, [x0]
    mrs x1, pmccntr_el0
    str x1, [x0]
    mov x1, #0x80000000
    msr pmcntenclr_el0, x1
    mov x1, #0x600d
    str x1, [x0]
2:  wfi
    b 2b
