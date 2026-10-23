/* SPDX-License-Identifier: GPL-2.0-or-later
 * Adapted from resetAdapter, lockNVRAM and indirect memory access in
 * BCM5722D/Private.cpp, Copyright (C) 2010 Adlan Razalan.
 * Reworked as a BCM5722-only C routine with bounded waits and error returns.
 * This is not called by the diagnostic VxD until platform MMIO is implemented.
 */
#include "bcm5722_hw.h"

#define HOST_CONTROL 0x68
#define HOST_CONTROL_VALUE 0x29aUL
#define INTERRUPT_MASK 2UL
#define MEMORY_WINDOW 0x7c
#define MEMORY_DATA 0x84
#define INTERRUPT_MAILBOX 0x204
#define MAGIC_MAILBOX 0xb50
#define MAGIC 0x4b657654UL
#define ARBITER_MODE 0x4000
#define GRC_MODE 0x6800
#define GRC_CONFIG 0x6804
#define FASTBOOT_PC 0x6894
#define NVRAM_ARBITRATION 0x7020
#define RESET_DISABLE 0x20000000UL
#define CORE_RESET 1UL
#define PHY_POWER_DOWN 0x04000000UL
#define TIMER_66MHZ 0x82UL
#define PCI_COMMAND 4
#define PCIE_CONTROL 0xd8
#define PCIE_STATUS 0xda
#define PCIE_TLP 0x7c00
#define PCIE_TEST 0x7e2c

static void mask_interrupts(const struct bcm_hw_io *io)
{
    io->write32(io->context, HOST_CONTROL,
        io->read32(io->context, HOST_CONTROL) | INTERRUPT_MASK);
    io->write32(io->context, INTERRUPT_MAILBOX, 1);
    (void)io->read32(io->context, HOST_CONTROL); /* Drain posted MMIO writes. */
}

static void memory_write(const struct bcm_hw_io *io, unsigned address,
                          uint32_t value)
{
    io->write32(io->context, MEMORY_WINDOW, address);
    io->write32(io->context, MEMORY_DATA, value);
    io->write32(io->context, MEMORY_WINDOW, 0);
}

static uint32_t memory_read(const struct bcm_hw_io *io, unsigned address)
{
    uint32_t value;
    io->write32(io->context, MEMORY_WINDOW, address);
    value = io->read32(io->context, MEMORY_DATA);
    io->write32(io->context, MEMORY_WINDOW, 0);
    return value;
}

enum bcm_hw_result bcm5722_reset(const struct bcm_hw_io *io)
{
    unsigned i;
    uint16_t command, control;
    if (io->pci_read16(io->context, 2) != 0x165a ||
            io->pci_read16(io->context, 0) != 0x14e4)
        return BCM_HW_WRONG_DEVICE;
    command = io->pci_read16(io->context, PCI_COMMAND);
    if ((command & 6) != 6)
        return BCM_HW_PCI_DISABLED;
    mask_interrupts(io);
    io->write32(io->context, NVRAM_ARBITRATION, 2); /* REQ_SET1 */
    for (i = 0; i < 1000; ++i) {
        if (io->read32(io->context, NVRAM_ARBITRATION) & 0x200)
            break;
        io->delay_us(io->context, 10);
    }
    if (i == 1000) {
        io->write32(io->context, NVRAM_ARBITRATION, 0x20); /* REQ_CLR1 */
        return BCM_HW_NVRAM_TIMEOUT;
    }
    io->write32(io->context, ARBITER_MODE, 2);
    io->pci_write32(io->context, HOST_CONTROL, HOST_CONTROL_VALUE);
    io->write32(io->context, FASTBOOT_PC, 0);
    memory_write(io, MAGIC_MAILBOX, MAGIC);
    if (io->read32(io->context, PCIE_TEST) == 0x60)
        io->write32(io->context, PCIE_TEST, 0x20);
    /* Preserve the upstream pre-reset workaround before asserting reset. */
    io->write32(io->context, GRC_CONFIG, RESET_DISABLE);
    io->write32(io->context, GRC_CONFIG,
        RESET_DISABLE | CORE_RESET | PHY_POWER_DOWN | TIMER_66MHZ);
    (void)io->read32(io->context, GRC_CONFIG);
    io->delay_us(io->context, 100000);
    mask_interrupts(io);
    io->pci_write16(io->context, PCI_COMMAND, command);
    control = io->pci_read16(io->context, PCIE_CONTROL);
    io->pci_write16(io->context, PCIE_CONTROL,
        (uint16_t)(control & ~0x08f0)); /* No snoop/relaxed, payload 128. */
    io->pci_write16(io->context, PCIE_STATUS, 0x0f);
    io->write32(io->context, ARBITER_MODE, 2);
    io->pci_write32(io->context, HOST_CONTROL, HOST_CONTROL_VALUE);
    io->write32(io->context, 0x70, 0x60); /* EEPROM / retry state. */
    io->write32(io->context, GRC_MODE, 0x34); /* Descriptor/frame swaps. */
    io->write32(io->context, 0x400, 0); /* MAC engines remain disabled. */
    io->delay_us(io->context, 40);
    for (i = 0; i < 100000; ++i) {
        if (memory_read(io, MAGIC_MAILBOX) == (uint32_t)~MAGIC)
            break;
        io->delay_us(io->context, 10);
    }
    if (i == 100000) {
        io->write32(io->context, NVRAM_ARBITRATION, 0x20);
        return BCM_HW_FIRMWARE_TIMEOUT;
    }
    io->write32(io->context, PCIE_TLP, 0x22000000UL);
    return BCM_HW_OK;
}
