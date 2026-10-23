/* SPDX-License-Identifier: GPL-2.0-or-later
 * Adapted from resetAdapter, initializeAdapter, stopAdapter,
 * configureMACAddress, lockNVRAM and indirect memory access in
 * BCM5722D/Private.cpp, Copyright (C) 2010 Adlan Razalan.
 * Reworked as BCM5722-only C routines with bounded waits and error returns.
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

#define DMA_RW_CONTROL 0x6c
#define MAC_MODE 0x400
#define MAC_MODE_CLEAR_STATISTICS 0x9000UL
#define MAC_EVENT 0x408
#define MAC_LED 0x40c
#define MAC_ADDRESS_HIGH 0x410
#define MAC_ADDRESS_LOW 0x414
#define MAC_BACKOFF_SEED 0x438
#define MAC_RX_MTU 0x43c
#define MI_STATUS 0x450
#define MI_MODE 0x454
#define MAC_TX_MODE 0x45c
#define MAC_TX_LENGTHS 0x464
#define MAC_RX_MODE 0x468
#define MAC_RX_RULES 0x500
#define MAC_RX_LOW_WATER 0x504
#define TX_DATA_INITIATOR 0xc00
#define TX_STATS_CONTROL 0xc08
#define TX_STATS_ENABLE 0xc0c
#define TX_DATA_COMPLETION 0x1000
#define TX_BD_SELECTOR 0x1400
#define TX_BD_INITIATOR 0x1800
#define TX_BD_COMPLETION 0x1c00
#define RX_PLACEMENT 0x2000
#define RX_PLACEMENT_CONFIG 0x2010
#define RX_STATS_CONTROL 0x2014
#define RX_STATS_ENABLE 0x2018
#define RX_BD_INITIATOR_DATA 0x2400
#define RX_STD_RCB 0x2450
#define RX_DATA_COMPLETION 0x2800
#define RX_BD_INITIATOR 0x2c00
#define RX_STD_THRESHOLD 0x2c18
#define RX_BD_COMPLETION 0x3000
#define COALESCING 0x3c00
#define COALESCING_ENABLE 2UL
#define STATUS_BLOCK_HIGH 0x3c38
#define BUFFER_MANAGER 0x4400
#define READ_DMA 0x4800
#define WRITE_DMA 0x4c00
#define FTQ_RESET 0x5c00
#define LOCAL_CONTROL 0x6808
#define TX_RCB 0x8100              /* NIC memory window, send ring 1 */
#define RETURN_RCB 0x8200          /* NIC memory window, return ring 1 */
#define ENGINE_ENABLE 2UL
#define ENGINE_ENABLE_ATTENTION 6UL

static void wait_set(const struct bcm_hw_io *io, unsigned offset,
                     uint32_t bit, int set, unsigned tries)
{
    while (tries--) {
        if (!(io->read32(io->context, offset) & bit) == !set)
            return;
        io->delay_us(io->context, 10);
    }
}

void bcm5722_read_mac(const struct bcm_hw_io *io, uint8_t mac[6])
{
    uint32_t high = io->read32(io->context, MAC_ADDRESS_HIGH);
    uint32_t low = io->read32(io->context, MAC_ADDRESS_LOW);
    mac[0] = (uint8_t)(high >> 8);
    mac[1] = (uint8_t)high;
    mac[2] = (uint8_t)(low >> 24);
    mac[3] = (uint8_t)(low >> 16);
    mac[4] = (uint8_t)(low >> 8);
    mac[5] = (uint8_t)low;
}

void bcm5722_enable_interrupts(const struct bcm_hw_io *io, int enable)
{
    uint32_t control = io->read32(io->context, HOST_CONTROL);
    if (enable)
        control &= ~INTERRUPT_MASK;
    else
        control |= INTERRUPT_MASK;
    io->write32(io->context, HOST_CONTROL, control);
    (void)io->read32(io->context, HOST_CONTROL);
}

#define MAC_HASH 0x470
#define RX_MODE_PROMISCUOUS 0x100UL

void bcm5722_rx_filter(const struct bcm_hw_io *io, uint32_t *rx_mode,
                       int promiscuous)
{
    unsigned i;
    for (i = 0; i < 4; ++i)
        io->write32(io->context, MAC_HASH + 4 * i, 0xffffffffUL);
    if (promiscuous)
        *rx_mode |= RX_MODE_PROMISCUOUS;
    else
        *rx_mode &= ~RX_MODE_PROMISCUOUS;
    io->write32(io->context, MAC_RX_MODE, *rx_mode);
}

static void write_mac(const struct bcm_hw_io *io, const uint8_t mac[6])
{
    io->write32(io->context, MAC_ADDRESS_HIGH, (uint32_t)mac[0] << 8 | mac[1]);
    io->write32(io->context, MAC_ADDRESS_LOW, (uint32_t)mac[2] << 24 |
        (uint32_t)mac[3] << 16 | (uint32_t)mac[4] << 8 | mac[5]);
    /* BCM5722D summed mac[4] twice and left out mac[5]. */
    io->write32(io->context, MAC_BACKOFF_SEED, (uint32_t)(mac[0] + mac[1] +
        mac[2] + mac[3] + mac[4] + mac[5]) & 0x3ff);
}

static void ring_control(const struct bcm_hw_io *io, unsigned rcb,
                         uint32_t host, uint32_t length_flags, uint32_t nic)
{
    io->write32(io->context, rcb, 0);
    io->write32(io->context, rcb + 4, host);
    io->write32(io->context, rcb + 8, length_flags);
    io->write32(io->context, rcb + 12, nic);
}

enum bcm_hw_result bcm5722_start(const struct bcm_hw_io *io,
    const struct bcm_ring_addresses *rings, const uint8_t mac[6],
    uint32_t mac_mode, uint32_t rx_mode, uint32_t tx_mode)
{
    unsigned i;

    io->write32(io->context, MEMORY_WINDOW, 0);
    io->write32(io->context, DMA_RW_CONTROL, 0x76180000UL);
    /* Swap non-frame and frame data, host send descriptors and stack,
     * no pseudo-header checksum, interrupt on MAC attention. */
    io->write32(io->context, GRC_MODE, 0x04130034UL);
    io->write32(io->context, GRC_CONFIG,
        io->read32(io->context, GRC_CONFIG) | TIMER_66MHZ);

    io->write32(io->context, BUFFER_MANAGER + 0x10, 0);    /* read DMA low */
    io->write32(io->context, BUFFER_MANAGER + 0x14, 0x10); /* RX low water */
    io->write32(io->context, BUFFER_MANAGER + 0x18, 0x60); /* high water */
    io->write32(io->context, MAC_RX_LOW_WATER, 2);
    io->write32(io->context, BUFFER_MANAGER + 0x34, 5);
    io->write32(io->context, BUFFER_MANAGER + 0x38, 10);
    io->write32(io->context, BUFFER_MANAGER, ENGINE_ENABLE_ATTENTION);
    for (i = 0; i < 1000; ++i) {
        if (io->read32(io->context, BUFFER_MANAGER) & ENGINE_ENABLE)
            break;
        io->delay_us(io->context, 10);
    }
    if (i == 1000)
        return BCM_HW_BUFFER_MANAGER_TIMEOUT;
    io->write32(io->context, FTQ_RESET, 0xffffffffUL);
    io->write32(io->context, FTQ_RESET, 0);

    ring_control(io, RX_STD_RCB, rings->std, 0x200UL << 16, 0x6000);
    io->write32(io->context, RX_STD_THRESHOLD, 25);
    io->write32(io->context, 0x304, 0);                    /* send producer */
    ring_control(io, TX_RCB, rings->tx, 512UL << 16, 0x4000);
    ring_control(io, RETURN_RCB, rings->ret, 0x200UL << 16, 0);
    io->write32(io->context, 0x284, 0);                    /* return consumer */
    io->write32(io->context, 0x26c, rings->std_posted & 511);

    write_mac(io, mac);
    io->write32(io->context, MAC_RX_MTU, 1518 + 4);
    io->write32(io->context, MAC_TX_LENGTHS, 0x2620);
    io->write32(io->context, MAC_RX_RULES, 8);
    io->write32(io->context, RX_PLACEMENT_CONFIG, 0x181);
    io->write32(io->context, RX_STATS_ENABLE,
        io->read32(io->context, RX_STATS_ENABLE) & ~0x40000UL);
    io->write32(io->context, RX_STATS_CONTROL, 1);
    io->write32(io->context, TX_STATS_ENABLE, 0xffffff);
    io->write32(io->context, TX_STATS_CONTROL, 3);

    io->write32(io->context, COALESCING, 0);
    wait_set(io, COALESCING, COALESCING_ENABLE, 0, 2000);
    io->write32(io->context, COALESCING + 0x08, 150);     /* RX ticks */
    io->write32(io->context, COALESCING + 0x0c, 150);     /* TX ticks */
    io->write32(io->context, COALESCING + 0x10, 10);      /* RX frames */
    io->write32(io->context, COALESCING + 0x14, 10);      /* TX frames */
    io->write32(io->context, COALESCING + 0x20, 0);
    io->write32(io->context, COALESCING + 0x24, 0);
    io->write32(io->context, STATUS_BLOCK_HIGH, 0);
    io->write32(io->context, STATUS_BLOCK_HIGH + 4, rings->status);
    io->write32(io->context, COALESCING, 0x100 | COALESCING_ENABLE);

    io->write32(io->context, RX_BD_COMPLETION, ENGINE_ENABLE_ATTENTION);
    io->write32(io->context, RX_PLACEMENT, ENGINE_ENABLE);
    io->write32(io->context, MAC_MODE, mac_mode | MAC_MODE_CLEAR_STATISTICS);
    io->delay_us(io->context, 40);
    io->write32(io->context, LOCAL_CONTROL, 0x01000008UL);
    io->delay_us(io->context, 100);
    io->write32(io->context, WRITE_DMA, 0x200003feUL);
    io->delay_us(io->context, 40);
    io->write32(io->context, READ_DMA, 0x000303feUL);
    io->delay_us(io->context, 40);
    io->write32(io->context, RX_DATA_COMPLETION, ENGINE_ENABLE_ATTENTION);
    io->write32(io->context, TX_DATA_COMPLETION, ENGINE_ENABLE);
    io->write32(io->context, TX_BD_COMPLETION, ENGINE_ENABLE_ATTENTION);
    io->write32(io->context, RX_BD_INITIATOR, ENGINE_ENABLE_ATTENTION);
    io->write32(io->context, RX_BD_INITIATOR_DATA, 0x12);
    io->write32(io->context, TX_DATA_INITIATOR, ENGINE_ENABLE);
    io->write32(io->context, TX_BD_INITIATOR, ENGINE_ENABLE_ATTENTION);
    io->write32(io->context, TX_BD_SELECTOR, ENGINE_ENABLE_ATTENTION);
    io->write32(io->context, MAC_TX_MODE, tx_mode);
    io->delay_us(io->context, 40);
    io->write32(io->context, MAC_RX_MODE, rx_mode);
    io->delay_us(io->context, 40);

    io->write32(io->context, MI_MODE, 0x000c0000UL);       /* no auto poll */
    io->write32(io->context, MAC_LED, 0x800);
    io->write32(io->context, MI_STATUS, 1);
    io->write32(io->context, MAC_EVENT, 0x1000);
    return BCM_HW_OK;
}

unsigned bcm5722_stop(const struct bcm_hw_io *io)
{
    static const struct { uint16_t offset; uint32_t bit; } steps[] = {
        {MAC_RX_MODE, ENGINE_ENABLE}, {RX_BD_INITIATOR, ENGINE_ENABLE},
        {RX_PLACEMENT, ENGINE_ENABLE}, {RX_BD_INITIATOR_DATA, ENGINE_ENABLE},
        {RX_DATA_COMPLETION, ENGINE_ENABLE}, {RX_BD_COMPLETION, ENGINE_ENABLE},
        {TX_BD_SELECTOR, ENGINE_ENABLE}, {TX_BD_INITIATOR, ENGINE_ENABLE},
        {TX_DATA_INITIATOR, ENGINE_ENABLE}, {READ_DMA, ENGINE_ENABLE},
        {TX_DATA_COMPLETION, ENGINE_ENABLE}, {TX_BD_COMPLETION, ENGINE_ENABLE},
        {MAC_MODE, 0x200000UL}, {MAC_TX_MODE, ENGINE_ENABLE},
        {COALESCING, COALESCING_ENABLE}, {WRITE_DMA, ENGINE_ENABLE}
    };
    unsigned i, j, stuck = 0;
    for (i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i) {
        io->write32(io->context, steps[i].offset,
            io->read32(io->context, steps[i].offset) & ~steps[i].bit);
        for (j = 0; j < 4000; ++j) {
            io->delay_us(io->context, 10);
            if (!(io->read32(io->context, steps[i].offset) & steps[i].bit))
                break;
        }
        if (j == 4000)
            ++stuck;
    }
    io->write32(io->context, FTQ_RESET, 0xffffffffUL);
    io->write32(io->context, FTQ_RESET, 0);
    return stuck;
}
