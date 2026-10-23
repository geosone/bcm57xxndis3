/* SPDX-License-Identifier: GPL-2.0-or-later
 * Hardware layouts and reset sequence adapted from BCM5722D.
 * Copyright (C) 2010 Adlan Razalan <dev at adlan dot name dot my>
 * See docs/hardware-port.md and COPYING for attribution and license.
 */
#ifndef BCM5722_HW_H
#define BCM5722_HW_H

#include <stdint.h>

struct bcm_tx_descriptor {
    uint32_t address_high, address_low;
    uint16_t flags, length, vlan_tag, mss;
};

struct bcm_rx_descriptor {
    uint32_t address_high, address_low;
    uint16_t length, index, flags, type, tcp_checksum, ip_checksum;
    uint16_t vlan_tag, error_flags;
    uint32_t rss_hash, opaque;
};

typedef char bcm_tx_size[(sizeof(struct bcm_tx_descriptor) == 16) ? 1 : -1];
typedef char bcm_rx_size[(sizeof(struct bcm_rx_descriptor) == 32) ? 1 : -1];

/* All offsets are byte offsets; the platform supplies serialized little-endian
 * register accesses and a delay in microseconds. MMIO must already be mapped,
 * PCI memory decoding and bus mastering enabled, and the adapter quiescent.
 * The caller exclusively owns the adapter throughout this sequence.
 */
struct bcm_hw_io {
    void *context;
    uint32_t (*read32)(void *, unsigned);
    void (*write32)(void *, unsigned, uint32_t);
    uint16_t (*pci_read16)(void *, unsigned);
    void (*pci_write16)(void *, unsigned, uint16_t);
    void (*pci_write32)(void *, unsigned, uint32_t);
    void (*delay_us)(void *, unsigned);
};

enum bcm_hw_result {
    BCM_HW_OK, BCM_HW_WRONG_DEVICE, BCM_HW_PCI_DISABLED,
    BCM_HW_NVRAM_TIMEOUT, BCM_HW_FIRMWARE_TIMEOUT, BCM_HW_BUFFER_MANAGER_TIMEOUT
};

enum bcm_hw_result bcm5722_reset(const struct bcm_hw_io *);

/* Physical addresses of the rings (below 4 GiB) and the number of standard
 * ring slots already posted. */
struct bcm_ring_addresses {
    uint32_t tx, std, ret, status;
    unsigned std_posted;
};

/* After bcm5722_reset: programs DMA, buffer manager, rings, coalescing and
 * the MAC address, then enables all engines. mac_mode/rx_mode/tx_mode are
 * the caller's register shadows (struct bcm_mac_modes in bcm5722_phy.h). */
enum bcm_hw_result bcm5722_start(const struct bcm_hw_io *,
    const struct bcm_ring_addresses *, const uint8_t mac[6],
    uint32_t mac_mode, uint32_t rx_mode, uint32_t tx_mode);

/* Disables receive, transmit and DMA engines in the documented order;
 * returns the number of engines whose enable bit did not clear. */
unsigned bcm5722_stop(const struct bcm_hw_io *);

/* Station address the bootcode loaded into MAC address register 0. */
void bcm5722_read_mac(const struct bcm_hw_io *, uint8_t mac[6]);

/* Unmasks or masks INTA in the host control register. The status block is
 * tagged: while unmasked, the interrupt mailbox decides (bcm_irq_done). */
void bcm5722_enable_interrupts(const struct bcm_hw_io *, int enable);

/* Accepts every multicast frame (the bindings filter them) and sets or
 * clears promiscuous mode in the caller's RX mode shadow and the chip. */
void bcm5722_rx_filter(const struct bcm_hw_io *, uint32_t *rx_mode,
                       int promiscuous);

#endif
