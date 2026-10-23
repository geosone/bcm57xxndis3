#ifndef BCM57_PCI_H
#define BCM57_PCI_H

#include <stdint.h>

typedef uint32_t (*bcm_pci_read_fn)(void *, unsigned, unsigned, unsigned,
                                  unsigned);

struct bcm_pci_device {
    unsigned bus, device, function;
    uint32_t class_revision, bar0, bar1, subsystem, command_status;
    unsigned irq, interrupt_pin;
};

/* Returns the first Ethernet BCM5722; performs configuration reads only. */
int bcm_find_5722(bcm_pci_read_fn read_config, void *context,
                  struct bcm_pci_device *result);

/* BCM5722 BAR0 maps 64 KiB. Reject I/O, unassigned, reserved, or >4 GiB BARs. */
int bcm_pci_mmio_base(uint32_t bar0, uint32_t bar1, uint32_t *base);

#endif
