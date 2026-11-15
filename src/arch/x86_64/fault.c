/*
 * Copyright 2025, UNSW
 * Copyright 2026, Valence contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <string.h>
#include <sddf/util/util.h>
#include <libvmm/util/util.h>
#include <libvmm/arch/x86_64/fault.h>
#include <libvmm/arch/x86_64/ioports.h>
#include <libvmm/arch/x86_64/virt.h>
#include <libvmm/arch/x86_64/acpi.h>
#include <libvmm/arch/x86_64/vcpu.h>
#include <libvmm/arch/x86_64/cpuid.h>
#include <libvmm/arch/x86_64/msr.h>
#include <libvmm/arch/x86_64/apic.h>
#include <libvmm/arch/x86_64/hpet.h>
#include <libvmm/arch/x86_64/fpu.h>
#include <libvmm/arch/x86_64/instruction.h>
#include <libvmm/arch/x86_64/memory_space.h>
#include <libvmm/arch/x86_64/guest_time.h>
#include <libvmm/arch/x86_64/cr_access.h>
#include <libvmm/guest.h>
#include <sel4/arch/vmenter.h>

/* Uncomment to print out summary of VM Exit reasons every 100000 exit */
// #define VM_EXIT_TRACKING
#define VM_EXIT_PRINT_MULTIPLE 100000

/* Uncomment to print every time the VMM injects a General Protection
 * fault into the guest. Useful to debug Triple Faults. */
// #define LOG_GP

/* Documents referenced:
 * [1] seL4: include/arch/x86/arch/object/vcpu.h
 * [2] Title: Intel® 64 and IA-32 Architectures Software Developer’s Manual Combined Volumes: 1, 2A, 2B, 2C, 2D, 3A, 3B, 3C, 3D, and 4 Order Number: 325462-080US June 2023
 * [3] https://wiki.osdev.org/Interrupt_Vector_Table
 * [4] AMD64 Architecture Programmer’s Manual Volume 2 (SVM / VMCB exits)
 */

#define GP_VECTOR 13

#if defined(CONFIG_VTX)

/* [2] Table 29-7. "Exit Qualification for EPT Violations" */
#define EPT_VIOLATION_READ_BIT BIT(0)
#define EPT_VIOLATION_WRITE_BIT BIT(1)

enum exit_reasons {
    EXCEPTION_OR_NMI = 0x00,
    EXTERNAL_INTERRUPT = 0x01,
    TRIPLE_FAULT = 0x02,
    INIT_SIGNAL = 0x03,
    SIPI = 0x04,
    INTERRUPT_WINDOW = 0x07,
    NMI_WINDOW = 0x08,
    TASK_SWITCH = 0x09,
    CPUID = 0x0A,
    GETSEC = 0x0B,
    HLT = 0x0C,
    INVD = 0x0D,
    INVLPG = 0x0E,
    RDPMC = 0x0F,
    RDTSC = 0x10,
    RSM = 0x11,
    VMCALL = 0x12,
    VMCLEAR = 0x13,
    VMLAUNCH = 0x14,
    VMPTRLD = 0x15,
    VMPTRST = 0x16,
    VMREAD = 0x17,
    VMRESUME = 0x18,
    VMWRITE = 0x19,
    VMXOFF = 0x1A,
    VMXON = 0x1B,
    CONTROL_REGISTER = 0x1C,
    MOV_DR = 0x1D,
    IO = 0x1E,
    RDMSR = 0x1F,
    WRMSR = 0x20,
    INVALID_GUEST_STATE = 0x21,
    MSR_LOAD_FAIL = 0x22,
    MWAIT = 0x24,
    MONITOR_TRAP_FLAG = 0x25,
    MONITOR = 0x27,
    PAUSE = 0x28,
    MACHINE_CHECK = 0x29,
    TPR_BELOW_THRESHOLD = 0x2B,
    APIC_ACCESS = 0x2C,
    VIRTUALIZED_EOI = 0x2D,
    GDTR_OR_IDTR = 0x2E,
    LDTR_OR_TR = 0x2F,
    EPT_VIOLATION = 0x30,
    EPT_MISCONFIGURATION = 0x31,
    INVEPT = 0x32,
    RDTSCP = 0x33,
    VMX_PREEMPTION_TIMER = 0x34,
    INVVPID = 0x35,
    WBINVD = 0x36,
    XSETBV = 0x37,
    APIC_WRITE = 0x38,
    NUM_EXIT_REASONS = 0x39,
};

static char *exit_reason_strs[] = { "Exception or non-maskable interrupt (NMI)",
                                    "External interrupt",
                                    "Triple Fault",
                                    "INIT signal",
                                    "Start-up IPI (SIPI)",
                                    "I/O system-management interrupt (SMI)",
                                    "Other SMI",
                                    "Interrupt window",
                                    "NMI window",
                                    "Task switch",
                                    "CPUID",
                                    "GETSEC",
                                    "HLT",
                                    "INVD",
                                    "INVLPG",
                                    "RDPMC",
                                    "RDTSC",
                                    "RSM",
                                    "VMCALL",
                                    "VMCLEAR",
                                    "VMLAUNCH",
                                    "VMPTRLD",
                                    "VMPTRST",
                                    "VMREAD",
                                    "VMRESUME",
                                    "VMWRITE",
                                    "VMXOFF",
                                    "VMXON",
                                    "Control-register accesses",
                                    "MOV DR",
                                    "I/O instruction",
                                    "RDMSR",
                                    "WRMSR",
                                    "VM-entry failure due to invalid guest state",
                                    "VM-entry failure due to MSR loading",
                                    "",
                                    "MWAIT",
                                    "Monitor trap flag",
                                    "",
                                    "MONITOR",
                                    "PAUSE",
                                    "VM-entry failure due to machine-check event",
                                    "",
                                    "TPR below threshold",
                                    "APIC access",
                                    "Virtualized EOI",
                                    "Access to GDTR or IDTR",
                                    "Access to LDTR or TR",
                                    "EPT violation",
                                    "EPT misconfiguration",
                                    "INVEPT",
                                    "RDTSCP",
                                    "VMX-preemption timer expired",
                                    "INVVPID",
                                    "WBINVD or WBNOINVD",
                                    "XSETBV",
                                    "APIC Write" };

_Static_assert(sizeof(exit_reason_strs) / sizeof(char *) == NUM_EXIT_REASONS,
               "Exit reason strings table is not correct length");

#ifdef VM_EXIT_TRACKING
uint64_t vm_exit_count[NUM_EXIT_REASONS] = { 0 };
uint64_t vm_exit_total = 0;
#endif

char *fault_to_string(int exit_reason)
{
    if (exit_reason >= NUM_EXIT_REASONS) {
        LOG_VMM_ERR("exit_reason %d is out of bound\n", exit_reason);
        return NULL;
    }
    assert(exit_reason_strs[exit_reason] != NULL);
    return exit_reason_strs[exit_reason];
}

static bool fault_is_trap_like(int exit_reason)
{
    switch (exit_reason) {
    case APIC_WRITE:
    case VIRTUALIZED_EOI:
    case VMX_PREEMPTION_TIMER:
    case INTERRUPT_WINDOW:
        return true;
    default:
        return false;
    }
}

bool ept_fault_is_read(seL4_Word qualification)
{
    return !!(qualification & EPT_VIOLATION_READ_BIT);
}

bool ept_fault_is_write(seL4_Word qualification)
{
    return !!(qualification & EPT_VIOLATION_WRITE_BIT);
}

#elif defined(CONFIG_SVM)

/* seL4 SVM delivers instruction_len MR as 0; use fixed lengths for common exits. */
#define SVM_INSN_LEN_CPUID 2
#define SVM_INSN_LEN_HLT   1
#define SVM_INSN_LEN_MSR   2
#define SVM_INSN_LEN_RDTSC 2
#define SVM_INSN_LEN_XSETBV 3

char *fault_to_string(int exit_reason)
{
    switch ((uint64_t)exit_reason) {
    case VMCB_EXIT_RDTSC:
        return "RDTSC";
    case VMCB_EXIT_CPUID:
        return "CPUID";
    case VMCB_EXIT_HLT:
        return "HLT";
    case VMCB_EXIT_IO:
        return "I/O instruction";
    case VMCB_EXIT_MSR:
        return "MSR";
    case VMCB_EXIT_SHUTDOWN:
        return "SHUTDOWN";
    case VMCB_EXIT_VMRUN:
        return "VMRUN";
    case VMCB_EXIT_XSETBV:
        return "XSETBV";
    case VMCB_EXIT_NPF:
        return "Nested page fault";
    default:
        return "Unknown SVM exit";
    }
}

static bool fault_is_trap_like(int exit_reason)
{
    switch ((uint64_t)exit_reason) {
    case VMCB_EXIT_HLT:
        /* Prefer rip+0 so empty/halt rung can observe halt without spinning. */
        return true;
    default:
        return false;
    }
}

bool ept_fault_is_write(seL4_Word qualification)
{
    return !!(qualification & VMCB_NPF_INFO1_W);
}

bool ept_fault_is_read(seL4_Word qualification)
{
    /* Mirror VT-x helpers: data read ≈ not write (ignore ID/fetch bit for callers). */
    return !ept_fault_is_write(qualification);
}

/* Translate SVM IO EXITINFO1 into the VT-x IO qualification layout used by ioports.c. */
static uint64_t svm_io_qual_to_vtx(uint64_t info1)
{
    uint64_t size = (info1 >> 4) & 0x7;
    uint64_t vtx_size = (size == 1) ? 0 : (size == 2) ? 1 : 3;
    uint64_t q = vtx_size;
    if (info1 & BIT(0)) {
        q |= BIT(3); /* IN */
    }
    if (info1 & BIT(2)) {
        q |= BIT(4); /* STR */
    }
    if (info1 & BIT(3)) {
        q |= BIT(5); /* REP */
    }
    q |= info1 & 0xffff0000ull; /* PORT */
    return q;
}

static void svm_inject_gp(void)
{
    /* EVENTINJ: vector | type=exception | EC valid | valid; error code in high 32 bits. */
    uint64_t eventinj = GP_VECTOR;
    eventinj |= ((uint64_t)VMCB_EVENTINJ_TYPE_EXCEPTION) << 8;
    eventinj |= VMCB_EVENTINJ_EC_VALID;
    eventinj |= VMCB_EVENTINJ_VALID;
    /* error code 0 in bits 63:32 */
    vcpu_exit_inject_irq(eventinj);
}

#endif /* CONFIG_VTX / CONFIG_SVM */

struct ept_exception_handler {
    uintptr_t base;
    uintptr_t end;
    ept_exception_callback_t callback;
    void *cookie;
};
#define MAX_EPT_EXCEPTION_HANDLERS 16
static struct ept_exception_handler registered_ept_exception_handlers[MAX_EPT_EXCEPTION_HANDLERS];
static size_t ept_exception_handler_index = 0;

bool fault_update_ept_exception_handler(uintptr_t base, uintptr_t new_base)
{
    for (int i = 0; i < ept_exception_handler_index; i++) {
        struct ept_exception_handler *curr = &registered_ept_exception_handlers[i];
        if (curr->base == base) {
            curr->base = new_base;
            return true;
        }
    }

    LOG_VMM("failed to update EPT exception handler, none exists for base 0x%lx\n", base);

    return false;
}

bool fault_register_ept_exception_handler(uintptr_t base, size_t size, ept_exception_callback_t callback, void *cookie)
{
    if (ept_exception_handler_index == MAX_EPT_EXCEPTION_HANDLERS - 1) {
        LOG_VMM_ERR("maximum number of EPT exception handlers registered");
        return false;
    }

    if (size == 0) {
        LOG_VMM_ERR("registered EPT exception handler with size 0\n");
        return false;
    }

    for (int i = 0; i < ept_exception_handler_index; i++) {
        struct ept_exception_handler *curr = &registered_ept_exception_handlers[i];
        if (!(base >= curr->end || base + size <= curr->base)) {
            LOG_VMM_ERR("EPT exception handler [0x%lx..0x%lx), overlaps with another handler [0x%lx..0x%lx)\n", base,
                        base + size, curr->base, curr->end);
            return false;
        }
    }

    registered_ept_exception_handlers[ept_exception_handler_index] = (struct ept_exception_handler) {
        .base = base,
        .end = base + size,
        .callback = callback,
        .cookie = cookie,
    };
    ept_exception_handler_index += 1;

    return true;
}

static bool handle_ept_fault(seL4_VCPUContext *vctx, seL4_Word qualification, decoded_instruction_ret_t decoded_ins)
{
    uint64_t addr = vcpu_exit_get_gpa();

    if (addr >= IOAPIC_GPA && addr < IOAPIC_GPA + IOAPIC_SIZE) {
        return ioapic_fault_handle(vctx, addr - IOAPIC_GPA, qualification, decoded_ins);
    } else if (addr >= HPET_GPA && addr < HPET_GPA + HPET_SIZE) {
        return hpet_fault_handle(vctx, addr - HPET_GPA, qualification, decoded_ins);
#if APIC_VIRT_LEVEL < APIC_VIRT_LEVEL_APICV
    } else if (addr >= LAPIC_GPA && addr < LAPIC_GPA + LAPIC_SIZE) {
        return lapic_fault_handle(vctx, addr - LAPIC_GPA, qualification, decoded_ins);
#endif
    } else {
        for (int i = 0; i < MAX_EPT_EXCEPTION_HANDLERS; i++) {
            uintptr_t base = registered_ept_exception_handlers[i].base;
            uintptr_t end = registered_ept_exception_handlers[i].end;
            ept_exception_callback_t callback = registered_ept_exception_handlers[i].callback;
            void *cookie = registered_ept_exception_handlers[i].cookie;
            if (addr >= base && addr < end) {
                bool success = callback(0, addr - base, qualification, decoded_ins, vctx, cookie);
                if (!success) {
                    LOG_VMM_ERR("registered EPT exception handler for region [0x%lx..0x%lx) at address "
                                "0x%lx failed\n",
                                base, end, addr);
                }

                return success;
            }
        }

        LOG_VMM_ERR("failed to find EPT handler for address 0x%lx\n", addr);
    }

    return false;
}

struct pio_exception_handler {
    uint16_t base;
    uint16_t end;
    pio_exception_callback_t callback;
    void *cookie;
};
#define MAX_PIO_EXCEPTION_HANDLERS 16
static struct pio_exception_handler registered_pio_exception_handlers[MAX_PIO_EXCEPTION_HANDLERS];
static size_t pio_exception_handler_index = 0;

bool fault_register_pio_exception_handler(uint16_t base, uint16_t size, pio_exception_callback_t callback, void *cookie)
{
    if (pio_exception_handler_index == MAX_PIO_EXCEPTION_HANDLERS - 1) {
        LOG_VMM_ERR("maximum number of PIO exception handlers registered");
        return false;
    }

    if (size == 0) {
        LOG_VMM_ERR("registered PIO exception handler with size 0\n");
        return false;
    }

    uint64_t size_64 = (uint64_t)base + (uint64_t)size;
    if (size_64 > 0xffff) {
        LOG_VMM_ERR("base + size = 0x%lx exceed uint16_t max\n", size_64);
        return false;
    }

    for (int i = 0; i < pio_exception_handler_index; i++) {
        struct pio_exception_handler *curr = &registered_pio_exception_handlers[i];
        if (!(base >= curr->end || base + size <= curr->base)) {
            LOG_VMM_ERR("PIO exception handler [0x%hx..0x%hx), overlaps with another handler [0x%hx..0x%hx)\n", base,
                        base + size, curr->base, curr->end);
            return false;
        }
    }

    registered_pio_exception_handlers[pio_exception_handler_index] = (struct pio_exception_handler) {
        .base = base,
        .end = base + size,
        .callback = callback,
        .cookie = cookie,
    };
    pio_exception_handler_index += 1;

    return true;
}

static bool handle_pio_fault(seL4_VCPUContext *vctx, seL4_Word qualification)
{
    uint16_t port_addr = pio_fault_addr(qualification);

    for (int i = 0; i < MAX_PIO_EXCEPTION_HANDLERS; i++) {
        uint16_t base = registered_pio_exception_handlers[i].base;
        uint16_t end = registered_pio_exception_handlers[i].end;
        pio_exception_callback_t callback = registered_pio_exception_handlers[i].callback;
        void *cookie = registered_pio_exception_handlers[i].cookie;
        if (port_addr >= base && port_addr < end) {
            bool success = callback(0, port_addr - base, qualification, vctx, cookie);
            if (!success) {
                LOG_VMM_ERR("registered PIO exception handler for region [0x%hx..0x%hx) at address "
                            "0x%hx failed\n",
                            base, end, port_addr);
            }

            return success;
        }
    }

    emulate_ioport_noop_access(qualification, vctx);
    return true;
}

#if defined(CONFIG_SVM)

bool fault_handle(size_t vcpu_id, microkit_msginfo msginfo)
{
    bool success = false;
    decoded_instruction_ret_t decoded_ins = { 0 };
    unsigned rip_additive = 0;
    bool msr_or_xsetbv_fail = false;
    vcpu_cache_exit_state(msginfo);

    seL4_Word f_reason = vcpu_exit_get_reason();
    seL4_Word qualification = vcpu_exit_get_qualification();
    seL4_Word rip = vcpu_exit_get_rip();
    seL4_VCPUContext *vctx = vcpu_exit_get_context();

    /* TEMP(#33): breadcrumb rare exits only (MSR spam is noisy). */
    {
        static uint64_t exit_n;
        exit_n++;
        if (f_reason == VMCB_EXIT_NPF || f_reason == VMCB_EXIT_SHUTDOWN
            || f_reason == VMCB_EXIT_HLT || f_reason >= VMCB_EXIT_EXCP_BASE
            || (f_reason == VMCB_EXIT_MSR && (vctx->ecx == 0x1b || vctx->ecx < 0x100))) {
            LOG_VMM("exit#%lu reason=0x%lx rip=0x%lx qual=0x%lx gpa=0x%lx ecx=0x%lx\n",
                    (unsigned long)exit_n, (unsigned long)f_reason, (unsigned long)rip,
                    (unsigned long)qualification, (unsigned long)vcpu_exit_get_gpa(),
                    (unsigned long)vctx->ecx);
        }
    }

    switch (f_reason) {
    case VMCB_EXIT_CPUID:
        success = emulate_cpuid(vctx);
        rip_additive = SVM_INSN_LEN_CPUID;
        break;
    case VMCB_EXIT_MSR:
        /* EXITINFO1 bit0: 1 = WRMSR, 0 = RDMSR. */
        if (qualification & BIT(0)) {
            success = emulate_wrmsr(vctx);
        } else {
            success = emulate_rdmsr(vctx);
        }
        rip_additive = SVM_INSN_LEN_MSR;
        msr_or_xsetbv_fail = !success;
        break;
    case VMCB_EXIT_NPF:
        decoded_ins = decode_instruction(vcpu_id, rip);
        if (decoded_ins.type == INSTRUCTION_DECODE_FAIL) {
            success = false;
        } else {
            success = handle_ept_fault(vctx, qualification, decoded_ins);
            rip_additive = decoded_ins.len;
        }
        break;
    case VMCB_EXIT_IO: {
        uint64_t vtx_qual = svm_io_qual_to_vtx(qualification);
        success = handle_pio_fault(vctx, vtx_qual);
        /* seL4 maps EXITINFO2 into the GPA MR; for IO that is next RIP. */
        if (vcpu_exit_get_gpa() > rip) {
            rip_additive = (unsigned)(vcpu_exit_get_gpa() - rip);
        } else {
            rip_additive = 1;
        }
        break;
    }
    case VMCB_EXIT_XSETBV:
        success = emulate_xsetbv(vctx);
        rip_additive = SVM_INSN_LEN_XSETBV;
        msr_or_xsetbv_fail = !success;
        break;
    case VMCB_EXIT_RDTSC: {
        /* Pass through host TSC (guest wall-clock == host TSC for bring-up). */
        uint64_t tsc = guest_time_tsc_now();
        vctx->eax = (uint32_t)tsc;
        vctx->edx = (uint32_t)(tsc >> 32);
        success = true;
        rip_additive = SVM_INSN_LEN_RDTSC;
        break;
    }
    case VMCB_EXIT_HLT:
        /* Trap-like: leave RIP on HLT so empty/halt can observe halt. */
        success = true;
        rip_additive = 0;
        break;
    case VMCB_EXIT_VMRUN:
        LOG_VMM_ERR("guest attempted VMRUN (Nested SVM is out of scope)\n");
        success = false;
        break;
    case VMCB_EXIT_SHUTDOWN:
        LOG_VMM_ERR("guest SHUTDOWN intercept\n");
        success = false;
        break;
    default:
        if (f_reason >= VMCB_EXIT_EXCP_BASE && f_reason < VMCB_EXIT_EXCP_BASE + 32) {
            uint64_t vec = f_reason - VMCB_EXIT_EXCP_BASE;
            static uint64_t excp_logs;
            if (excp_logs < 32) {
                excp_logs++;
                LOG_VMM_ERR("guest exception vector=%lu rip=0x%lx info1=0x%lx cr2=0x%lx\n",
                            (unsigned long)vec, (unsigned long)rip,
                            (unsigned long)qualification,
                            (unsigned long)libvmm_vcpu_read(0, VMCB_STATE_GUEST_CR2));
            }
            /* Reinject so we can observe cascades instead of freezing the VCPU. */
            uint64_t eventinj = vec;
            eventinj |= ((uint64_t)VMCB_EVENTINJ_TYPE_EXCEPTION) << 8;
            eventinj |= VMCB_EVENTINJ_VALID;
            /* #DF/#TS/#NP/#SS/#GP/#PF/#AC have error codes. */
            if (vec == 8 || vec == 10 || vec == 11 || vec == 12 || vec == 13 || vec == 14 || vec == 17) {
                eventinj |= VMCB_EVENTINJ_EC_VALID;
                eventinj |= (qualification & 0xffffffffull) << 32;
            }
            vcpu_exit_inject_irq(eventinj);
            success = true;
            rip_additive = 0;
        } else {
            LOG_VMM_ERR("unhandled SVM fault: 0x%lx rip=0x%lx\n", f_reason, (unsigned long)rip);
        }
    };

    /*
     * Deviation from VT-x: no VMX-preemption timer. guest_time_maintenance still
     * services due timeouts via TSC comparison when the guest already exits.
     */
    guest_time_maintenance();

    if (success) {
        if (fault_is_trap_like(f_reason)) {
            rip_additive = 0;
        }
        vcpu_exit_prepare_resume(rip_additive);
    } else if (msr_or_xsetbv_fail) {
#ifdef LOG_GP
        LOG_VMM("injecting GP due to failure in handling %s\n", fault_to_string(f_reason));
#endif
        svm_inject_gp();
        vcpu_exit_prepare_resume(0);
        success = true;
    } else {
        LOG_VMM_ERR("failed handling fault: '%s' (0x%lx)\n", fault_to_string(f_reason), f_reason);
        vcpu_print_regs(vcpu_id);
        vcpu_print_instruction(vcpu_id);
        LOG_VMM_ERR("VCPU will not be resumed.\n");
    }

    return success;
}

#elif defined(CONFIG_VTX)

bool fault_handle(size_t vcpu_id, microkit_msginfo msginfo)
{
    bool success = false;
    decoded_instruction_ret_t decoded_ins;

    vcpu_cache_exit_state(msginfo);

    seL4_Word f_reason = vcpu_exit_get_reason();
    seL4_Word qualification = vcpu_exit_get_qualification();
    seL4_Word rip = vcpu_exit_get_rip();
    seL4_VCPUContext *vctx = vcpu_exit_get_context();

    switch (f_reason) {
    case CPUID:
        success = emulate_cpuid(vctx);
        break;
    case RDMSR:
        success = emulate_rdmsr(vctx);
        break;
    case WRMSR:
        success = emulate_wrmsr(vctx);
        break;
    case EPT_VIOLATION:
        decoded_ins = decode_instruction(vcpu_id, rip);
        if (decoded_ins.type == INSTRUCTION_DECODE_FAIL) {
            success = false;
        } else {
            success = handle_ept_fault(vctx, qualification, decoded_ins);
        }
        break;
    case IO:
        success = handle_pio_fault(vctx, qualification);
        break;
    case XSETBV:
        success = emulate_xsetbv(vctx);
        break;
    case VMX_PREEMPTION_TIMER:
        guest_time_handle_timer_ntfn();
        success = true;
        break;
    case CONTROL_REGISTER:
        success = handle_cr_access(vctx, qualification);
        break;
#if APIC_VIRT_LEVEL == APIC_VIRT_LEVEL_APICV
    case VIRTUALIZED_EOI: {
        uint8_t eoi_vector = qualification;
        success = ioapic_ack_passthrough_irq(eoi_vector);
        break;
    }
    case APIC_WRITE: {
        uint64_t lapic_reg_offset = qualification;
        success = lapic_write_fault_handle(lapic_reg_offset, lapic_read_reg(lapic_reg_offset));
        break;
    }
    case APIC_ACCESS: {
        uint16_t offset = qualification & 0x7ff;
        uint8_t access_type = (qualification >> 12) & 0xf;

        if (access_type == 0) {
            uint32_t data;
            success = lapic_read_fault_handle(offset, &data);
            decoded_ins = decode_instruction(vcpu_id, rip);
            if (decoded_ins.type != INSTRUCTION_MEMORY) {
                LOG_VMM_ERR("expected memory instruction type, got type %d\n", decoded_ins.type);
                success = false;
                break;
            }
            uint64_t *vctx_raw = (uint64_t *)vctx;
            vctx_raw[decoded_ins.decoded.memory_instruction.target_reg] = data;
        } else if (access_type == 1) {
            decoded_ins = decode_instruction(vcpu_id, rip);
            if (decoded_ins.type != INSTRUCTION_MEMORY) {
                LOG_VMM_ERR("expected memory instruction type, got type %d\n", decoded_ins.type);
                success = false;
                break;
            }

            int access_width = mem_access_width_to_bytes(decoded_ins);
            if (access_width != 4) {
                LOG_VMM_ERR("expected access width 4, got %d\n", access_width);
                success = false;
                break;
            }
            uint64_t *vctx_raw = (uint64_t *)vctx;
            success = lapic_write_fault_handle(offset, vctx_raw[decoded_ins.decoded.memory_instruction.target_reg]);
        } else {
            LOG_VMM_ERR("unsupported access type %d for apic access vm exit\n", access_type);
            success = false;
        }
        break;
    }
#else
    case INTERRUPT_WINDOW:
        lapic_maintenance();
        success = true;
        break;
#endif
    default:
        LOG_VMM_ERR("unhandled fault: 0x%lx\n", f_reason);
    };

#ifdef VM_EXIT_TRACKING
    vm_exit_count[f_reason] += 1;
    vm_exit_total += 1;
    if (vm_exit_total % VM_EXIT_PRINT_MULTIPLE == 0) {
        LOG_VMM("VM Exit total %lu, count by type:\n", vm_exit_total);
        for (int i = 0; i < NUM_EXIT_REASONS; i++) {
            if (vm_exit_count[i]) {
                LOG_VMM("%s: %lu\n", fault_to_string(i), vm_exit_count[i]);
            }
        }
    }
#endif

    /* The VMX preemption timer doesn't count down while the VMM is running.
     * So we must correct for the time that the VMM stole away from the VM. */
    guest_time_maintenance();

    if (success) {
        unsigned rip_additive = 0;
        if (!fault_is_trap_like(f_reason)) {
            if (f_reason == EPT_VIOLATION) {
                rip_additive = decoded_ins.len;
            } else {
                rip_additive = vcpu_exit_get_instruction_len();
            }
        }
        vcpu_exit_prepare_resume(rip_additive);
    } else if (!success && (f_reason == RDMSR || f_reason == WRMSR || f_reason == XSETBV)) {
#ifdef LOG_GP
        LOG_VMM("injecting GP due to failure in handling %s\n", fault_to_string(f_reason));
#endif

        microkit_vcpu_x86_write_vmcs(GUEST_BOOT_VCPU_ID, VMX_CONTROL_ENTRY_EXCEPTION_ERROR_CODE, 0);

        uint64_t interruption = GP_VECTOR;
        interruption |= 3ull << 8;
        interruption |= BIT(11);
        interruption |= BIT(31);

        vcpu_exit_inject_irq(interruption);
        vcpu_exit_prepare_resume(0);
        success = true;
    } else if (!success) {
        LOG_VMM_ERR("failed handling fault: '%s' (0x%lx)\n", fault_to_string(f_reason), f_reason);
        vcpu_print_regs(vcpu_id);
        vcpu_print_instruction(vcpu_id);
        LOG_VMM_ERR("VCPU will not be resumed.\n");
    }

    return success;
}

#endif /* CONFIG_SVM / CONFIG_VTX */
