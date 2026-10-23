/* SPDX-License-Identifier: GPL-2.0-or-later
 * PHY access and link setup adapted from BCM5722D/PHY.cpp.
 * Copyright (C) 2010 Adlan Razalan <dev at adlan dot name dot my>
 * See docs/hardware-port.md and COPYING for attribution and license.
 */
#ifndef BCM5722_PHY_H
#define BCM5722_PHY_H

#include "bcm5722_hw.h"

enum bcm_phy_result { BCM_PHY_OK, BCM_PHY_BUSY, BCM_PHY_RESET_TIMEOUT };

enum bcm_flow { BCM_FLOW_NONE, BCM_FLOW_RX, BCM_FLOW_TX, BCM_FLOW_BOTH };

struct bcm_link {
    int up;
    unsigned speed;                   /* 10, 100, 1000; 0 if unknown */
    int full_duplex;
    enum bcm_flow flow;
};

/* MAC register shadows owned by the caller; link setup changes the port
 * mode, duplex and flow-control bits and writes them to the chip. */
struct bcm_mac_modes {
    uint32_t mac_mode, rx_mode, tx_mode;
};

enum bcm_phy_result bcm5722_mii_read(const struct bcm_hw_io *, unsigned reg,
                                     uint16_t *value);
enum bcm_phy_result bcm5722_mii_write(const struct bcm_hw_io *, unsigned reg,
                                      uint16_t value);

/* PHY identifier: OUI << 10 | model << 4 | revision (as BCM5722D). */
enum bcm_phy_result bcm5722_phy_id(const struct bcm_hw_io *, uint32_t *id);

/* Resets the PHY, then enables auto-MDIX and Ethernet@Wirespeed. */
enum bcm_phy_result bcm5722_phy_reset(const struct bcm_hw_io *);

/* Advertises 10/100/1000 half and full duplex and symmetric pause, then
 * restarts autonegotiation. */
enum bcm_phy_result bcm5722_phy_autoneg(const struct bcm_hw_io *);

/* Reads the link state, programs MAC port mode, duplex, TX lengths and flow
 * control to match, and acknowledges the link-change event. */
enum bcm_phy_result bcm5722_phy_link(const struct bcm_hw_io *,
                                     struct bcm_mac_modes *,
                                     struct bcm_link *);

#endif
