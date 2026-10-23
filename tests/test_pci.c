#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pci.h"

struct fixture {
    unsigned bus, device, function;
    uint32_t id, class_revision, header;
    unsigned reads;
};

static uint32_t read_config(void *context, unsigned bus, unsigned device,
                            unsigned function, unsigned offset)
{
    struct fixture *f = context;
    ++f->reads;
    assert(bus < 256 && device < 32 && function < 8);
    assert(offset <= 0xfc && (offset & 3) == 0);
    if (bus != f->bus || device != f->device)
        return 0xffffffffUL;
    if (function == 0 && f->function != 0) {
        if (offset == 0)
            return 0x12348086UL;
        if (offset == 0x0c)
            return f->header;
    }
    if (function != f->function)
        return 0xffffffffUL;
    switch (offset) {
    case 0: return f->id;
    case 4: return 7;
    case 8: return f->class_revision;
    case 0x0c: return f->header;
    case 0x10: return 0xfabc0004UL; /* 64-bit BAR; preserve flags */
    case 0x14: return 0;
    case 0x2c: return 0x02a61028UL;
    case 0x3c: return 0x010bUL;
    default: assert(0); return 0;
    }
}

int main(void)
{
    struct fixture f = {255, 31, 7, 0x165a14e4UL, 0x02000002UL,
                         0x00800000UL, 0};
    struct bcm_pci_device card, unchanged;

    assert(bcm_find_5722(read_config, &f, &card));
    assert(card.bus == 255 && card.device == 31 && card.function == 7);
    assert(card.bar0 == 0xfabc0004UL && card.bar1 == 0);
    assert(card.class_revision == 0x02000002UL);
    assert(card.subsystem == 0x02a61028UL);
    assert(card.irq == 11 && card.interrupt_pin == 1);
    assert(card.command_status == 7);

    f.bus = 0; f.device = 0; f.function = 0; f.header = 0;
    assert(bcm_find_5722(read_config, &f, &card));
    assert(card.bus == 0 && card.device == 0 && card.function == 0);

    memset(&card, 0xa5, sizeof(card));
    unchanged = card;
    f.id = 0xffffffffUL;
    assert(!bcm_find_5722(read_config, &f, &card));
    {
        uint32_t base = 0x12345678;
        assert(bcm_pci_mmio_base(0xfabc0004UL, 0, &base) && base == 0xfabc0000UL);
        assert(bcm_pci_mmio_base(0xfabc0000UL, 0xfeed, &base));
        base = 0x12345678;
        assert(!bcm_pci_mmio_base(0xfabc0004UL, 1, &base));
        assert(!bcm_pci_mmio_base(0xfabc0001UL, 0, &base));
        assert(!bcm_pci_mmio_base(0xfabc0006UL, 0, &base));
        assert(!bcm_pci_mmio_base(0, 0, &base));
        assert(!bcm_pci_mmio_base(4, 0, &base));
        assert(!bcm_pci_mmio_base(0xfabc1000UL, 0, &base));
        assert(base == 0x12345678);
    }
    assert(memcmp(&card, &unchanged, sizeof(card)) == 0);
    f.id = 0x160014e4UL; /* BCM5752 is not implicitly supported. */
    assert(!bcm_find_5722(read_config, &f, &card));
    f.id = 0x165a14e4UL; f.class_revision = 0x06040000UL;
    assert(!bcm_find_5722(read_config, &f, &card));
    f.class_revision = 0x02000000UL; f.header = 0x00010000UL;
    assert(!bcm_find_5722(read_config, &f, &card));
    f.function = 1; f.header = 0;
    assert(!bcm_find_5722(read_config, &f, &card));
    puts("PCI tests: normal and multifunction discovery, absent/wrong devices,"
         " class/header checks, unchanged result on failure passed");
    return 0;
}
