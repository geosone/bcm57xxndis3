/* SPDX-License-Identifier: GPL-2.0-or-later
 * Adapted from readMII, writeMII, resetPHY, enableAutoMDIX,
 * enableEthernetAtWirespeed, startAutoNegotiation, setupPHY, setupFlowControl
 * and configureMAC in BCM5722D/PHY.cpp, Copyright (C) 2010 Adlan Razalan.
 * BCM5722 (gigabit PHY) only; the BCM5906 fast Ethernet paths are left out.
 */
#include "bcm5722_phy.h"

#define MAC_MODE 0x400
#define MAC_MODE_HALF_DUPLEX 0x2UL
#define MAC_MODE_PORT_MASK 0xcUL
#define MAC_MODE_PORT_MII 0x4UL
#define MAC_MODE_PORT_GMII 0x8UL
#define MAC_STATUS 0x404
#define MAC_STATUS_CHANGES 0x00401018UL /* config, sync, link, MI completion */
#define MAC_EVENT 0x408
#define MAC_EVENT_LINK 0x1000UL
#define MI_COMM 0x44c
#define MI_COMM_WRITE 0x04000000UL
#define MI_COMM_READ 0x08000000UL
#define MI_COMM_BUSY 0x20000000UL /* also the start bit */
#define MI_COMM_PHY1 (1UL << 21)
#define MI_MODE 0x454
#define MI_MODE_PORT_POLL 0x10UL
#define TX_MODE 0x45c
#define TX_MODE_FLOW 0x10UL
#define TX_LENGTHS 0x464
#define RX_MODE 0x468
#define RX_MODE_FLOW 0x4UL

#define PHY_CONTROL 0x00
#define PHY_CONTROL_AUTONEG_RESTART 0x0200
#define PHY_CONTROL_AUTONEG 0x1000
#define PHY_CONTROL_RESET 0x8000
#define PHY_STATUS 0x01
#define PHY_STATUS_LINK 0x0004
#define PHY_ID1 0x02
#define PHY_ID2 0x03
#define PHY_ADVERTISE 0x04
#define PHY_PARTNER 0x05
#define PHY_ADVERTISE_ALL 0x01e1 /* 802.3, 10/100 half and full */
#define PHY_PAUSE 0x0400
#define PHY_ASYM_PAUSE 0x0800
#define PHY_1000_CONTROL 0x09
#define PHY_1000_ADVERTISE_ALL 0x0300
#define PHY_AUX_CONTROL 0x18
#define PHY_AUX_MISC_READ 0x7007 /* shadow register 7, misc control */
#define PHY_AUX_MISC_WRITE 0x8000
#define PHY_AUX_MISC_WIRESPEED 0x0010
#define PHY_AUX_MISC_AUTO_MDIX 0x0200
#define PHY_AUX_STATUS 0x19
#define PHY_AUX_STATUS_LINK 0x0004
#define PHY_AUX_STATUS_MODE 0x0700
#define PHY_IRQ_STATUS 0x1a
#define PHY_IRQ_MASK 0x1b

static enum bcm_phy_result mii(const struct bcm_hw_io *io, uint32_t command,
                               uint16_t *value)
{
    uint32_t mode, comm = 0;
    unsigned i;
    mode = io->read32(io->context, MI_MODE);
    if (mode & MI_MODE_PORT_POLL) {
        io->write32(io->context, MI_MODE, mode & ~MI_MODE_PORT_POLL);
        io->delay_us(io->context, 40);
    }
    io->write32(io->context, MI_COMM, command | MI_COMM_BUSY | MI_COMM_PHY1);
    for (i = 0; i < 5000; ++i) {
        io->delay_us(io->context, 10);
        if (!(io->read32(io->context, MI_COMM) & MI_COMM_BUSY)) {
            io->delay_us(io->context, 5);
            comm = io->read32(io->context, MI_COMM);
            break;
        }
    }
    if (mode & MI_MODE_PORT_POLL) {
        io->write32(io->context, MI_MODE, mode);
        io->delay_us(io->context, 40);
    }
    if (i == 5000)
        return BCM_PHY_BUSY;
    if (value)
        *value = (uint16_t)comm;
    return BCM_PHY_OK;
}

enum bcm_phy_result bcm5722_mii_read(const struct bcm_hw_io *io, unsigned reg,
                                     uint16_t *value)
{
    enum bcm_phy_result result;
    result = mii(io, MI_COMM_READ | (uint32_t)(reg & 0x1f) << 16, value);
    if (result != BCM_PHY_OK)
        *value = 0;
    return result;
}

enum bcm_phy_result bcm5722_mii_write(const struct bcm_hw_io *io, unsigned reg,
                                      uint16_t value)
{
    return mii(io, MI_COMM_WRITE | (uint32_t)(reg & 0x1f) << 16 | value, 0);
}

enum bcm_phy_result bcm5722_phy_id(const struct bcm_hw_io *io, uint32_t *id)
{
    uint16_t id1, id2;
    enum bcm_phy_result result;
    if ((result = bcm5722_mii_read(io, PHY_ID1, &id1)) != BCM_PHY_OK ||
            (result = bcm5722_mii_read(io, PHY_ID2, &id2)) != BCM_PHY_OK)
        return result;
    *id = (uint32_t)id1 << 10 | (uint32_t)(id2 & 0xfc00) << 16 |
          (id2 & 0x03ff);
    return BCM_PHY_OK;
}

/* Read-modify-write of the misc control shadow register; BCM5722D's
 * wirespeed path combined the two results with & and lost failures. */
static enum bcm_phy_result set_misc(const struct bcm_hw_io *io, uint16_t bits)
{
    uint16_t value;
    enum bcm_phy_result result;
    if ((result = bcm5722_mii_write(io, PHY_AUX_CONTROL, PHY_AUX_MISC_READ)) !=
            BCM_PHY_OK ||
            (result = bcm5722_mii_read(io, PHY_AUX_CONTROL, &value)) !=
            BCM_PHY_OK)
        return result;
    return bcm5722_mii_write(io, PHY_AUX_CONTROL,
                             (uint16_t)(value | bits | PHY_AUX_MISC_WRITE));
}

enum bcm_phy_result bcm5722_phy_reset(const struct bcm_hw_io *io)
{
    uint16_t value;
    unsigned i;
    enum bcm_phy_result result;
    if ((result = bcm5722_mii_read(io, PHY_STATUS, &value)) != BCM_PHY_OK ||
            (result = bcm5722_mii_read(io, PHY_STATUS, &value)) != BCM_PHY_OK ||
            (result = bcm5722_mii_write(io, PHY_CONTROL, PHY_CONTROL_RESET)) !=
            BCM_PHY_OK)
        return result;
    for (i = 0; i < 5000; ++i) {
        if (bcm5722_mii_read(io, PHY_CONTROL, &value) == BCM_PHY_OK &&
                !(value & PHY_CONTROL_RESET)) {
            io->delay_us(io->context, 40);
            break;
        }
        io->delay_us(io->context, 10);
    }
    if (i == 5000)
        return BCM_PHY_RESET_TIMEOUT;
    if ((result = set_misc(io, PHY_AUX_MISC_AUTO_MDIX)) != BCM_PHY_OK)
        return result;
    return set_misc(io, PHY_AUX_MISC_WIRESPEED);
}

enum bcm_phy_result bcm5722_phy_autoneg(const struct bcm_hw_io *io)
{
    enum bcm_phy_result result;
    if ((result = bcm5722_mii_write(io, PHY_ADVERTISE,
                                    PHY_ADVERTISE_ALL | PHY_PAUSE)) !=
            BCM_PHY_OK ||
            (result = bcm5722_mii_write(io, PHY_1000_CONTROL,
                                        PHY_1000_ADVERTISE_ALL)) != BCM_PHY_OK)
        return result;
    return bcm5722_mii_write(io, PHY_CONTROL,
                             PHY_CONTROL_AUTONEG | PHY_CONTROL_AUTONEG_RESTART);
}

static void resolve_mode(uint16_t aux, struct bcm_link *link)
{
    static const unsigned speeds[8] = {0, 10, 10, 100, 0, 100, 1000, 1000};
    unsigned mode = (aux & PHY_AUX_STATUS_MODE) >> 8;
    link->speed = speeds[mode];
    link->full_duplex = mode == 2 || mode == 5 || mode == 7;
}

/* IEEE 802.3 annex 28B pause resolution, as BCM5722D's setupFlowControl. */
static enum bcm_flow resolve_flow(uint16_t local, uint16_t partner)
{
    if (local & partner & PHY_PAUSE)
        return BCM_FLOW_BOTH;
    if (local & partner & PHY_ASYM_PAUSE) {
        if (local & PHY_PAUSE)
            return BCM_FLOW_RX;
        if (partner & PHY_PAUSE)
            return BCM_FLOW_TX;
    }
    return BCM_FLOW_NONE;
}

enum bcm_phy_result bcm5722_phy_link(const struct bcm_hw_io *io,
                                     struct bcm_mac_modes *modes,
                                     struct bcm_link *link)
{
    uint16_t status, aux = 0, local, partner;
    unsigned i;
    enum bcm_phy_result result;

    link->up = 0;
    link->speed = 0;
    link->full_duplex = 0;
    link->flow = BCM_FLOW_NONE;

    /* Acknowledge MAC and PHY link events; PHY interrupts stay masked. */
    io->write32(io->context, MAC_EVENT, 0);
    io->write32(io->context, MAC_STATUS, MAC_STATUS_CHANGES);
    io->delay_us(io->context, 40);
    if ((result = bcm5722_mii_write(io, PHY_AUX_CONTROL, 0x02)) != BCM_PHY_OK ||
            (result = bcm5722_mii_read(io, PHY_IRQ_STATUS, &status)) !=
            BCM_PHY_OK ||
            (result = bcm5722_mii_read(io, PHY_IRQ_STATUS, &status)) !=
            BCM_PHY_OK ||
            (result = bcm5722_mii_write(io, PHY_IRQ_MASK, 0xffff)) != BCM_PHY_OK)
        return result;

    /* The link bit latches low; the second read is the current state. */
    if ((result = bcm5722_mii_read(io, PHY_STATUS, &status)) != BCM_PHY_OK ||
            (result = bcm5722_mii_read(io, PHY_STATUS, &status)) != BCM_PHY_OK)
        return result;
    if (status & PHY_STATUS_LINK) {
        for (i = 0; i < 2000; ++i) {
            if ((result = bcm5722_mii_read(io, PHY_AUX_STATUS, &aux)) !=
                    BCM_PHY_OK)
                return result;
            if (aux & PHY_AUX_STATUS_LINK)
                break;
            io->delay_us(io->context, 10);
        }
        resolve_mode(aux, link);
        link->up = link->speed != 0;
        if (link->up && link->full_duplex) {
            if ((result = bcm5722_mii_read(io, PHY_ADVERTISE, &local)) !=
                    BCM_PHY_OK ||
                    (result = bcm5722_mii_read(io, PHY_PARTNER, &partner)) !=
                    BCM_PHY_OK)
                return result;
            link->flow = resolve_flow(local, partner);
        }
    }

    /* Without a known speed the MAC stays in GMII, as in BCM5722D. */
    modes->mac_mode &= ~(MAC_MODE_PORT_MASK | MAC_MODE_HALF_DUPLEX);
    modes->mac_mode |= link->speed == 10 || link->speed == 100 ?
        MAC_MODE_PORT_MII : MAC_MODE_PORT_GMII;
    if (link->up && !link->full_duplex)
        modes->mac_mode |= MAC_MODE_HALF_DUPLEX;
    io->write32(io->context, MAC_MODE, modes->mac_mode);
    io->delay_us(io->context, 40);
    io->write32(io->context, TX_LENGTHS,
        link->speed == 1000 && !link->full_duplex ? 0x26ffUL : 0x2620UL);

    /* BCM5722D left stale flow-control bits on half duplex; always rewrite. */
    modes->rx_mode &= ~RX_MODE_FLOW;
    modes->tx_mode &= ~TX_MODE_FLOW;
    if (link->flow == BCM_FLOW_RX || link->flow == BCM_FLOW_BOTH)
        modes->rx_mode |= RX_MODE_FLOW;
    if (link->flow == BCM_FLOW_TX || link->flow == BCM_FLOW_BOTH)
        modes->tx_mode |= TX_MODE_FLOW;
    io->write32(io->context, RX_MODE, modes->rx_mode);
    io->write32(io->context, TX_MODE, modes->tx_mode);

    io->write32(io->context, MAC_EVENT, MAC_EVENT_LINK);
    io->delay_us(io->context, 40);
    return BCM_PHY_OK;
}
