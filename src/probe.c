/* Ring-0 preflight, then the MAC registration. The probe itself only reads
 * the card, unless [BCM57DBG] ResetTest=1 in SYSTEM.INI asks for one reset
 * (and PHY setup); ResetTest=2 adds a DMA loopback test. */
#include "pci.h"
#include "ndis3.h"
#include "platform.h"
#include "bcm5722_phy.h"
#include "bcm5722_ring.h"

extern uint32_t bcm_ndis_version(void);
extern uint32_t bcm_vmm_version(void);
extern void bcm_debug_char(unsigned character);
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

static const char *const reset_results[] = {
    "ok", "wrong device", "PCI memory/bus master disabled",
    "NVRAM arbitration timeout", "bootcode timeout", "buffer manager timeout"
};

static void log_phy(const char *step, enum bcm_phy_result result)
{
    bcm_log_text("BCM57DBG: ResetTest: PHY ");
    bcm_log_text(step);
    bcm_log_text(result == BCM_PHY_OK ? " ok\r\n" :
                 result == BCM_PHY_BUSY ? " MI busy\r\n" : " reset timeout\r\n");
}

/* After the chip reset: PHY ID, PHY reset, autonegotiation and up to 5 s of
 * link polling. Only MAC mode, lengths and flow bits are written; the MAC
 * engines stay disabled. */
static void dma_test(const struct bcm_hw_io *, struct bcm_mac_modes *);

static void phy_test(const struct bcm_hw_io *io, int dma)
{
    struct bcm_mac_modes modes = {0, 0, 0};
    struct bcm_link link;
    enum bcm_phy_result result;
    uint32_t id;
    unsigned i;

    result = bcm5722_phy_id(io, &id);
    log_phy("ID", result);
    if (result != BCM_PHY_OK)
        return;
    bcm_log_text("BCM57DBG: ResetTest: PHY ID=");
    bcm_log_hex(id);
    bcm_log_text("\r\n");
    if ((result = bcm5722_phy_reset(io)) != BCM_PHY_OK) {
        log_phy("reset", result);
        return;
    }
    if ((result = bcm5722_phy_autoneg(io)) != BCM_PHY_OK) {
        log_phy("autonegotiation", result);
        return;
    }
    for (i = 0; i < 50; ++i) {
        if ((result = bcm5722_phy_link(io, &modes, &link)) != BCM_PHY_OK) {
            log_phy("link", result);
            return;
        }
        if (link.up)
            break;
        bcm_platform_delay_us(100000);
    }
    bcm_log_text("BCM57DBG: ResetTest: link ");
    if (!link.up) {
        bcm_log_text("down after 5 s\r\n");
        if (dma)
            dma_test(io, &modes);
        return;
    }
    bcm_log_text("up speed=");
    bcm_log_hex(link.speed);
    bcm_log_text(link.full_duplex ? " full" : " half");
    bcm_log_text(" duplex flow=");
    bcm_log_hex(link.flow);
    bcm_log_text("\r\n");
    if (dma)
        dma_test(io, &modes);
}

static struct bcm_rings rings;
static unsigned loop_received, loop_matched;
static uint8_t loop_frame[128];

static void loop_deliver(void *context, const uint8_t *frame, unsigned length)
{
    unsigned i;
    (void)context;
    ++loop_received;
    if (length != sizeof(loop_frame))
        return;
    for (i = 0; i < length; ++i)
        if (frame[i] != loop_frame[i])
            return;
    ++loop_matched;
}

/* ResetTest=2: DMA rings and engines with the MAC in internal loopback;
 * 8 frames to the station address must come back unchanged. */
static void dma_test(const struct bcm_hw_io *io, struct bcm_mac_modes *modes)
{
    struct bcm_ring_addresses addresses;
    enum bcm_hw_result result;
    uint8_t mac[6];
    unsigned i, sent = 0, completed = 0, stuck;

    if (bcm_platform_dma_alloc(&rings)) {
        bcm_log_text("BCM57DBG: DMA test: no DMA memory\r\n");
        return;
    }
    bcm_rings_init(&rings);
    addresses.tx = rings.block_phys + BCM_RING_TX_OFFSET;
    addresses.std = rings.block_phys + BCM_RING_STD_OFFSET;
    addresses.ret = rings.block_phys + BCM_RING_RETURN_OFFSET;
    addresses.status = rings.block_phys + BCM_RING_STATUS_OFFSET;
    addresses.std_posted = rings.rx_producer;
    bcm5722_read_mac(io, mac);
    bcm_log_text("BCM57DBG: DMA test: rings phys=");
    bcm_log_hex(rings.block_phys);
    bcm_log_text("\r\n");
    /* TDE/RDE/FHDE, statistics, internal loopback (MAC_MODE bit 4). */
    result = bcm5722_start(io, &addresses, mac,
        modes->mac_mode | 0x00e04810UL, 0x01000002UL, 0x102);
    if (result != BCM_HW_OK) {
        bcm_log_text("BCM57DBG: DMA test: ");
        bcm_log_text(reset_results[result]);
        bcm_log_text("\r\n");
        bcm_platform_dma_free();
        return;
    }
    for (i = 0; i < sizeof(loop_frame); ++i)
        loop_frame[i] = (uint8_t)i;
    for (i = 0; i < 6; ++i)
        loop_frame[i] = loop_frame[6 + i] = mac[i];
    loop_frame[12] = 0x88;              /* local experimental EtherType */
    loop_frame[13] = 0xb5;
    for (i = 0; i < 200 && loop_received < 8; ++i) {
        if (sent < 8 && bcm_tx_send(io, &rings, loop_frame, sizeof(loop_frame)) == 0)
            ++sent;
        completed += bcm_tx_complete(&rings);
        bcm_rx_poll(io, &rings, loop_deliver, 0);
        bcm_platform_delay_us(10000);
    }
    stuck = bcm5722_stop(io);
    bcm_log_text("BCM57DBG: DMA test: sent=");
    bcm_log_hex(sent);
    bcm_log_text(" completed=");
    bcm_log_hex(completed);
    bcm_log_text(" received=");
    bcm_log_hex(loop_received);
    bcm_log_text(" matched=");
    bcm_log_hex(loop_matched);
    bcm_log_text(" rx-errors=");
    bcm_log_hex(rings.rx_errors + rings.rx_bad_index);
    bcm_log_text(" status=");
    bcm_log_hex(rings.status->status);
    bcm_log_text(" engines-stuck=");
    bcm_log_hex(stuck);
    bcm_log_text(loop_matched == 8 ? " PASS\r\n" : " FAIL\r\n");
    bcm_platform_dma_free();
}

/* Opt-in: one chip reset with bus mastering enabled; MAC engines stay off. */
static void reset_test(const struct bcm_pci_device *card,
                       volatile uint32_t *mmio, int dma)
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
    bcm_log_text(result <= BCM_HW_BUFFER_MANAGER_TIMEOUT ?
                 reset_results[result] : "?");
    bcm_log_text(" host-control=");
    bcm_log_hex(mmio[0x68 / 4]);
    bcm_log_text(" MAC-high=");
    bcm_log_hex(mmio[0x410 / 4]);
    bcm_log_text(" MAC-low=");
    bcm_log_hex(mmio[0x414 / 4]);
    bcm_log_text("\r\n");
    if (result == BCM_HW_OK)
        phy_test(&io, dma);
}

/* Read-only description of the card; with ResetTest, the diagnostics. */
static void probe_card(int reset)
{
    struct bcm_pci_device card;
    uint32_t physical, pci_host, mmio_host;
    volatile uint32_t *mmio;

    if (!bcm_find_5722(bcm_platform_pci_read, 0, &card)) {
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
    pci_host = bcm_platform_pci_read(0, card.bus, card.device, card.function, 0x68);
    bcm_log_text("BCM57DBG: PCI host-control/ASIC=");
    bcm_log_hex(pci_host);
    bcm_log_text("\r\n");
    if (!(card.command_status & 2) ||
            (bcm_platform_pci_read(0, card.bus, card.device, card.function, 0x4c) & 3)) {
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
    reset_test(&card, mmio, reset >= 2);
}

/* Diagnostics first: AddAdapter, called from NdisRegisterMac, takes the
 * card into service. */
void bcm_probe_init(void)
{
    uint32_t version;
    bcm_ndis_status status;
    int reset;

    bcm_log_text("BCM57DBG: init\r\n");
    bcm_log_text("BCM57DBG: VMM version raw=");
    bcm_log_hex(bcm_vmm_version());
    bcm_log_text("\r\n");
    version = bcm_ndis_version();
    bcm_log_text("BCM57DBG: NdisGetVersion raw=");
    bcm_log_hex(version);
    bcm_log_text(version ? "\r\n" : " (unavailable or zero)\r\n");
    reset = bcm_profile_int("ResetTest", 0);
    bcm_log_text("BCM57DBG: ResetTest=");
    bcm_log_hex((uint32_t)reset);
    bcm_log_text("\r\n");
    probe_card(reset);
    if (version) {
        status = bcm_mac_start();
        bcm_log_text("BCM57DBG: NdisRegisterMac status=");
        bcm_log_hex(status);
        bcm_log_text("\r\n");
    }
}
