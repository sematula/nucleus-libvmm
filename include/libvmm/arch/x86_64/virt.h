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
 * Microkit caches INTERCEPT_INSTRUCTION1 (0x00C) in the SysVMEnter PPC slot;
 * the kernel SVM SysVMEnter path applies RIP + EVENTINJ and ignores arg1 for now.
 * Writing LIBVMM_CONTROL_PPC therefore only updates the local Microkit cache;
 * default kernel intercepts (CPUID/HLT/IO/SHUTDOWN/VMRUN) remain in force.
 * MSR intercept stays off until a real MSRPM exists.
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
 * exiting. SysVMEnter also ignores INTERCEPT updates. Keep the PPVC macros as
 * no-ops; pending IRQs are retried on subsequent exits (HLT/IO/NPF/…).
 */
#define LIBVMM_PPVC_DEFAULT             0
#define LIBVMM_PPVC_WAIT_IRQ_WINDOW     0

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
