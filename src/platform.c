#include "platform.h"

/* PIT input 1.19318 MHz; in mode 3 the counter steps by 2, 2.386 counts/us.
 * In mode 2 the same count takes twice as long, which only lengthens delays. */
#define PIT_COUNTS_PER_MS 2387UL

static uint32_t config_address(const struct bcm_platform *p, unsigned offset)
{
    return 0x80000000UL | ((uint32_t)p->bus << 16) |
           ((uint32_t)p->device << 11) | ((uint32_t)p->function << 8) |
           (offset & 0xff);
}

static uint32_t mmio_read32(void *context, unsigned offset)
{
    struct bcm_platform *p = context;
    return p->mmio[offset / 4];
}

static void mmio_write32(void *context, unsigned offset, uint32_t value)
{
    struct bcm_platform *p = context;
    p->mmio[offset / 4] = value;
}

static uint16_t pci_read16(void *context, unsigned offset)
{
    return bcm_config_read16(config_address(context, offset));
}

static void pci_write16(void *context, unsigned offset, uint16_t value)
{
    bcm_config_write16(config_address(context, offset), value);
}

static void pci_write32(void *context, unsigned offset, uint32_t value)
{
    bcm_config_write32(config_address(context, offset), value);
}

static void delay_us(void *context, unsigned us)
{
    (void)context;
    bcm_platform_delay_us(us);
}

/* The PIT channel 0 reload value belongs to Windows and is unknown here. On a
 * wrap only the counts down to zero are taken, so a wrap never shortens it. */
static void wait_counts(uint32_t counts)
{
    uint32_t elapsed = 0;
    unsigned old = bcm_pit_count(), now;
    while (elapsed < counts) {
        now = bcm_pit_count();
        elapsed += now <= old ? old - now : old;
        old = now;
    }
}

void bcm_platform_delay_us(unsigned us)
{
    while (us >= 1000) {
        wait_counts(PIT_COUNTS_PER_MS);
        us -= 1000;
    }
    if (us)
        wait_counts(((uint32_t)us * PIT_COUNTS_PER_MS + 999) / 1000);
}

void bcm_platform_io(struct bcm_platform *platform, struct bcm_hw_io *io)
{
    io->context = platform;
    io->read32 = mmio_read32;
    io->write32 = mmio_write32;
    io->pci_read16 = pci_read16;
    io->pci_write16 = pci_write16;
    io->pci_write32 = pci_write32;
    io->delay_us = delay_us;
}
