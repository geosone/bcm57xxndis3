/* Ring-0 preflight and MAC callback probe. No NIC register writes or DMA,
 * unless [BCM57DBG] ResetTest=1 in SYSTEM.INI asks for one reset. */
#include "pci.h"
#include "ndis3.h"
#include "platform.h"

extern uint32_t bcm_ndis_version(void);
extern uint32_t bcm_vmm_version(void);
extern uint32_t bcm_config_read(uint32_t address);
extern void bcm_debug_char(unsigned character);
extern void *bcm_map_mmio(uint32_t physical);
extern int bcm_profile_int(const char *key, int def);

void bcm_log_text(const char *text)
{
    while (*text)
        bcm_debug_char((unsigned char)*text++);
}

void bcm_log_hex(uint32_t value)
{
    static const char digits[] = "0123456789abcdef";
    int shift;
    for (shift = 28; shift >= 0; shift -= 4)
        bcm_debug_char(digits[(value >> shift) & 15]);
}

static uint32_t read_config(void *context, unsigned bus, unsigned device,
                            unsigned function, unsigned offset)
{
    (void)context;
    return bcm_config_read(0x80000000UL | ((uint32_t)bus << 16) |
                           ((uint32_t)device << 11) |
                           ((uint32_t)function << 8) | (offset & 0xfc));
}

static const char *const reset_results[] = {
    "ok", "wrong device", "PCI memory/bus master disabled",
    "NVRAM arbitration timeout", "bootcode timeout"
};

/* Opt-in: one chip reset with bus mastering enabled; MAC engines stay off. */
static void reset_test(const struct bcm_pci_device *card,
                       volatile uint32_t *mmio)
{
    struct bcm_platform platform;
    struct bcm_hw_io io;
    enum bcm_hw_result result;
    uint16_t command;

    platform.bus = card->bus;
    platform.device = card->device;
    platform.function = card->function;
    platform.mmio = mmio;
    bcm_platform_io(&platform, &io);
    command = io.pci_read16(io.context, 4);
    if ((command & 6) != 6) {
        io.pci_write16(io.context, 4, (uint16_t)(command | 6));
        bcm_log_text("BCM57DBG: ResetTest: PCI command ");
        bcm_log_hex(command);
        bcm_log_text(" -> ");
        bcm_log_hex(io.pci_read16(io.context, 4));
        bcm_log_text("\r\n");
    }
    result = bcm5722_reset(&io);
    bcm_log_text("BCM57DBG: ResetTest: ");
    bcm_log_text(result <= BCM_HW_FIRMWARE_TIMEOUT ? reset_results[result] : "?");
    bcm_log_text(" host-control=");
    bcm_log_hex(mmio[0x68 / 4]);
    bcm_log_text(" MAC-high=");
    bcm_log_hex(mmio[0x410 / 4]);
    bcm_log_text(" MAC-low=");
    bcm_log_hex(mmio[0x414 / 4]);
    bcm_log_text("\r\n");
}

void bcm_probe_init(void)
{
    struct bcm_pci_device card;
    uint32_t version;
    bcm_ndis_status status;
    uint32_t physical, pci_host, mmio_host;
    volatile uint32_t *mmio;
    int reset;

    bcm_log_text("BCM57DBG: diagnostic init; no network support\r\n");
    bcm_log_text("BCM57DBG: VMM version raw=");
    bcm_log_hex(bcm_vmm_version());
    bcm_log_text("\r\n");
    version = bcm_ndis_version();
    bcm_log_text("BCM57DBG: NdisGetVersion raw=");
    bcm_log_hex(version);
    bcm_log_text(version ? "\r\n" : " (unavailable or zero)\r\n");
    if (version) {
        status = bcm_mac_start();
        bcm_log_text("BCM57DBG: NdisRegisterMac status=");
        bcm_log_hex(status);
        bcm_log_text("\r\n");
    }
    reset = bcm_profile_int("ResetTest", 0);
    bcm_log_text("BCM57DBG: ResetTest=");
    bcm_log_hex((uint32_t)reset);
    bcm_log_text("\r\n");
    if (!bcm_find_5722(read_config, 0, &card)) {
        bcm_log_text("BCM57DBG: BCM5722 14e4:165a not found\r\n");
        return;
    }
    bcm_log_text("BCM57DBG: BDF=");
    bcm_log_hex((card.bus << 8) | (card.device << 3) | card.function);
    bcm_log_text(" class/revision=");
    bcm_log_hex(card.class_revision);
    bcm_log_text(" BAR0=");
    bcm_log_hex(card.bar0);
    bcm_log_text(" BAR1=");
    bcm_log_hex(card.bar1);
    bcm_log_text(" subsystem=");
    bcm_log_hex(card.subsystem);
    bcm_log_text(" IRQ=");
    bcm_log_hex(card.irq);
    bcm_log_text(" pin=");
    bcm_log_hex(card.interrupt_pin);
    bcm_log_text(" command/status=");
    bcm_log_hex(card.command_status);
    bcm_log_text("\r\n");
    pci_host = read_config(0, card.bus, card.device, card.function, 0x68);
    bcm_log_text("BCM57DBG: PCI host-control/ASIC=");
    bcm_log_hex(pci_host);
    bcm_log_text("\r\n");
    if (!(card.command_status & 2) ||
            (read_config(0, card.bus, card.device, card.function, 0x4c) & 3)) {
        bcm_log_text("BCM57DBG: MMIO skipped: memory decode disabled or device not in D0\r\n");
        return;
    }
    if (!bcm_pci_mmio_base(card.bar0, card.bar1, &physical)) {
        bcm_log_text("BCM57DBG: MMIO skipped: unsupported/unassigned BAR0\r\n");
        return;
    }
    mmio = bcm_map_mmio(physical);
    if (!mmio || (uint32_t)mmio == 0xffffffffUL) {
        bcm_log_text("BCM57DBG: MMIO mapping failed\r\n");
        return;
    }
    mmio_host = mmio[0x68 / 4];
    bcm_log_text("BCM57DBG: MMIO host-control/ASIC=");
    bcm_log_hex(mmio_host);
    if ((mmio_host & 0xffff0000UL) != (pci_host & 0xffff0000UL) ||
            mmio_host == 0xffffffffUL) {
        bcm_log_text(" (mismatch; stopping MMIO probe)\r\n");
        return;
    }
    bcm_log_text(" MAC-high=");
    bcm_log_hex(mmio[0x410 / 4]);
    bcm_log_text(" MAC-low=");
    bcm_log_hex(mmio[0x414 / 4]);
    bcm_log_text(" mode=");
    bcm_log_hex(mmio[0x6800 / 4]);
    bcm_log_text("\r\n");
    if (!reset) {
        bcm_log_text("BCM57DBG: probe complete; NIC configuration unchanged\r\n");
        return;
    }
    reset_test(&card, mmio);
}
