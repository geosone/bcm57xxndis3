#include "platform.h"
#include "bcm5722_ring.h"

/* PIT input 1.19318 MHz; in mode 3 the counter steps by 2, 2.386 counts/us.
 * In mode 2 the same count takes twice as long, which only lengthens delays. */
#define PIT_COUNTS_PER_MS 2387UL

static uint32_t config_address(const struct bcm_platform *p, unsigned offset)
{
    return 0x80000000UL | ((uint32_t)p->bus << 16) |
           ((uint32_t)p->device << 11) | ((uint32_t)p->function << 8) |
           (offset & 0xff);
}

uint32_t bcm_platform_pci_read(void *context, unsigned bus, unsigned device,
                               unsigned function, unsigned offset)
{
    struct bcm_platform p;
    (void)context;
    p.bus = bus;
    p.device = device;
    p.function = function;
    return bcm_config_read(config_address(&p, offset & 0xfc));
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

#define RING_PAGES (BCM_RING_BLOCK_SIZE / 4096)
#define BUFFER_PAGES ((BCM_RX_BUFFERS + BCM_TX_BUFFERS) / 2)

static uint32_t ring_handle, buffer_handle;

static int contiguous(const uint8_t *linear, unsigned pages)
{
    uint32_t physical = bcm_lin_to_phys(linear);
    unsigned i;
    for (i = 1; i < pages; ++i)
        if (bcm_lin_to_phys(linear + i * 4096UL) != physical + i * 4096UL)
            return 0;
    return 1;
}

/* The ring block: one PageContig allocation, as NDIS.386 does for shared
 * memory. VMM grants that only during system initialization; later, ordinary
 * pages are tried, as they are only sometimes contiguous. A non-contiguous
 * block stays allocated until the next try is done, so that the next
 * allocation cannot return the same pages. */
static uint8_t *ring_block(void)
{
    uint32_t failed[8];
    unsigned tries, i;
    uint8_t *block = bcm_page_alloc_contiguous(RING_PAGES, &ring_handle);

    if (block && contiguous(block, RING_PAGES))
        return block;
    if (block)
        bcm_page_free(ring_handle);
    block = 0;
    for (tries = 0; tries < 8; ++tries) {
        block = bcm_page_alloc(RING_PAGES, &ring_handle);
        if (!block)
            break;
        if (contiguous(block, RING_PAGES))
            break;
        failed[tries] = ring_handle;
        block = 0;
    }
    for (i = 0; i < tries; ++i)
        bcm_page_free(failed[i]);
    if (!block)
        ring_handle = 0;
    return block;
}

int bcm_platform_dma_alloc(struct bcm_rings *r)
{
    unsigned i;
    uint8_t *block = ring_block(), *buffers;

    if (!block)
        return -1;
    buffers = bcm_page_alloc(BUFFER_PAGES, &buffer_handle);
    if (!buffers) {
        bcm_page_free(ring_handle);
        ring_handle = buffer_handle = 0;
        return -1;
    }
    bcm_rings_setup(r, block, bcm_lin_to_phys(block));
    for (i = 0; i < BCM_RX_BUFFERS + BCM_TX_BUFFERS; ++i) {
        uint8_t *buffer = buffers + i * 2048UL;
        if (i < BCM_RX_BUFFERS) {
            r->rx_buffer[i] = buffer;
            r->rx_phys[i] = bcm_lin_to_phys(buffer);
        } else {
            r->tx_buffer[i - BCM_RX_BUFFERS] = buffer;
            r->tx_phys[i - BCM_RX_BUFFERS] = bcm_lin_to_phys(buffer);
        }
    }
    return 0;
}

void bcm_platform_dma_free(void)
{
    if (buffer_handle)
        bcm_page_free(buffer_handle);
    if (ring_handle)
        bcm_page_free(ring_handle);
    buffer_handle = ring_handle = 0;
}
