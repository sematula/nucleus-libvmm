/*
 * Copyright 2026, Valence contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * VMCB field offsets, intercept bits, and exit codes adapted from FreeBSD
 * sys/amd64/vmm/amd/vmcb.h (BSD-2-Clause). Layout matches AMD APM Vol 2
 * Appendix B and seL4 include/arch/x86/arch/object/svm_vmcb.h.
 */

#pragma once

#include <microkit.h>
#include <libvmm/util/util.h>

/* Control area (absolute offsets from VMCB base). */
#define VMCB_CONTROL_INTERCEPT_CR           0x000
#define VMCB_CONTROL_INTERCEPT_DR           0x004
#define VMCB_CONTROL_INTERCEPT_EXC          0x008
#define VMCB_CONTROL_INTERCEPT_INSTRUCTION1 0x00C
#define VMCB_CONTROL_INTERCEPT_INSTRUCTION2 0x010
#define VMCB_CONTROL_IOPM_BASE_PA           0x040
#define VMCB_CONTROL_MSRPM_BASE_PA          0x048
#define VMCB_CONTROL_TSC_OFFSET             0x050
#define VMCB_CONTROL_GUEST_ASID             0x058
#define VMCB_CONTROL_TLB_CONTROL            0x05C
#define VMCB_CONTROL_V_TPR                  0x060
#define VMCB_CONTROL_INTERRUPT_SHADOW       0x068
#define VMCB_CONTROL_EXITCODE               0x070
#define VMCB_CONTROL_EXITINFO1              0x078
#define VMCB_CONTROL_EXITINFO2              0x080
#define VMCB_CONTROL_EXITINTINFO            0x088
#define VMCB_CONTROL_NP_ENABLE              0x090
#define VMCB_CONTROL_EVENTINJ               0x0A8
#define VMCB_CONTROL_NCR3                   0x0B0
#define VMCB_CONTROL_NRIP                   0x0C8

/* State-save area begins at 0x400. Each segment is a 16-byte record:
 * selector (u16), attrib (u16), limit (u32), base (u64). */
#define VMCB_STATE_SAVE_BASE                0x400
#define VMCB_STATE_ES                       0x400
#define VMCB_STATE_CS                       0x410
#define VMCB_STATE_SS                       0x420
#define VMCB_STATE_DS                       0x430
#define VMCB_STATE_FS                       0x440
#define VMCB_STATE_GS                       0x450
#define VMCB_STATE_GDTR                     0x460
#define VMCB_STATE_LDTR                     0x470
#define VMCB_STATE_IDTR                     0x480
#define VMCB_STATE_TR                       0x490

#define VMCB_STATE_GUEST_CPL                0x4CB
#define VMCB_STATE_GUEST_EFER               0x4D0
#define VMCB_STATE_GUEST_CR4                0x548
#define VMCB_STATE_GUEST_CR3                0x550
#define VMCB_STATE_GUEST_CR0                0x558
#define VMCB_STATE_GUEST_DR7                0x560
#define VMCB_STATE_GUEST_DR6                0x568
#define VMCB_STATE_GUEST_RFLAGS             0x570
#define VMCB_STATE_GUEST_RIP                0x578
#define VMCB_STATE_GUEST_RSP                0x5D8
#define VMCB_STATE_GUEST_RAX                0x5F8
#define VMCB_STATE_GUEST_PAT                0x668

#ifndef PAT_RESET_VALUE
#define PAT_RESET_VALUE 0x0007040600070406ull
#endif

/* intercept[INSTRUCTION1] bits (FreeBSD VMCB_INTCPT_*). */
#define VMCB_INTERCEPT_CPUID                BIT(18)
#define VMCB_INTERCEPT_HLT                  BIT(24)
#define VMCB_INTERCEPT_IO                   BIT(27)
#define VMCB_INTERCEPT_MSR                  BIT(28)
#define VMCB_INTERCEPT_SHUTDOWN             BIT(31)

/* intercept[INSTRUCTION2] bits. */
#define VMCB_INTERCEPT_VMRUN                BIT(0)
#define VMCB_INTERCEPT_VMMCALL              BIT(1)
#define VMCB_INTERCEPT_XSETBV               BIT(13)

/* Event injection (EVENTINJ / EXITINTINFO) field bits. */
#define VMCB_EVENTINJ_TYPE_INTR             0
#define VMCB_EVENTINJ_TYPE_NMI              2
#define VMCB_EVENTINJ_TYPE_EXCEPTION        3
#define VMCB_EVENTINJ_EC_VALID              BIT(11)
#define VMCB_EVENTINJ_VALID                 BIT(31)

/* Nested page fault EXITINFO1 bits. */
#define VMCB_NPF_INFO1_P                    BIT(0)
#define VMCB_NPF_INFO1_W                    BIT(1)
#define VMCB_NPF_INFO1_U                    BIT(2)
#define VMCB_NPF_INFO1_RSV                  BIT(3)
#define VMCB_NPF_INFO1_ID                   BIT(4)

/* Exit codes (AMD APM Vol 2 Appendix C / FreeBSD VMCB_EXIT_*). */
#define VMCB_EXIT_CPUID                     0x072ULL
#define VMCB_EXIT_HLT                       0x078ULL
#define VMCB_EXIT_IO                        0x07BULL
#define VMCB_EXIT_MSR                       0x07CULL
#define VMCB_EXIT_SHUTDOWN                  0x07FULL
#define VMCB_EXIT_VMRUN                     0x080ULL
#define VMCB_EXIT_XSETBV                    0x08DULL
#define VMCB_EXIT_NPF                       0x400ULL

/* Aliases matching seL4 svm_vmcb.h naming. */
#define SVM_EXIT_CPUID                      VMCB_EXIT_CPUID
#define SVM_EXIT_HLT                        VMCB_EXIT_HLT
#define SVM_EXIT_IO                         VMCB_EXIT_IO
#define SVM_EXIT_MSR                        VMCB_EXIT_MSR
#define SVM_EXIT_VMRUN                      VMCB_EXIT_VMRUN
#define SVM_EXIT_NPF                        VMCB_EXIT_NPF

/* VMCB 12-bit segment attribute helpers (type|S|DPL|P|AVL|L|D|G). */
#define VMCB_SEG_ATTRIB_TYPE_CODE           0xB
#define VMCB_SEG_ATTRIB_TYPE_DATA           0x3
#define VMCB_SEG_ATTRIB_TYPE_LDT            0x2
#define VMCB_SEG_ATTRIB_TYPE_TSS_BUSY       0xB
#define VMCB_SEG_ATTRIB_S                   BIT(4)
#define VMCB_SEG_ATTRIB_P                   BIT(7)
#define VMCB_SEG_ATTRIB_L                   BIT(9)
#define VMCB_SEG_ATTRIB_DB                  BIT(10)
#define VMCB_SEG_ATTRIB_G                   BIT(11)

/* Long-mode CS: present ring-0 64-bit code, G=1 → 0xA9B. */
#define VMCB_CS_ATTRIB_LONG_MODE \
    (VMCB_SEG_ATTRIB_TYPE_CODE | VMCB_SEG_ATTRIB_S | VMCB_SEG_ATTRIB_P | VMCB_SEG_ATTRIB_L | VMCB_SEG_ATTRIB_G)
/* Long-mode data: present ring-0 data, D=1, G=1 → 0xC93. */
#define VMCB_DS_ATTRIB_LONG_MODE \
    (VMCB_SEG_ATTRIB_TYPE_DATA | VMCB_SEG_ATTRIB_S | VMCB_SEG_ATTRIB_P | VMCB_SEG_ATTRIB_DB | VMCB_SEG_ATTRIB_G)
/* Real-mode-like code/data after reset. */
#define VMCB_CS_ATTRIB_RESET \
    (VMCB_SEG_ATTRIB_TYPE_CODE | VMCB_SEG_ATTRIB_S | VMCB_SEG_ATTRIB_P)
#define VMCB_DS_ATTRIB_RESET \
    (VMCB_SEG_ATTRIB_TYPE_DATA | VMCB_SEG_ATTRIB_S | VMCB_SEG_ATTRIB_P)

/*
 * seL4 WriteVMCB stores a word_t at the given offset. Pack selector|attrib|limit
 * into the first 8 bytes and base into the next 8.
 */
static inline void vmcb_write_segment(microkit_child vcpu, seL4_Word seg_off, uint16_t selector,
                                      uint16_t attrib, uint32_t limit, uint64_t base)
{
    uint64_t word0 = (uint64_t)selector | ((uint64_t)attrib << 16) | ((uint64_t)limit << 32);
    microkit_vcpu_x86_write_vmcb(vcpu, seg_off, word0);
    microkit_vcpu_x86_write_vmcb(vcpu, seg_off + 8, base);
}
