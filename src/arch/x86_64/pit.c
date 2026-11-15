/*
 * Minimal i8254 PIT PIO emulator for x86 guests.
 *
 * Nested passthrough of host PIT is unsafe (guest resets). Linux early boot
 * uses the PIT for KASLR entropy, TSC calibration, and (via IO-APIC pin 2 /
 * MADT ISO IRQ0→GSI2) the timer IRQ check in setup_IO_APIC.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stdint.h>
#include <stdbool.h>
#include <libvmm/arch/x86_64/pit.h>
#include <libvmm/arch/x86_64/fault.h>
#include <libvmm/arch/x86_64/ioports.h>
#include <libvmm/arch/x86_64/guest_time.h>
#include <libvmm/arch/x86_64/apic.h>
#include <libvmm/virq.h>
#include <libvmm/util/util.h>

#define PIT_PORT_BASE 0x40
#define PIT_PORT_SIZE 4
#define PIT_PORT_COUNTER0 0
#define PIT_PORT_MODE 3

/* ISA PIT oscillator. */
#define PIT_HZ 1193182ull

/* MADT ISO: ISA IRQ0 (PIT) → GSI 2 on our bring-up tables. */
#define PIT_IOAPIC_PIN 2

static uint16_t pit_latched;
static uint8_t pit_latch_hi; /* 0 = next read is low byte, 1 = high */
static bool pit_latched_valid;

/* Counter 0 programming state for IRQ generation. */
static uint8_t pit_c0_access; /* 1=lo, 2=hi, 3=lo/hi */
static uint8_t pit_c0_mode;
static uint16_t pit_c0_divisor;
static uint8_t pit_c0_write_phase; /* 0=expect lo, 1=expect hi when lo/hi */
static bool pit_c0_periodic;
static guest_timeout_handle_t pit_timeout_handle = TIMEOUT_HANDLE_INVALID;

static uint16_t pit_counter_now(void)
{
    uint64_t hz = guest_time_tsc_hz();
    if (hz == 0) {
        hz = 2000000000ull;
    }
    uint64_t tsc = guest_time_tsc_now();
    /* Free-running 16-bit counter at PIT_HZ. Split the multiply so absolute
     * host TSC (rdtsc) * PIT_HZ cannot overflow uint64 — that tripped
     * -fsanitize-trap=undefined and killed the VMM with no serial (silent TF
     * right after NR_IRQS when Linux first programs the PIT timer). */
    uint64_t pit_ticks = (tsc / hz) * PIT_HZ + ((tsc % hz) * PIT_HZ) / hz;
    return (uint16_t)pit_ticks;
}

static void pit_irq_fire(size_t cookie)
{
    (void)cookie;
    pit_timeout_handle = TIMEOUT_HANDLE_INVALID;
    /* Edge-triggered ISA timer → IOAPIC pin from MADT ISO. */
    if (!virq_inject(X86_IOAPIC_IRQ_ROUTE(0, PIT_IOAPIC_PIN))) {
        LOG_VMM_ERR("PIT IRQ inject failed (pin %u)\n", PIT_IOAPIC_PIN);
    }
    if (pit_c0_periodic) {
        uint64_t hz = guest_time_tsc_hz();
        if (hz == 0) {
            hz = 2000000000ull;
        }
        uint32_t div = pit_c0_divisor ? pit_c0_divisor : 65536u;
        uint64_t tsc_delta = (hz / PIT_HZ) * div + ((hz % PIT_HZ) * div) / PIT_HZ;
        if (tsc_delta == 0) {
            tsc_delta = 1;
        }
        pit_timeout_handle = guest_time_request_timeout(tsc_delta, &pit_irq_fire, 0);
    }
}

static void pit_arm_irq(void)
{
    if (pit_timeout_handle != TIMEOUT_HANDLE_INVALID) {
        guest_time_cancel_timeout(pit_timeout_handle);
        pit_timeout_handle = TIMEOUT_HANDLE_INVALID;
    }
    /* Mode 0/1: one-shot; mode 2/3 (and 6/7 aliases): periodic. */
    uint8_t mode = pit_c0_mode;
    if (mode == 6) {
        mode = 2;
    } else if (mode == 7) {
        mode = 3;
    }
    if (mode > 3) {
        pit_c0_periodic = false;
        return;
    }
    pit_c0_periodic = (mode == 2 || mode == 3);
    uint64_t hz = guest_time_tsc_hz();
    if (hz == 0) {
        hz = 2000000000ull;
    }
    uint32_t div = pit_c0_divisor ? pit_c0_divisor : 65536u;
    uint64_t tsc_delta = (hz / PIT_HZ) * div + ((hz % PIT_HZ) * div) / PIT_HZ;
    if (tsc_delta == 0) {
        tsc_delta = 1;
    }
    pit_timeout_handle = guest_time_request_timeout(tsc_delta, &pit_irq_fire, 0);
    if (pit_timeout_handle == TIMEOUT_HANDLE_INVALID) {
        LOG_VMM_ERR("PIT timeout arm failed (no slots)\n");
    }
}

static bool pit_fault_handle(size_t vcpu_id, uint16_t port_offset, size_t qualification,
                             seL4_VCPUContext *vctx, void *cookie)
{
    (void)vcpu_id;
    (void)cookie;

    if (pio_fault_is_write(qualification)) {
        uint32_t val = 0;
        if (!pio_get_write_data(qualification, vctx, &val)) {
            return false;
        }
        if (port_offset == PIT_PORT_MODE) {
            uint8_t channel = (val >> 6) & 0x3;
            uint8_t access = (val >> 4) & 0x3;
            uint8_t mode = (val >> 1) & 0x7;
            if (channel == 0) {
                if (access == 0) {
                    /* Latch counter 0. */
                    pit_latched = pit_counter_now();
                    pit_latched_valid = true;
                    pit_latch_hi = 0;
                } else {
                    pit_c0_access = access;
                    pit_c0_mode = mode;
                    pit_c0_write_phase = 0;
                    pit_c0_divisor = 0;
                }
            }
        } else if (port_offset == PIT_PORT_COUNTER0) {
            if (pit_c0_access == 1) {
                /* lobyte only */
                pit_c0_divisor = val & 0xff;
                pit_arm_irq();
            } else if (pit_c0_access == 2) {
                /* hibyte only */
                pit_c0_divisor = (val & 0xff) << 8;
                pit_arm_irq();
            } else if (pit_c0_access == 3) {
                if (pit_c0_write_phase == 0) {
                    pit_c0_divisor = val & 0xff;
                    pit_c0_write_phase = 1;
                } else {
                    pit_c0_divisor |= (val & 0xff) << 8;
                    pit_c0_write_phase = 0;
                    pit_arm_irq();
                }
            }
        }
        return true;
    }

    /* Reads: only counter 0 is used for entropy/calibration; other ports return 0. */
    uint32_t data = 0;
    if (port_offset == PIT_PORT_COUNTER0) {
        uint16_t count = pit_latched_valid ? pit_latched : pit_counter_now();
        if (pit_latch_hi == 0) {
            data = count & 0xffu;
            pit_latch_hi = 1;
        } else {
            data = (count >> 8) & 0xffu;
            pit_latch_hi = 0;
            pit_latched_valid = false;
        }
    }

    return pio_emulate_read(qualification, vctx, data);
}

bool initialise_pit(void)
{
    pit_latched = 0;
    pit_latch_hi = 0;
    pit_latched_valid = false;
    pit_c0_access = 3;
    pit_c0_mode = 0;
    pit_c0_divisor = 0;
    pit_c0_write_phase = 0;
    pit_c0_periodic = false;
    pit_timeout_handle = TIMEOUT_HANDLE_INVALID;
    /* Pin must be registered before virq_inject; shared with HPET legacy route. */
    if (!virq_register(X86_IOAPIC_IRQ_ROUTE(0, PIT_IOAPIC_PIN), NULL, NULL)) {
        LOG_VMM_ERR("Failed to register PIT IOAPIC pin %u\n", PIT_IOAPIC_PIN);
        return false;
    }
    if (!fault_register_pio_exception_handler(PIT_PORT_BASE, PIT_PORT_SIZE, pit_fault_handle, NULL)) {
        LOG_VMM_ERR("Failed to register PIO handler for PIT\n");
        return false;
    }
    /*
     * With tsc_early_khz / nokaslr, Linux may never program the PIT yet still
     * expects IRQ0 (via IOAPIC pin 2) during check_timer. Start a ~100 Hz soft
     * tick; injects no-op while the pin is masked.
     */
    pit_c0_mode = 2;
    pit_c0_divisor = (uint16_t)(PIT_HZ / 100);
    if (pit_c0_divisor == 0) {
        pit_c0_divisor = 1;
    }
    pit_arm_irq();
    return true;
}
