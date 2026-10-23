#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "platform.h"
#include "bcm5722_ring.h"

/* PIT channel 0 in mode 3: steps by 2 from reload down to 0, then reloads. */
static uint32_t pit_reload, pit_step, pit_time;

unsigned bcm_pit_count(void)
{
    pit_time += pit_step;
    return (unsigned)(pit_reload - (pit_time % pit_reload));
}

static uint32_t last_address, last_value, config_word;
static int writes16, writes32;

uint32_t bcm_config_read(uint32_t address)
{
    last_address = address;
    return 0;
}

uint16_t bcm_config_read16(uint32_t address)
{
    last_address = address;
    return (uint16_t)config_word;
}

void bcm_config_write16(uint32_t address, uint16_t value)
{
    last_address = address;
    last_value = value;
    ++writes16;
}

void bcm_config_write32(uint32_t address, uint32_t value)
{
    last_address = address;
    last_value = value;
    ++writes32;
}

static void test_config(void)
{
    struct bcm_platform p = {3, 0x1f, 7, 0};
    struct bcm_hw_io io;
    bcm_platform_io(&p, &io);
    assert(io.context == &p);

    config_word = 0x165a;
    assert(io.pci_read16(io.context, 2) == 0x165a);
    assert(last_address == 0x8003ff02UL);

    /* PCIe status: a word write next to the control word, no dword RMW. */
    io.pci_write16(io.context, 0xda, 0x0f);
    assert(last_address == 0x8003ffdaUL && last_value == 0x0f);
    assert(writes16 == 1 && writes32 == 0);

    io.pci_write32(io.context, 0x68, 0x29a);
    assert(last_address == 0x8003ff68UL && last_value == 0x29a);
    assert(writes16 == 1 && writes32 == 1);
}

static void test_mmio(void)
{
    uint32_t regs[0x10000 / 4] = {0};
    struct bcm_platform p = {0, 0, 0, regs};
    struct bcm_hw_io io;
    bcm_platform_io(&p, &io);
    io.write32(io.context, 0x6804, 0x20000000UL);
    assert(regs[0x6804 / 4] == 0x20000000UL);
    regs[0x68 / 4] = 0x57220000UL;
    assert(io.read32(io.context, 0x68) == 0x57220000UL);
}

/* The true time must reach the request whatever the reload and poll rate. */
static void test_delay(void)
{
    static const uint32_t reloads[] = {65536, 11932, 1193, 100};
    static const uint32_t steps[] = {2, 14, 501, 1501, 70001};
    static const unsigned delays[] = {1, 10, 40, 999, 1000, 100000};
    unsigned r, s, d;
    for (r = 0; r < sizeof(reloads) / sizeof(reloads[0]); ++r)
        for (s = 0; s < sizeof(steps) / sizeof(steps[0]); ++s)
            for (d = 0; d < sizeof(delays) / sizeof(delays[0]); ++d) {
                uint32_t start, need = (delays[d] * 2386UL + 999) / 1000;
                pit_reload = reloads[r];
                pit_step = steps[s];
                pit_time = 0;
                start = pit_time;
                bcm_platform_delay_us(delays[d]);
                assert(pit_time - start >= need);
            }
}

/* Page allocator: each allocation gets fresh linear pages; physical pages
 * are scattered unless the allocation number is in contiguous_from on. */
static uint8_t arena[64][4096 * 48];
static unsigned allocations, frees, contiguous_from, fail_at;
static unsigned contiguous_calls, contiguous_works;
static int forced_contiguous[64];
static uint32_t freed[64];

/* PageContig: refused unless contiguous_works. */
void *bcm_page_alloc_contiguous(unsigned pages, uint32_t *handle)
{
    assert(pages == BCM_RING_BLOCK_SIZE / 4096 && allocations < 64);
    ++contiguous_calls;
    if (!contiguous_works) {
        *handle = 0;
        return 0;
    }
    forced_contiguous[allocations] = 1;
    *handle = 0x1000 + allocations;
    return arena[allocations++];
}

void *bcm_page_alloc(unsigned pages, uint32_t *handle)
{
    assert(pages <= 48 && allocations < 64);
    if (allocations + 1 == fail_at) {
        *handle = 0;
        return 0;
    }
    *handle = 0x1000 + allocations;
    return arena[allocations++];
}

void bcm_page_free(uint32_t handle)
{
    unsigned i;
    for (i = 0; i < frees; ++i)
        assert(freed[i] != handle);                 /* no double free */
    freed[frees++] = handle;
}

uint32_t bcm_lin_to_phys(const void *linear)
{
    const uint8_t *p = linear;
    unsigned a = (unsigned)((p - &arena[0][0]) / sizeof(arena[0]));
    unsigned offset = (unsigned)((p - arena[a]) % sizeof(arena[0]));
    unsigned page = offset / 4096;
    assert(a < 64);
    if (a + 1 >= contiguous_from || forced_contiguous[a])
        return 0x00400000UL + a * 0x40000UL + offset;
    /* Pages in reverse order: physically scattered. */
    return 0x00400000UL + a * 0x40000UL + (47 - page) * 4096UL + offset % 4096;
}

static void dma_case(unsigned contiguous_on, unsigned fail)
{
    allocations = frees = contiguous_calls = contiguous_works = 0;
    memset(forced_contiguous, 0, sizeof(forced_contiguous));
    contiguous_from = contiguous_on;
    fail_at = fail;
    bcm_platform_dma_free();
    frees = 0;
}

static void test_dma(void)
{
    struct bcm_rings r;
    unsigned i;

    /* PageContig granted: one ring allocation, no retries. */
    dma_case(1000, 0);
    contiguous_works = 1;
    assert(bcm_platform_dma_alloc(&r) == 0);
    assert(contiguous_calls == 1 && allocations == 2 && frees == 0);
    assert(r.block_phys == bcm_lin_to_phys(arena[0]));
    assert(r.rx_buffer[0] == arena[1]);
    bcm_platform_dma_free();
    assert(frees == 2);

    /* PageContig refused, third ordinary block contiguous: two retries,
     * both freed. */
    dma_case(3, 0);
    assert(bcm_platform_dma_alloc(&r) == 0);
    assert(contiguous_calls == 1 && allocations == 4 && frees == 2);
    assert(freed[0] == 0x1000 && freed[1] == 0x1001);
    assert(r.block_phys == bcm_lin_to_phys(arena[2]));
    assert((uint8_t *)r.status == arena[2] + BCM_RING_STATUS_OFFSET);
    for (i = 0; i < BCM_RX_BUFFERS; ++i)
        assert(r.rx_phys[i] == bcm_lin_to_phys(r.rx_buffer[i]) &&
               r.rx_buffer[i] == arena[3] + i * 2048);
    for (i = 0; i < BCM_TX_BUFFERS; ++i)
        assert(r.tx_phys[i] == bcm_lin_to_phys(r.tx_buffer[i]) &&
               ((r.tx_phys[i] + BCM_BUFFER_SIZE - 1) >> 12) == r.tx_phys[i] >> 12);
    bcm_platform_dma_free();
    assert(frees == 4);

    /* Never contiguous: eight tries, all freed, nothing left to free. */
    dma_case(1000, 0);
    assert(bcm_platform_dma_alloc(&r) == -1);
    assert(allocations == 8 && frees == 8);
    bcm_platform_dma_free();
    assert(frees == 8);

    /* Buffer pages unavailable: the ring block is released. */
    dma_case(1, 2);
    assert(bcm_platform_dma_alloc(&r) == -1);
    assert(frees == 1 && freed[0] == 0x1000);
    bcm_platform_dma_free();
    assert(frees == 1);

    /* No memory at all. */
    dma_case(1, 1);
    assert(bcm_platform_dma_alloc(&r) == -1 && frees == 0);
}

int main(void)
{
    test_config();
    test_mmio();
    test_delay();
    test_dma();
    puts("Platform tests: config word/dword access, MMIO, delays with any PIT reload, DMA allocation passed");
    return 0;
}
