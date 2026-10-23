#include "pci.h"

int bcm_find_5722(bcm_pci_read_fn read_config, void *context,
                  struct bcm_pci_device *result)
{
    unsigned bus, device, function, functions;
    uint32_t id, header, class_revision, irq;

    for (bus = 0; bus < 256; ++bus) {
        for (device = 0; device < 32; ++device) {
            id = read_config(context, bus, device, 0, 0);
            if ((id & 0xffffUL) == 0xffffUL)
                continue;
            header = read_config(context, bus, device, 0, 0x0c);
            functions = (header & 0x00800000UL) ? 8 : 1;
            for (function = 0; function < functions; ++function) {
                id = read_config(context, bus, device, function, 0);
                if (id != 0x165a14e4UL)
                    continue;
                class_revision = read_config(context, bus, device, function, 8);
                if ((class_revision >> 8) != 0x020000UL)
                    continue;
                header = read_config(context, bus, device, function, 0x0c);
                if ((header & 0x007f0000UL) != 0)
                    continue;
                result->bus = bus;
                result->device = device;
                result->function = function;
                result->class_revision = class_revision;
                result->command_status = read_config(context, bus, device, function, 4);
                result->bar0 = read_config(context, bus, device, function, 0x10);
                result->bar1 = read_config(context, bus, device, function, 0x14);
                result->subsystem = read_config(context, bus, device, function, 0x2c);
                irq = read_config(context, bus, device, function, 0x3c);
                result->irq = irq & 0xff;
                result->interrupt_pin = (irq >> 8) & 0xff;
                return 1;
            }
        }
    }
    return 0;
}

int bcm_pci_mmio_base(uint32_t bar0, uint32_t bar1, uint32_t *base)
{
    uint32_t address = bar0 & 0xfffffff0UL;
    unsigned type = (unsigned)((bar0 >> 1) & 3);
    if ((bar0 & 1) || (type != 0 && type != 2) ||
            (type == 2 && bar1 != 0) || !address || (address & 0xffff))
        return 0;
    *base = address;
    return 1;
}
