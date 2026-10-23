#include "nic.h"
#include "adapter.h"
#include "pci.h"
#include "platform.h"
#include "bcm5722_phy.h"
#include "bcm5722_ring.h"

/* MAC engines as in the DMA test, without internal loopback. */
#define MAC_MODE_DEFAULT 0x00e04800UL
#define RX_MODE_DEFAULT 0x01000002UL
#define TX_MODE_DEFAULT 0x00000102UL
#define LINK_POLL_MS 1000
#define ISA_INTERFACE 1                 /* what WfW PCI MACs register */

static struct {
    struct bcm_pci_device card;
    struct bcm_platform platform;
    struct bcm_hw_io io;
    struct bcm_rings rings;
    struct bcm_mac_modes modes;
    struct bcm_link link;
    struct bcm_adapter adapter;
    struct bcm_ndis_interrupt irq_object;
    struct bcm_ndis_timer link_timer, link_event;
    bcm_ndis_handle ndis_adapter;
    uint8_t mac[6];
    unsigned rx_errors;                 /* ring errors already counted */
    int running, registered;
} nic;

static const char *const hw_results[] = {
    "ok", "wrong device", "PCI memory/bus master disabled",
    "NVRAM arbitration timeout", "bootcode timeout", "buffer manager timeout"
};

static void log_status(const char *text, uint32_t value)
{
    if (text[0] != ' ')
        bcm_log_text("BCM57DBG: ");
    bcm_log_text(text);
    bcm_log_hex(value);
    bcm_log_text("\r\n");
}

/* Called under bcm_lock by the adapter. */
static int nic_transmit(void *context, const uint8_t *frame, unsigned length)
{
    (void)context;
    if (!nic.running)
        return -1;
    return bcm_tx_send(&nic.io, &nic.rings, frame, length);
}

static void nic_set_promiscuous(void *context, int on)
{
    (void)context;
    if (nic.running)
        bcm5722_rx_filter(&nic.io, &nic.modes.rx_mode, on);
}

/* Chip reset, PHY setup, fresh rings, engines on. Interrupts stay masked. */
static enum bcm_hw_result bring_up(void)
{
    struct bcm_ring_addresses addresses;
    enum bcm_hw_result result = bcm5722_reset(&nic.io);
    if (result != BCM_HW_OK)
        return result;
    bcm5722_read_mac(&nic.io, nic.mac);
    /* Without a working PHY the MAC still starts; link polling reports. */
    if (bcm5722_phy_reset(&nic.io) != BCM_PHY_OK ||
            bcm5722_phy_autoneg(&nic.io) != BCM_PHY_OK)
        bcm_log_text("BCM57DBG: PHY setup failed\r\n");
    bcm_rings_init(&nic.rings);
    nic.rx_errors = 0;
    nic.modes.mac_mode = MAC_MODE_DEFAULT;
    nic.modes.rx_mode = RX_MODE_DEFAULT;
    nic.modes.tx_mode = TX_MODE_DEFAULT;
    if (bcm5722_phy_link(&nic.io, &nic.modes, &nic.link) != BCM_PHY_OK)
        nic.link.up = 0;
    addresses.tx = nic.rings.block_phys + BCM_RING_TX_OFFSET;
    addresses.std = nic.rings.block_phys + BCM_RING_STD_OFFSET;
    addresses.ret = nic.rings.block_phys + BCM_RING_RETURN_OFFSET;
    addresses.status = nic.rings.block_phys + BCM_RING_STATUS_OFFSET;
    addresses.std_posted = nic.rings.rx_producer;
    result = bcm5722_start(&nic.io, &addresses, nic.mac, nic.modes.mac_mode,
                           nic.modes.rx_mode, nic.modes.tx_mode);
    if (result != BCM_HW_OK) {
        bcm5722_stop(&nic.io);
        return result;
    }
    bcm5722_rx_filter(&nic.io, &nic.modes.rx_mode, nic.adapter.promiscuous);
    return BCM_HW_OK;
}

/* Unmasks the mailbox and INTA; the chip interrupts on its next update. */
static void enable_interrupts(void)
{
    bcm_irq_done(&nic.io, &nic.rings, 0);
    bcm5722_enable_interrupts(&nic.io, 1);
}

/* Protocol reset: the adapter refuses sends meanwhile, and no DPC runs
 * during this VM call, so the rings can be rebuilt without bcm_lock. */
static int nic_reset(void *context)
{
    enum bcm_hw_result result;
    (void)context;
    if (!nic.running)
        return -1;
    bcm5722_enable_interrupts(&nic.io, 0);
    bcm5722_stop(&nic.io);
    result = bring_up();
    if (result != BCM_HW_OK) {
        bcm_log_text("BCM57DBG: reset: ");
        bcm_log_text(hw_results[result]);
        bcm_log_text("\r\n");
        return -1;
    }
    nic.adapter.link_speed = nic.link.up ? nic.link.speed : 0;
    enable_interrupts();
    return 0;
}

static const struct bcm_adapter_hw hw = {
    0, nic_transmit, nic_set_promiscuous, nic_reset,
    BCM_TX_BUFFERS, BCM_RX_BUFFERS, BCM_BUFFER_SIZE
};

/* Interrupt time, interrupts disabled. */
static int isr(void *context)
{
    (void)context;
    return nic.running && bcm_irq_claim(&nic.io, &nic.rings);
}

static void deliver(void *context, const uint8_t *frame, unsigned length)
{
    bcm_adapter_receive(context, frame, length);
}

/* Global event, after isr returned 1: send completions, receives, then the
 * status tag is acknowledged and the interrupt unmasked. */
static void dpc(void *system1, void *context, void *system2, void *system3)
{
    uint32_t tag, flags;
    unsigned freed, errors;
    (void)system1; (void)context; (void)system2; (void)system3;
    if (!nic.running)
        return;
    tag = bcm_irq_tag(&nic.rings);
    flags = bcm_lock();
    freed = bcm_tx_complete(&nic.rings);
    bcm_unlock(flags);
    if (freed)
        bcm_adapter_send_space(&nic.adapter);
    if (bcm_rx_poll(&nic.io, &nic.rings, deliver, &nic.adapter))
        bcm_adapter_receive_complete(&nic.adapter);
    errors = nic.rings.rx_errors + nic.rings.rx_bad_index;
    nic.adapter.rcv_error += errors - nic.rx_errors;
    nic.rx_errors = errors;
    bcm_irq_done(&nic.io, &nic.rings, tag);
}

static void log_link(void)
{
    bcm_log_text("BCM57DBG: link ");
    if (!nic.link.up) {
        bcm_log_text("down\r\n");
        return;
    }
    bcm_log_text("up speed=");
    bcm_log_hex(nic.link.speed);
    bcm_log_text(nic.link.full_duplex ? " full duplex\r\n" : " half duplex\r\n");
}

/* Time-outs run at interrupt time; MDIO polling is left to an event. */
static void link_timeout(void *system, void *context)
{
    (void)system; (void)context;
    bcm_ndis_set_timer(&nic.link_event, 0);
}

static void link_poll(void *system, void *context)
{
    unsigned speed;
    (void)system; (void)context;
    if (!nic.running)
        return;
    if (bcm5722_phy_link(&nic.io, &nic.modes, &nic.link) == BCM_PHY_OK) {
        speed = nic.link.up ? nic.link.speed : 0;
        if (speed != nic.adapter.link_speed) {
            nic.adapter.link_speed = speed;
            log_link();
        }
    }
    bcm_ndis_set_timer(&nic.link_timer, LINK_POLL_MS);
}

/* PCI function in D0 with a usable 64 KiB BAR0, mapped. */
static int map_card(void)
{
    uint32_t physical;
    const struct bcm_pci_device *card = &nic.card;
    if (bcm_platform_pci_read(0, card->bus, card->device, card->function,
                              0x4c) & 3) {
        bcm_log_text("BCM57DBG: AddAdapter: device not in D0\r\n");
        return 0;
    }
    if (!bcm_pci_mmio_base(card->bar0, card->bar1, &physical)) {
        bcm_log_text("BCM57DBG: AddAdapter: unsupported or unassigned BAR0\r\n");
        return 0;
    }
    nic.platform.bus = card->bus;
    nic.platform.device = card->device;
    nic.platform.function = card->function;
    nic.platform.mmio = bcm_map_mmio(physical);
    if (!nic.platform.mmio || (uint32_t)nic.platform.mmio == 0xffffffffUL) {
        bcm_log_text("BCM57DBG: AddAdapter: MMIO mapping failed\r\n");
        return 0;
    }
    bcm_platform_io(&nic.platform, &nic.io);
    return 1;
}

bcm_ndis_status bcm_nic_add(bcm_ndis_handle mac, bcm_ndis_handle configuration,
                            struct bcm_ndis_string *name)
{
    struct bcm_ndis_adapter_information information;
    bcm_ndis_status status;
    enum bcm_hw_result result;
    uint16_t command;
    uint8_t *p;
    unsigned i;

    if (nic.registered) {
        bcm_log_text("BCM57DBG: AddAdapter: only one adapter is supported\r\n");
        return BCM_NDIS_FAILURE;
    }
    if (!bcm_find_5722(bcm_platform_pci_read, 0, &nic.card)) {
        bcm_log_text("BCM57DBG: AddAdapter: BCM5722 not found\r\n");
        return BCM_NDIS_ADAPTER_NOT_FOUND;
    }
    if (!nic.card.irq || nic.card.irq > 15) {
        log_status("AddAdapter: no usable IRQ, line=", nic.card.irq);
        return BCM_NDIS_ADAPTER_NOT_FOUND;
    }
    if (!map_card())
        return BCM_NDIS_ADAPTER_NOT_FOUND;
    command = nic.io.pci_read16(nic.io.context, 4);
    if ((command & 6) != 6)             /* memory decode, bus master */
        nic.io.pci_write16(nic.io.context, 4, (uint16_t)(command | 6));
    if (bcm_platform_dma_alloc(&nic.rings)) {
        bcm_log_text("BCM57DBG: AddAdapter: no DMA memory\r\n");
        return BCM_NDIS_RESOURCES;
    }
    nic.adapter.promiscuous = 0;
    result = bring_up();
    if (result != BCM_HW_OK) {
        bcm_log_text("BCM57DBG: AddAdapter: ");
        bcm_log_text(hw_results[result]);
        bcm_log_text("\r\n");
        bcm_platform_dma_free();
        return BCM_NDIS_ADAPTER_NOT_FOUND;
    }
    bcm_adapter_init(&nic.adapter, &hw, nic.mac);
    nic.adapter.link_speed = nic.link.up ? nic.link.speed : 0;

    /* As the WfW PCI bus-master MACs: ISA type, no DMA channel, no map
     * registers. MMIO needs no I/O port descriptor. */
    p = (uint8_t *)&information;
    for (i = 0; i < sizeof(information); ++i)
        p[i] = 0;
    information.adapter_type = ISA_INTERFACE;
    status = bcm_ndis_register_adapter(&nic.ndis_adapter, mac, &nic.adapter,
                                       configuration, name, &information);
    if (status != BCM_NDIS_SUCCESS) {
        log_status("NdisRegisterAdapter failed: ", status);
        bcm5722_stop(&nic.io);
        bcm_platform_dma_free();
        return status;
    }
    nic.running = 1;
    /* Shared, as PCI INTA may be; WfW ignores the mode. */
    bcm_ndis_initialize_interrupt(&status, &nic.irq_object, nic.ndis_adapter,
        isr, &nic, dpc, nic.card.irq, nic.card.irq, 1, 0);
    if (status != BCM_NDIS_SUCCESS) {
        log_status("NdisInitializeInterrupt failed: ", status);
        nic.running = 0;
        bcm5722_stop(&nic.io);
        bcm_ndis_deregister_adapter(nic.ndis_adapter);
        bcm_platform_dma_free();
        return status;
    }
    nic.registered = 1;
    enable_interrupts();
    nic.link_timer.handle = nic.link_event.handle = 0;
    nic.link_timer.function = link_timeout;
    nic.link_event.function = link_poll;
    bcm_ndis_set_timer(&nic.link_timer, LINK_POLL_MS);

    bcm_log_text("BCM57DBG: adapter ready, MAC-high=");
    bcm_log_hex((uint32_t)nic.mac[0] << 8 | nic.mac[1]);
    bcm_log_text(" MAC-low=");
    bcm_log_hex((uint32_t)nic.mac[2] << 24 | (uint32_t)nic.mac[3] << 16 |
                (uint32_t)nic.mac[4] << 8 | nic.mac[5]);
    log_status(" IRQ=", nic.card.irq);
    log_link();
    return BCM_NDIS_SUCCESS;
}

void bcm_nic_shutdown(void)
{
    if (!nic.running)
        return;
    nic.running = 0;
    bcm5722_enable_interrupts(&nic.io, 0);
    bcm5722_stop(&nic.io);
}

void bcm_nic_remove(void)
{
    uint8_t cancelled;
    bcm_nic_shutdown();
    if (!nic.registered)
        return;
    nic.registered = 0;
    /* Only the time-out; a 0 ms timer is an event and cannot be cancelled,
     * and link_poll ignores it once running is clear. */
    bcm_ndis_cancel_timer(&nic.link_timer, &cancelled);
    bcm_ndis_remove_interrupt(&nic.irq_object);
    bcm_ndis_deregister_adapter(nic.ndis_adapter);
    bcm_platform_dma_free();
}
