#include <assert.h>
#include <stdio.h>
#include "platform.h"

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

int main(void)
{
    test_config();
    test_mmio();
    test_delay();
    puts("Platform tests: config word/dword access, MMIO, delays with any PIT reload passed");
    return 0;
}
