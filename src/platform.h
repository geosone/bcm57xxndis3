/* Ring-0 services behind struct bcm_hw_io: mapped MMIO, PCI configuration
 * mechanism 1 and PIT-based delays. */
#ifndef BCM57_PLATFORM_H
#define BCM57_PLATFORM_H

#include <stdint.h>
#include "bcm5722_hw.h"

struct bcm_platform {
    unsigned bus, device, function;
    volatile uint32_t *mmio;          /* BAR0, mapped for 64 KiB */
};

/* Fills io with callbacks that use platform as their context. */
void bcm_platform_io(struct bcm_platform *platform, struct bcm_hw_io *io);

/* Busy-waits at least us microseconds; never shorter, possibly longer. */
void bcm_platform_delay_us(unsigned us);

/* Provided by entry.asm (host tests supply their own). Offsets may address
 * any byte of a register; 16-bit accesses use the matching 0CFCh port. */
uint32_t bcm_config_read(uint32_t address);
uint16_t bcm_config_read16(uint32_t address);
void bcm_config_write16(uint32_t address, uint16_t value);
void bcm_config_write32(uint32_t address, uint32_t value);
unsigned bcm_pit_count(void);

#endif
