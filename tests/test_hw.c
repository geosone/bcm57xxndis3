#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "bcm5722_hw.h"

struct fixture {
    uint32_t regs[0x8000 / 4];
    uint16_t device, command, pcie_control;
    int grant_nvram, acknowledge_firmware, reset_seen, reset_guard, tlp_written;
    unsigned writes, delays;
};

static uint32_t read32(void *context, unsigned offset)
{
    struct fixture *f = context;
    assert(offset < 0x8000 && !(offset & 3));
    if (offset == 0x7020)
        return f->grant_nvram ? 0x200 : 0;
    if (offset == 0x84 && f->regs[0x7c / 4] == 0xb50)
        return f->acknowledge_firmware && f->reset_seen ? 0xb49a89abUL : 0;
    return f->regs[offset / 4];
}

static void write32(void *context, unsigned offset, uint32_t value)
{
    struct fixture *f = context;
    assert(offset < 0x8000 && !(offset & 3));
    ++f->writes;
    if (offset == 0x6804) {
        if (value & 1) {
            assert(f->reset_guard && (value & 0x20000000UL));
            f->reset_seen = 1;
        } else if (value == 0x20000000UL)
            f->reset_guard = 1;
    }
    if (offset == 0x84) {
        assert(f->regs[0x7c / 4] == 0xb50 && value == 0x4b657654UL);
        assert(!f->reset_seen);
    }
    if (offset == 0x7c00) {
        assert(f->acknowledge_firmware && f->reset_seen);
        f->tlp_written = 1;
    }
    f->regs[offset / 4] = value;
}

static uint16_t pci_read16(void *context, unsigned offset)
{
    struct fixture *f = context;
    switch (offset) {
    case 0: return 0x14e4;
    case 2: return f->device;
    case 4: return f->command;
    case 0xd8: return f->pcie_control;
    default: assert(0); return 0;
    }
}

static void pci_write16(void *context, unsigned offset, uint16_t value)
{
    struct fixture *f = context;
    ++f->writes;
    if (offset == 4) assert(value == f->command);
    else if (offset == 0xd8) f->pcie_control = value;
    else assert(offset == 0xda && value == 15);
}

static void pci_write32(void *context, unsigned offset, uint32_t value)
{
    struct fixture *f = context;
    ++f->writes;
    assert(offset == 0x68 && value == 0x29a);
}

static void delay_us(void *context, unsigned delay)
{
    struct fixture *f = context;
    assert(delay == 10 || delay == 40 || delay == 100000);
    f->delays += delay;
}

static void prepare(struct fixture *f)
{
    memset(f, 0, sizeof(*f));
    f->device = 0x165a;
    f->command = 7;
    f->pcie_control = 0xffff;
    f->grant_nvram = f->acknowledge_firmware = 1;
    f->regs[0x7e2c / 4] = 0x60;
}

int main(void)
{
    struct fixture f;
    struct bcm_hw_io io = {&f, read32, write32, pci_read16,
                           pci_write16, pci_write32, delay_us};
    assert(offsetof(struct bcm_tx_descriptor, flags) == 8);
    assert(offsetof(struct bcm_rx_descriptor, opaque) == 28);
    prepare(&f);
    assert(bcm5722_reset(&io) == BCM_HW_OK);
    assert(f.reset_seen && f.tlp_written && f.delays == 100040);
    assert(f.regs[0x7c / 4] == 0 && f.regs[0x204 / 4] == 1);
    assert(f.regs[0x7e2c / 4] == 0x20 && f.pcie_control == 0xf70f);
    prepare(&f); f.device = 0x1600;
    assert(bcm5722_reset(&io) == BCM_HW_WRONG_DEVICE && !f.writes);
    prepare(&f); f.command = 2;
    assert(bcm5722_reset(&io) == BCM_HW_PCI_DISABLED && !f.writes);
    prepare(&f); f.grant_nvram = 0;
    assert(bcm5722_reset(&io) == BCM_HW_NVRAM_TIMEOUT && !f.reset_seen);
    assert(f.regs[0x7020 / 4] == 0x20 && f.delays == 10000);
    prepare(&f); f.acknowledge_firmware = 0;
    assert(bcm5722_reset(&io) == BCM_HW_FIRMWARE_TIMEOUT && !f.tlp_written);
    assert(f.regs[0x7c / 4] == 0 && f.regs[0x7020 / 4] == 0x20);
    puts("Hardware tests: descriptor layout, reset ordering, disabled PCI and timeouts passed");
    return 0;
}
