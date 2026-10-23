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
    BCM_HW_NVRAM_TIMEOUT, BCM_HW_FIRMWARE_TIMEOUT
};

enum bcm_hw_result bcm5722_reset(const struct bcm_hw_io *);

#endif
