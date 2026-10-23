#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bcm5722_phy.h"

/* MAC registers plus a PHY behind the MI interface. A transaction takes
 * busy_reads polls of MI_COMM before it completes. */
struct fixture {
    uint32_t regs[0x800 / 4];
    uint16_t phy[32], misc_shadow;
    unsigned busy_reads, busy_left, reset_reads, polls_while_busy;
    int hang, link_latched_low, shadow_selected, reset_stuck;
    unsigned mii_writes, mode_changes;
    uint16_t last_mii_reg[64];
    unsigned mii_count;
};

static void complete(struct fixture *f)
{
    uint32_t comm = f->regs[0x44c / 4];
    unsigned reg = (comm >> 16) & 0x1f;
    uint16_t data = (uint16_t)comm;
    assert(((comm >> 21) & 0x1f) == 1);
    assert(!(f->regs[0x454 / 4] & 0x10)); /* auto polling must be off */
    if (f->mii_count < 64)
        f->last_mii_reg[f->mii_count++] = (uint16_t)reg;
    if (comm & 0x04000000UL) {
        ++f->mii_writes;
        if (reg == 0x18 && data == 0x7007)
            f->shadow_selected = 1;
        else if (reg == 0x18 && (data & 0x8000)) {
            assert(f->shadow_selected);
            f->misc_shadow = data & 0x7fff;
            f->shadow_selected = 0;
        } else if (reg == 0 && (data & 0x8000)) {
            f->phy[0] = data;
            f->reset_reads = f->reset_stuck ? 0 : 3;
        } else
            f->phy[reg] = data;
    } else {
        assert(comm & 0x08000000UL);
        if (reg == 0x18 && f->shadow_selected)
            data = f->misc_shadow;
        else if (reg == 0 && f->reset_reads && !--f->reset_reads)
            data = f->phy[0] &= 0x7fff;
        else if (reg == 1 && f->link_latched_low) {
            f->link_latched_low = 0;
            data = f->phy[1] & ~0x0004;
        } else
            data = f->phy[reg];
    }
    f->regs[0x44c / 4] = (comm & 0xffff0000UL & ~0x20000000UL) | data;
}

static uint32_t read32(void *context, unsigned offset)
{
    struct fixture *f = context;
    assert(offset < 0x800 && !(offset & 3));
    if (offset == 0x44c && (f->regs[offset / 4] & 0x20000000UL)) {
        ++f->polls_while_busy;
        if (!f->hang && !f->busy_left--)
            complete(f);
    }
    return f->regs[offset / 4];
}

static void write32(void *context, unsigned offset, uint32_t value)
{
    struct fixture *f = context;
    assert(offset < 0x800 && !(offset & 3));
    if (offset == 0x44c) {
        assert(value & 0x20000000UL);
        assert(f->hang || !(f->regs[offset / 4] & 0x20000000UL));
        f->busy_left = f->busy_reads;
    }
    if (offset == 0x454)
        ++f->mode_changes;
    f->regs[offset / 4] = value;
}

static uint16_t pci_read16(void *context, unsigned offset)
{
    (void)context; (void)offset;
    assert(0);
    return 0;
}

static void pci_write16(void *context, unsigned offset, uint16_t value)
{
    (void)context; (void)offset; (void)value;
    assert(0);
}

static void pci_write32(void *context, unsigned offset, uint32_t value)
{
    (void)context; (void)offset; (void)value;
    assert(0);
}

static void delay_us(void *context, unsigned us)
{
    (void)context; (void)us;
}

static struct fixture f;
static const struct bcm_hw_io io = {
    &f, read32, write32, pci_read16, pci_write16, pci_write32, delay_us
};

static void setup(unsigned busy_reads)
{
    memset(&f, 0, sizeof(f));
    f.busy_reads = busy_reads;
    f.phy[2] = 0x0143;   /* Broadcom OUI */
    f.phy[3] = 0xbcc1;
}

static void test_mii(void)
{
    uint16_t value;
    uint32_t id;
    setup(3);
    f.regs[0x454 / 4] = 0x000c0010UL; /* 66 MHz clock, auto polling */
    assert(bcm5722_mii_read(&io, 3, &value) == BCM_PHY_OK && value == 0xbcc1);
    assert(f.regs[0x454 / 4] == 0x000c0010UL && f.mode_changes == 2);
    assert(bcm5722_phy_id(&io, &id) == BCM_PHY_OK);
    assert(id == ((0x0143UL << 10) | (0xbc00UL << 16) | 0x00c1));

    setup(0);
    f.hang = 1;
    value = 0x1234;
    assert(bcm5722_mii_read(&io, 1, &value) == BCM_PHY_BUSY && value == 0);
    assert(f.polls_while_busy == 5000);
    assert(bcm5722_mii_write(&io, 4, 0x1e1) == BCM_PHY_BUSY);
}

static void test_reset(void)
{
    setup(1);
    f.misc_shadow = 0x0100;
    assert(bcm5722_phy_reset(&io) == BCM_PHY_OK);
    assert(!(f.phy[0] & 0x8000));
    assert(f.misc_shadow == (0x0100 | 0x0200 | 0x0010)); /* MDIX, wirespeed */

    /* The reset bit never clears: timeout, no MDIX/wirespeed writes. */
    setup(0);
    f.reset_stuck = 1;
    assert(bcm5722_phy_reset(&io) == BCM_PHY_RESET_TIMEOUT);
    assert(f.misc_shadow == 0);

    /* A hanging MI bus fails before the reset is written. */
    setup(0);
    f.hang = 1;
    assert(bcm5722_phy_reset(&io) == BCM_PHY_BUSY);
    assert(f.mii_writes == 0);
}

static void test_autoneg(void)
{
    setup(2);
    assert(bcm5722_phy_autoneg(&io) == BCM_PHY_OK);
    assert(f.phy[4] == 0x05e1);
    assert(f.phy[9] == 0x0300);
    assert(f.phy[0] == 0x1200);
}

static void link_case(uint16_t aux, uint16_t partner, unsigned speed,
                      int full, enum bcm_flow flow, uint32_t port,
                      uint32_t txlen)
{
    struct bcm_mac_modes modes = {0x00c00000UL | 0x2 | 0xc, 0x4, 0x10};
    struct bcm_link link;
    setup(1);
    f.phy[1] = 0x796d;
    f.link_latched_low = 1;
    f.phy[0x19] = aux | 0x0004;
    f.phy[4] = 0x05e1;
    f.phy[5] = partner;
    f.regs[0x404 / 4] = 0x00401018UL;
    assert(bcm5722_phy_link(&io, &modes, &link) == BCM_PHY_OK);
    assert(link.up && link.speed == speed && link.full_duplex == full);
    assert(link.flow == flow);
    assert((modes.mac_mode & 0xe) == (port | (full ? 0 : 2)));
    assert(modes.mac_mode & 0x00c00000UL);  /* unrelated bits kept */
    assert(f.regs[0x400 / 4] == modes.mac_mode);
    assert(f.regs[0x464 / 4] == txlen);
    assert(!!(f.regs[0x468 / 4] & 4) == (flow == BCM_FLOW_RX || flow == BCM_FLOW_BOTH));
    assert(!!(f.regs[0x45c / 4] & 0x10) == (flow == BCM_FLOW_TX || flow == BCM_FLOW_BOTH));
    assert(f.regs[0x408 / 4] == 0x1000);
    assert(f.phy[0x1b] == 0xffff);
}

static void test_link(void)
{
    struct bcm_mac_modes modes = {0x8, 0x4, 0x10};
    struct bcm_link link;

    link_case(0x0700, 0x0400, 1000, 1, BCM_FLOW_BOTH, 0x8, 0x2620);
    link_case(0x0600, 0x0400, 1000, 0, BCM_FLOW_NONE, 0x8, 0x26ff);
    link_case(0x0500, 0x0000, 100, 1, BCM_FLOW_NONE, 0x4, 0x2620);
    link_case(0x0300, 0x0400, 100, 0, BCM_FLOW_NONE, 0x4, 0x2620);
    link_case(0x0200, 0x0c00, 10, 1, BCM_FLOW_BOTH, 0x4, 0x2620);
    link_case(0x0100, 0x0000, 10, 0, BCM_FLOW_NONE, 0x4, 0x2620);

    /* No link: GMII, flow control and half duplex cleared. */
    setup(1);
    modes.mac_mode = 0x6;
    f.phy[1] = 0x7949;
    assert(bcm5722_phy_link(&io, &modes, &link) == BCM_PHY_OK);
    assert(!link.up && link.speed == 0 && link.flow == BCM_FLOW_NONE);
    assert(modes.mac_mode == 0x8);
    assert(!(f.regs[0x468 / 4] & 4) && !(f.regs[0x45c / 4] & 0x10));
}

int main(void)
{
    test_mii();
    test_reset();
    test_autoneg();
    test_link();
    puts("PHY tests: MI polling and timeouts, reset with MDIX/wirespeed, autonegotiation, link speed/duplex/flow and MAC mode passed");
    return 0;
}
