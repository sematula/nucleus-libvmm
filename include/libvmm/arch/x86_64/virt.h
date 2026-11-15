/*
 * Copyright 2026, Valence contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Thin VT-x / SVM selection layer for guest-state field access.
 * One virt ISA per image: CONFIG_VTX or CONFIG_SVM, not both.
 */

#pragma once

#include <microkit.h> /* CONFIG_SVM / CONFIG_VTX from board gen_config */

#if defined(CONFIG_SVM)

#include <libvmm/arch/x86_64/vmcb.h>

#define libvmm_vcpu_read(v, f)          microkit_vcpu_x86_read_vmcb((v), (f))
#define libvmm_vcpu_write(v, f, val)    microkit_vcpu_x86_write_vmcb((v), (f), (val))

#define LIBVMM_GUEST_RIP                VMCB_STATE_GUEST_RIP
#define LIBVMM_GUEST_RSP                VMCB_STATE_GUEST_RSP
#define LIBVMM_GUEST_RFLAGS             VMCB_STATE_GUEST_RFLAGS
#define LIBVMM_GUEST_CR0                VMCB_STATE_GUEST_CR0
#define LIBVMM_GUEST_CR3                VMCB_STATE_GUEST_CR3
#define LIBVMM_GUEST_CR4                VMCB_STATE_GUEST_CR4
#define LIBVMM_GUEST_EFER               VMCB_STATE_GUEST_EFER
#define LIBVMM_GUEST_PAT                VMCB_STATE_GUEST_PAT

/*
 * LIBVMM_CONTROL_PPC aliases INTERCEPT_INSTRUCTION1. microkit_vcpu_x86_write_vmcb
 * is a real seL4 WriteVMCB — it updates the VMCB, not only the SysVMEnter cache.
 * SysVMEnter itself applies RIP + EVENTINJ and ignores the PPC *message* arg;
 * explicit WriteVMCB of this field still changes intercepts. Never write 0 here.
 * MSR intercept stays off until a dedicated MSRPM exists (seL4 svm_vcpu.c).
 *
 * CRITICAL: seL4 vmcb_write stores a full word_t (8 bytes). Adjacent VMCB u32
 * fields share a word (EXC@0x008+INTERCEPT1@0x00C, INTERCEPT1+INTERCEPT2@0x010).
 * Blind 8-byte writes zero the neighbour — e.g. writing INTERCEPT1 clears
 * INTERCEPT2 (VMRUN), and writing EXC clears INTERCEPT1 (CPUID). Use
 * svm_vmcb_write_u32() for these fields.
 */
#define LIBVMM_CONTROL_PPC              VMCB_CONTROL_INTERCEPT_INSTRUCTION1

/*
 * Prefer EVENTINJ (0xA8) for injection: Microkit caches EXITINTINFO (0x088) as
 * irq_info, but the kernel ignores that cache on enter. EVENTINJ is not in the
 * cached switch, so write_vmcb goes to real WriteVMCB.
 */
#define LIBVMM_CONTROL_IRQ              VMCB_CONTROL_EVENTINJ

/*
 * Soft-APIC IRQ-window arming has no SVM equivalent of VT-x interrupt-window
 * exiting. PPVC_* are INTERCEPT1 bitmasks (not VT-x primary controls). Keep the
 * same safe defaults for both; pending IRQs retry on subsequent exits.
 * Must match seL4 set_default_intercepts() (+ guest must not see host KVM).
 */
/* MSR intercept on: reuse seL4 IOPM page as all-1s MSRPM placeholder (nested
 * KVM requires a legal MSRPM GPA when the intercept is set). Needed so L2
 * cannot RDMSR host IA32_APIC_BASE / x2APIC and bypass soft LAPIC MMIO. */
#define LIBVMM_SVM_INTERCEPT1_DEFAULT \
    (VMCB_INTERCEPT_CPUID | VMCB_INTERCEPT_HLT | VMCB_INTERCEPT_IO | VMCB_INTERCEPT_MSR \
     | VMCB_INTERCEPT_SHUTDOWN)
#define LIBVMM_SVM_INTERCEPT2_DEFAULT \
    (VMCB_INTERCEPT_VMRUN | VMCB_INTERCEPT_XSETBV)
#define LIBVMM_PPVC_DEFAULT             LIBVMM_SVM_INTERCEPT1_DEFAULT
#define LIBVMM_PPVC_WAIT_IRQ_WINDOW     LIBVMM_SVM_INTERCEPT1_DEFAULT

/* Preserve the other u32 in the same 8-byte VMCB word. field must be 4-byte aligned. */
static inline void svm_vmcb_write_u32(seL4_Word field, uint32_t value)
{
    seL4_Word aligned = field & ~7ull;
    seL4_Word word = libvmm_vcpu_read(0, aligned);
    if (field & 4) {
        word = (word & 0xffffffffull) | ((seL4_Word)value << 32);
    } else {
        word = (word & 0xffffffff00000000ull) | value;
    }
    libvmm_vcpu_write(0, aligned, word);
}

#elif defined(CONFIG_VTX)

#include <libvmm/arch/x86_64/vmcs.h>

#define libvmm_vcpu_read(v, f)          microkit_vcpu_x86_read_vmcs((v), (f))
#define libvmm_vcpu_write(v, f, val)    microkit_vcpu_x86_write_vmcs((v), (f), (val))

#define LIBVMM_GUEST_RIP                VMX_GUEST_RIP
#define LIBVMM_GUEST_RSP                VMX_GUEST_RSP
#define LIBVMM_GUEST_RFLAGS             VMX_GUEST_RFLAGS
#define LIBVMM_GUEST_CR0                VMX_GUEST_CR0
#define LIBVMM_GUEST_CR3                VMX_GUEST_CR3
#define LIBVMM_GUEST_CR4                VMX_GUEST_CR4
#define LIBVMM_GUEST_EFER               VMX_GUEST_EFER
#define LIBVMM_GUEST_PAT                VMX_GUEST_PAT

#define LIBVMM_CONTROL_PPC              VMX_CONTROL_PRIMARY_PROCESSOR_CONTROLS
#define LIBVMM_CONTROL_IRQ              VMX_CONTROL_ENTRY_INTERRUPTION_INFO

#define LIBVMM_PPVC_DEFAULT             VMCS_PPVC_DEFAULT
#define LIBVMM_PPVC_WAIT_IRQ_WINDOW     VMCS_PPVC_WAIT_IRQ_WINDOW

#else
#error "libvmm x86_64 requires CONFIG_VTX or CONFIG_SVM (one virt ISA per image)"
#endif
