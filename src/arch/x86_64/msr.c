/*
 * Copyright 2025, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sddf/util/util.h>
#include <libvmm/guest.h>
#include <libvmm/util/util.h>
#include <libvmm/arch/x86_64/msr.h>
#include <libvmm/arch/x86_64/vcpu.h>
#include <libvmm/arch/x86_64/virt.h>
#include <libvmm/arch/x86_64/fault.h>
#include <libvmm/arch/x86_64/guest_time.h>
#include <libvmm/arch/x86_64/memory_space.h>
#include <libvmm/arch/x86_64/mtrr.h>
#include <libvmm/arch/x86_64/machine_check.h>

#include <x86intrin.h>

/* Documents referenced:
 * 1. Intel® 64 and IA-32 Architectures Software Developer’s Manual
 *    Combined Volumes: 1, 2A, 2B, 2C, 2D, 3A, 3B, 3C, 3D, and 4
 *    Order Number: 325462-080US June 2023
 */

/* [1] "Table 2-2. IA-32 Architectural MSRs" */
#define IA32_TIME_STAMP_COUNTER (0x10)
#define IA32_PLATFORM_ID (0x17)
#define IA32_SPEC_CTRL (0x48)
#define IA32_PRED_CMD (0x49)
#define IA32_BIOS_SIGN_ID (0x8b)
#define IA32_MISC_ENABLE (0x1a0)
#define IA32_PAT (0x277)
#define IA32_XSS (0xda0)

#define IA32_APIC_BASE (0x1b)
#define IA32_FEATURE_CONTROL (0x3a)
#define IA32_TSC_ADJUST (0x3b)
#define IA32_SYSENTER_CS (0x174)
#define IA32_SYSENTER_ESP (0x175)
#define IA32_SYSENTER_EIP (0x176)
#define IA32_DEBUGCTL (0x1d9)

/* x86-64 specific MSRs */
#define MSR_EFER            0xc0000080 /* extended feature register */
#define MSR_STAR            0xc0000081 /* legacy mode SYSCALL target */
#define MSR_LSTAR           0xc0000082 /* long mode SYSCALL target */
#define MSR_CSTAR           0xc0000083 /* compat mode SYSCALL target */
#define MSR_SYSCALL_MASK    0xc0000084 /* EFLAGS mask for syscall */
#define MSR_FS_BASE         0xc0000100
#define MSR_GS_BASE         0xc0000101
#define MSR_KERNEL_GS_BASE  0xc0000102

static uint64_t msr_star, msr_lstar, msr_cstar, msr_syscall_mask;
static uint64_t msr_fs_base, msr_gs_base, msr_kernel_gs_base;
static uint64_t msr_sysenter_cs, msr_sysenter_esp, msr_sysenter_eip;
static uint64_t msr_misc_enable;
static uint64_t msr_tsc_adjust;

static bool msrs_initialised = false;
static uint32_t apic_base_msr_mask = 0;

bool initialise_msrs(bool bsp)
{
    if (bsp) {
        /* Figure 11-5. IA32_APIC_BASE MSR (APIC_BASE_MSR in P6 Family)
         * Is a boot strap processor. */
        apic_base_msr_mask = BIT(8);

        if (!initialise_mtrr()) {
            LOG_VMM_ERR("Failed to initialise MTRR\n");
            return false;
        }
    }

    msrs_initialised = true;

    return true;
}

bool emulate_rdmsr(seL4_VCPUContext *vctx)
{
    if (!msrs_initialised) {
        LOG_VMM_ERR("MSRs not initialised!\n");
        return false;
    }

    uint64_t result = 0;

    if (msr_is_mtrr(vctx->ecx, true)) {
        if (!handle_mtrr_msr_read(vctx->ecx, &result)) {
            LOG_VMM_ERR("Failed to read MTRR MSR 0x%lx\n", vctx->ecx);
            return false;
        }
    } else if (msr_is_machine_check(vctx->ecx, true)) {
        if (!handle_machine_check_msr_read(vctx->ecx, &result)) {
            LOG_VMM_ERR("Failed to read machine check MSR 0x%lx\n", vctx->ecx);
            return false;
        }
    } else {
        switch (vctx->ecx) {
        case MSR_EFER:
            result = libvmm_vcpu_read(GUEST_BOOT_VCPU_ID, LIBVMM_GUEST_EFER);
            break;
        case IA32_TIME_STAMP_COUNTER:
            result = guest_time_tsc_now();
            break;
        case MSR_STAR:
            result = msr_star;
            break;
        case MSR_LSTAR:
            result = msr_lstar;
            break;
        case MSR_CSTAR:
            result = msr_cstar;
            break;
        case MSR_SYSCALL_MASK:
            result = msr_syscall_mask;
            break;
        case MSR_FS_BASE:
            result = libvmm_vcpu_read(GUEST_BOOT_VCPU_ID, VMCB_STATE_FS + 8);
            msr_fs_base = result;
            break;
        case MSR_GS_BASE:
            result = libvmm_vcpu_read(GUEST_BOOT_VCPU_ID, VMCB_STATE_GS + 8);
            msr_gs_base = result;
            break;
        case MSR_KERNEL_GS_BASE:
            result = msr_kernel_gs_base;
            break;
        case IA32_SYSENTER_CS:
            result = msr_sysenter_cs;
            break;
        case IA32_SYSENTER_ESP:
            result = msr_sysenter_esp;
            break;
        case IA32_SYSENTER_EIP:
            result = msr_sysenter_eip;
            break;
        case IA32_APIC_BASE:
            /* Figure 11-5. IA32_APIC_BASE MSR (APIC_BASE_MSR in P6 Family)
             *                   enable    is boot cpu? */
            result = LAPIC_GPA | BIT(11) | apic_base_msr_mask;
            break;
        case IA32_SPEC_CTRL:
            // @billn revisit, I think we should use Virtualize IA32_SPEC_CTRL
            // in Tertiary Processor-Based VM-Execution Controls
            break;
        case IA32_PAT:
            result = libvmm_vcpu_read(GUEST_BOOT_VCPU_ID, LIBVMM_GUEST_PAT);
            break;
        case IA32_MISC_ENABLE:
            result = msr_misc_enable;
            break;
        case IA32_PLATFORM_ID:
        case IA32_TSC_ADJUST:
            result = msr_tsc_adjust;
            break;
        case IA32_BIOS_SIGN_ID:
        case IA32_FEATURE_CONTROL:
        case IA32_DEBUGCTL:
        case IA32_XSS:
            result = 0;
            break;
        default:
            LOG_VMM_ERR("unhandled RDMSR 0x%lx\n", (unsigned long)vctx->ecx);
            return false;
        }
    }

    vctx->eax = result & 0xffffffff;
    vctx->edx = (result >> 32) & 0xffffffff;
    return true;
}

bool emulate_wrmsr(seL4_VCPUContext *vctx)
{
    if (!msrs_initialised) {
        LOG_VMM_ERR("MSRs not initialised!\n");
        return false;
    }

    uint64_t value = (uint64_t)((vctx->edx & 0xffffffff) << 32) | (uint64_t)(vctx->eax & 0xffffffff);

    if (msr_is_mtrr(vctx->ecx, false)) {
        if (!handle_mtrr_msr_write(vctx->ecx, value)) {
            LOG_VMM_ERR("Failed to write MTRR MSR 0x%lx, value 0x%lx\n", vctx->ecx, value);
            return false;
        }
    } else if (msr_is_machine_check(vctx->ecx, false)) {
        if (!handle_machine_check_msr_write(vctx->ecx, value)) {
            LOG_VMM_ERR("Failed to write machine check MSR 0x%lx, value 0x%lx\n", vctx->ecx, value);
            return false;
        }
    } else {
        switch (vctx->ecx) {
        case IA32_APIC_BASE:
            /* Make sure that the guest isn't transitioning our virtual APIC into an invalid state.
             * See Figure 11-5. IA32_APIC_BASE MSR (APIC_BASE_MSR in P6 Family) for bit definitions. */
            if (value & BIT(10)) {
                LOG_VMM_ERR("guest tried to enable x2APIC via IA32_APIC_BASE\n");
                return false;
            }

            uint64_t requested_apic_base = value & ~0xFFFull;
            if (requested_apic_base != LAPIC_GPA) {
                LOG_VMM_ERR("Guest tried to relocate APIC base to 0x%lx\n", requested_apic_base);
                return false;
            }

            if (!(value & BIT(11))) {
                LOG_VMM_ERR("Guest tried to globally disable the APIC (not supported)\n");
                return false;
            }

            break;
        case MSR_EFER:
            libvmm_vcpu_write(GUEST_BOOT_VCPU_ID, LIBVMM_GUEST_EFER, value);
            break;
        case IA32_PAT:
            libvmm_vcpu_write(GUEST_BOOT_VCPU_ID, LIBVMM_GUEST_PAT, value);
            break;
        case MSR_STAR:
            msr_star = value;
            break;
        case MSR_LSTAR:
            msr_lstar = value;
            break;
        case MSR_CSTAR:
            msr_cstar = value;
            break;
        case MSR_SYSCALL_MASK:
            msr_syscall_mask = value;
            break;
        case MSR_FS_BASE:
            msr_fs_base = value;
            /* Keep VMCB FS.base in sync; clear CLEAN.SEG so VMRUN reloads it. */
            libvmm_vcpu_write(GUEST_BOOT_VCPU_ID, VMCB_STATE_FS + 8, value);
            libvmm_vcpu_write(GUEST_BOOT_VCPU_ID, VMCB_CONTROL_CLEAN, 0);
            break;
        case MSR_GS_BASE:
            msr_gs_base = value;
            libvmm_vcpu_write(GUEST_BOOT_VCPU_ID, VMCB_STATE_GS + 8, value);
            libvmm_vcpu_write(GUEST_BOOT_VCPU_ID, VMCB_CONTROL_CLEAN, 0);
            break;
        case MSR_KERNEL_GS_BASE:
            msr_kernel_gs_base = value;
            break;
        case IA32_SYSENTER_CS:
            msr_sysenter_cs = value;
            break;
        case IA32_SYSENTER_ESP:
            msr_sysenter_esp = value;
            break;
        case IA32_SYSENTER_EIP:
            msr_sysenter_eip = value;
            break;
        case IA32_MISC_ENABLE:
            msr_misc_enable = value;
            break;
        case IA32_TSC_ADJUST:
            msr_tsc_adjust = value;
            break;
        case IA32_PRED_CMD:
        case IA32_SPEC_CTRL:
            // @billn revisit, security concerns same as IA32_SPEC_CTRL, as they are used for speculative exec controls
            break;
        case IA32_XSS:
        case IA32_DEBUGCTL:
        case IA32_BIOS_SIGN_ID:
            break;
        default:
            LOG_VMM_ERR("unhandled WRMSR 0x%lx val=0x%lx\n", (unsigned long)vctx->ecx,
                        (unsigned long)value);
            return false;
        }
    }

    return true;
}
