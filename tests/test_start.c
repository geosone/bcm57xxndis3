#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bcm5722_hw.h"

/* Register file with a write log; engine enable bits read back as written
 * unless the engine is marked as hanging. */
static uint32_t regs[0x10000 / 4];
static unsigned log_offset[512], log_count;
static int buffer_manager_dead;
static unsigned stuck_engine;

static uint32_t read32(void *context, unsigned offset)
{
    (void)context;
    assert(offset < 0x10000 && !(offset & 3));
    if (offset == 0x4400 && buffer_manager_dead)
        return 0;
    if (offset == stuck_engine)
        return regs[offset / 4] | 2;
    return regs[offset / 4];
}

static void write32(void *context, unsigned offset, uint32_t value)
{
    (void)context;
    assert(offset < 0x10000 && !(offset & 3));
    regs[offset / 4] = value;
    if (log_count < 512)
        log_offset[log_count++] = offset;
}

static void delay_us(void *context, unsigned us)
{
    (void)context; (void)us;
}

static const struct bcm_hw_io io = {0, read32, write32, 0, 0, 0, delay_us};

static int position(unsigned offset)
{
    unsigned i;
    for (i = 0; i < log_count; ++i)
        if (log_offset[i] == offset)
            return (int)i;
    return -1;
}

static void setup(void)
{
    memset(regs, 0, sizeof(regs));
    log_count = 0;
    buffer_manager_dead = 0;
    stuck_engine = 0;
    regs[0x2018 / 4] = 0x00ffffffUL;
}

static const struct bcm_ring_addresses rings = {
    0x00120000UL, 0x00122000UL, 0x00126000UL, 0x0012a000UL, 64
};
static const uint8_t mac[6] = {0x00, 0x10, 0x18, 0xab, 0xcd, 0xef};

static void test_start(void)
{
    static const unsigned engines[] = {
        0x3000, 0x2000, 0x4c00, 0x4800, 0x2800, 0x1000, 0x1c00, 0x2c00,
        0x2400, 0x0c00, 0x1800, 0x1400
    };
    unsigned i;
    int last_setup;
    setup();
    assert(bcm5722_start(&io, &rings, mac, 0x00e04800UL, 0x01000002UL,
                         0x102) == BCM_HW_OK);
    /* Rings, status block, mailboxes and MAC address. */
    assert(regs[0x2450 / 4] == 0 && regs[0x2454 / 4] == rings.std);
    assert(regs[0x2458 / 4] == 0x02000000UL && regs[0x245c / 4] == 0x6000);
    assert(regs[0x8104 / 4] == rings.tx && regs[0x8108 / 4] == 0x02000000UL);
    assert(regs[0x810c / 4] == 0x4000);
    assert(regs[0x8204 / 4] == rings.ret && regs[0x8208 / 4] == 0x02000000UL);
    assert(regs[0x3c3c / 4] == rings.status && regs[0x3c38 / 4] == 0);
    assert(regs[0x26c / 4] == 64 && regs[0x304 / 4] == 0 && regs[0x284 / 4] == 0);
    assert(regs[0x410 / 4] == 0x0010 && regs[0x414 / 4] == 0x18abcdefUL);
    assert(regs[0x438 / 4] == ((0x00 + 0x10 + 0x18 + 0xab + 0xcd + 0xef) & 0x3ff));
    assert(regs[0x6800 / 4] == 0x04130034UL);
    assert(!(regs[0x2018 / 4] & 0x40000UL));
    assert(regs[0x400 / 4] == (0x00e04800UL | 0x9000));
    assert(regs[0x45c / 4] == 0x102 && regs[0x468 / 4] == 0x01000002UL);
    assert(regs[0x3c00 / 4] == 0x102);
    assert(regs[0x454 / 4] == 0x000c0000UL);   /* MDIO without auto poll */

    /* Every engine starts after the rings and the status block are set up. */
    last_setup = position(0x3c3c);
    if (position(0x26c) > last_setup) last_setup = position(0x26c);
    if (position(0x8204) > last_setup) last_setup = position(0x8204);
    assert(position(0x4400) < position(0x2454));   /* buffer manager first */
    for (i = 0; i < sizeof(engines) / sizeof(engines[0]); ++i) {
        assert(position(engines[i]) > last_setup);
        assert(regs[engines[i] / 4] & 2);
    }
    assert(position(0x468) > position(0x1400));    /* receive MAC last */
}

static void test_buffer_manager_timeout(void)
{
    setup();
    buffer_manager_dead = 1;
    assert(bcm5722_start(&io, &rings, mac, 0, 0, 0) ==
           BCM_HW_BUFFER_MANAGER_TIMEOUT);
    assert(position(0x2454) < 0 && position(0x4800) < 0 && position(0x468) < 0);
}

static void test_stop(void)
{
    uint8_t read[6];
    setup();
    assert(bcm5722_start(&io, &rings, mac, 0x00e04800UL, 0x01000002UL,
                         0x102) == BCM_HW_OK);
    log_count = 0;
    assert(bcm5722_stop(&io) == 0);
    assert(!(regs[0x468 / 4] & 2) && !(regs[0x4c00 / 4] & 2));
    assert(!(regs[0x400 / 4] & 0x200000UL) && (regs[0x400 / 4] & 0x400000UL));
    assert(position(0x468) < position(0x4800));   /* receive before DMA */
    assert(position(0x4800) < position(0x4c00));   /* read before write DMA */
    assert(log_offset[log_count - 1] == 0x5c00 && regs[0x5c00 / 4] == 0);

    stuck_engine = 0x1800;
    assert(bcm5722_stop(&io) == 1);

    bcm5722_read_mac(&io, read);
    assert(!memcmp(read, mac, 6));
}

static void test_filter_and_interrupts(void)
{
    uint32_t rx_mode = 0x01000002UL;
    unsigned i;
    setup();
    bcm5722_rx_filter(&io, &rx_mode, 1);
    for (i = 0; i < 4; ++i)
        assert(regs[(0x470 + 4 * i) / 4] == 0xffffffffUL);
    assert(rx_mode == 0x01000102UL && regs[0x468 / 4] == rx_mode);
    bcm5722_rx_filter(&io, &rx_mode, 0);
    assert(rx_mode == 0x01000002UL && regs[0x468 / 4] == rx_mode);

    regs[0x68 / 4] = 0x29a;
    bcm5722_enable_interrupts(&io, 1);
    assert(regs[0x68 / 4] == 0x298);
    bcm5722_enable_interrupts(&io, 0);
    assert(regs[0x68 / 4] == 0x29a);
}

int main(void)
{
    test_start();
    test_buffer_manager_timeout();
    test_stop();
    test_filter_and_interrupts();
    puts("Start tests: rings, status block, MAC address, engine order, buffer manager timeout, stop sequence, RX filter, interrupt mask passed");
    return 0;
}
