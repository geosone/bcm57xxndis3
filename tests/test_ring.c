#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bcm5722_ring.h"

/* Host memory standing in for DMA memory; physical address = offset + base. */
#define PHYS_BASE 0x00100000UL
static uint8_t memory[BCM_RING_BLOCK_SIZE + (BCM_RX_BUFFERS + BCM_TX_BUFFERS) * 2048];
static uint32_t mailbox_tx, mailbox_std, mailbox_ret;
static unsigned mailbox_writes;

static uint8_t *lin(uint32_t phys)
{
    assert(phys >= PHYS_BASE && phys - PHYS_BASE < sizeof(memory));
    return memory + (phys - PHYS_BASE);
}

/* Interrupt line: asserted while the chip has an unacknowledged update and
 * the mailbox does not mask it; PCI state bit 1 reads "not active". */
static uint32_t mailbox_irq = 1;
static int chip_wants_irq;

static int inta(void)
{
    return chip_wants_irq && !(mailbox_irq & 1);
}

static uint32_t read32(void *context, unsigned offset)
{
    (void)context;
    if (offset == 0x70)
        return inta() ? 0 : 2;
    assert(offset == 0x204);
    return mailbox_irq;
}

static void write32(void *context, unsigned offset, uint32_t value)
{
    (void)context;
    if (offset == 0x204) {
        mailbox_irq = value;
        return;
    }
    assert(value < BCM_RING_ENTRIES);
    ++mailbox_writes;
    if (offset == 0x304) mailbox_tx = value;
    else if (offset == 0x26c) mailbox_std = value;
    else if (offset == 0x284) mailbox_ret = value;
    else assert(0);
}

static const struct bcm_hw_io io = {0, read32, write32, 0, 0, 0, 0};
static struct bcm_rings r;

/* The chip: free-running counters of what it has consumed and produced. */
static unsigned chip_tx, chip_std, chip_ret;
static uint8_t wire[BCM_FRAME_MAX + 4];
static unsigned wire_length;

static void setup(void)
{
    unsigned i;
    memset(memory, 0x5a, sizeof(memory));
    memset(&r, 0, sizeof(r));
    bcm_rings_setup(&r, memory, PHYS_BASE);
    for (i = 0; i < BCM_RX_BUFFERS; ++i) {
        r.rx_buffer[i] = memory + BCM_RING_BLOCK_SIZE + i * 2048;
        r.rx_phys[i] = PHYS_BASE + BCM_RING_BLOCK_SIZE + i * 2048;
    }
    for (i = 0; i < BCM_TX_BUFFERS; ++i) {
        r.tx_buffer[i] = memory + BCM_RING_BLOCK_SIZE + (BCM_RX_BUFFERS + i) * 2048;
        r.tx_phys[i] = PHYS_BASE + BCM_RING_BLOCK_SIZE + (BCM_RX_BUFFERS + i) * 2048;
    }
    bcm_rings_init(&r);
    mailbox_tx = mailbox_ret = 0;
    mailbox_std = r.rx_producer % BCM_RING_ENTRIES;
    chip_tx = chip_std = chip_ret = 0;
}

/* Sends one frame from the send ring onto the wire; 0 if none is pending. */
static int chip_transmit(void)
{
    volatile struct bcm_tx_descriptor *d;
    if ((chip_tx & 511) == mailbox_tx)
        return 0;
    d = &r.tx[chip_tx & 511];
    assert(d->flags == BCM_TXD_END && d->address_high == 0);
    assert(d->length >= BCM_FRAME_MIN && d->length <= BCM_FRAME_MAX);
    memcpy(wire, lin(d->address_low), d->length);
    wire_length = d->length;
    ++chip_tx;
    r.status->tx_consumer = (uint16_t)(chip_tx & 511);
    return 1;
}

/* Receives the wire frame into the next posted buffer; 0 if none is posted. */
static int chip_receive(const uint8_t *frame, unsigned length, uint16_t flags)
{
    volatile struct bcm_rx_descriptor *s, *ret;
    if ((chip_std & 511) == mailbox_std)
        return 0;
    s = &r.std[chip_std & 511];
    assert(s->length == BCM_BUFFER_SIZE && s->flags == BCM_RXD_END);
    memcpy(lin(s->address_low), frame, length);
    memcpy(lin(s->address_low) + length, "\xde\xad\xbe\xef", 4);   /* CRC */
    ret = &r.ret[chip_ret & 511];
    ret->address_low = s->address_low;
    ret->index = s->index;
    ret->length = (uint16_t)(length + 4);
    ret->flags = BCM_RXD_END | flags;
    ++chip_std;
    ++chip_ret;
    r.status->return1_producer = (uint16_t)(chip_ret & 511);
    return 1;
}

static uint8_t received[BCM_FRAME_MAX];
static unsigned received_length, received_count;

static void deliver(void *context, const uint8_t *frame, unsigned length)
{
    assert(context == &r);
    memcpy(received, frame, length);
    received_length = length;
    ++received_count;
}

static void test_init(void)
{
    unsigned i;
    setup();
    assert(r.rx_producer == BCM_RX_BUFFERS);
    for (i = 0; i < BCM_RING_ENTRIES; ++i) {
        assert(r.std[i].address_low == r.rx_phys[i % BCM_RX_BUFFERS]);
        assert(r.std[i].index == i % BCM_RX_BUFFERS);
        assert(r.tx[i].address_low == 0 && r.ret[i].length == 0);
    }
    assert(r.status->tx_consumer == 0 && r.status->return1_producer == 0);
    /* Status block fields at the offsets the chip writes. */
    assert((uint8_t *)&r.status->std_consumer - (uint8_t *)r.status == 10);
    assert((uint8_t *)&r.status->return1_producer - (uint8_t *)r.status == 16);
    assert((uint8_t *)&r.status->tx_consumer - (uint8_t *)r.status == 18);
}

/* Over 2000 frames: every ring index wraps several times. */
static void test_loopback(void)
{
    uint8_t frame[BCM_FRAME_MAX];
    unsigned n, i, length, expect;
    setup();
    srand(1);
    for (n = 0; n < 2000; ++n) {
        length = 14 + (unsigned)rand() % (BCM_FRAME_MAX - 13);
        for (i = 0; i < length; ++i)
            frame[i] = (uint8_t)rand();
        assert(bcm_tx_send(&io, &r, frame, length) == 0);
        assert(mailbox_tx == r.tx_producer % 512);
        assert(chip_transmit());
        assert(bcm_tx_complete(&r) == 1);
        expect = length < BCM_FRAME_MIN ? BCM_FRAME_MIN : length;
        assert(wire_length == expect);
        assert(chip_receive(wire, wire_length, 0));
        assert(bcm_rx_poll(&io, &r, deliver, &r) == 1);
        assert(received_length == expect);
        assert(!memcmp(received, frame, length));
        for (i = length; i < expect; ++i)
            assert(received[i] == 0);
        assert(mailbox_ret == chip_ret % 512);
        assert(mailbox_std == (chip_std + BCM_RX_BUFFERS) % 512);
    }
    assert(received_count == 2000 && r.rx_errors == 0);
}

static void test_tx_full(void)
{
    uint8_t frame[64] = {0};
    unsigned i;
    setup();
    for (i = 0; i < BCM_TX_BUFFERS; ++i)
        assert(bcm_tx_send(&io, &r, frame, sizeof(frame)) == 0);
    assert(bcm_tx_send(&io, &r, frame, sizeof(frame)) == -1);
    assert(bcm_tx_send(&io, &r, frame, 13) == -1);
    assert(bcm_tx_send(&io, &r, frame, BCM_FRAME_MAX + 1) == -1);
    /* A consumer index ahead of the producer is not trusted. */
    r.status->tx_consumer = 100;
    assert(bcm_tx_complete(&r) == 0);
    assert(chip_transmit() && chip_transmit());
    assert(bcm_tx_complete(&r) == 2);
    assert(bcm_tx_send(&io, &r, frame, sizeof(frame)) == 0);
    assert(bcm_tx_complete(&r) == 0);
}

/* The chip only ever writes posted buffers, and a buffer is reposted only
 * after its frame has been delivered. */
static void test_rx_posting(void)
{
    uint8_t frame[100];
    unsigned i;
    setup();
    for (i = 0; i < BCM_RX_BUFFERS; ++i) {
        memset(frame, (int)i, sizeof(frame));
        assert(chip_receive(frame, sizeof(frame), 0));
    }
    assert(!chip_receive(frame, sizeof(frame), 0));
    received_count = 0;
    assert(bcm_rx_poll(&io, &r, deliver, &r) == BCM_RX_BUFFERS);
    assert(received_count == BCM_RX_BUFFERS && received[0] == BCM_RX_BUFFERS - 1);
    assert(bcm_rx_poll(&io, &r, deliver, &r) == 0);
    assert(chip_receive(frame, sizeof(frame), 0));
}

static void test_rx_errors(void)
{
    uint8_t frame[100] = {0};
    unsigned writes;
    setup();
    received_count = 0;
    assert(chip_receive(frame, sizeof(frame), BCM_RXD_ERROR));
    assert(chip_receive(frame, 50, 0));                 /* runt */
    assert(chip_receive(frame, sizeof(frame), 0));
    r.ret[2].index = BCM_RX_BUFFERS;                   /* corrupt entry */
    assert(chip_receive(frame, sizeof(frame), 0));
    assert(bcm_rx_poll(&io, &r, deliver, &r) == 4);
    assert(received_count == 1 && r.rx_errors == 2 && r.rx_bad_index == 1);
    assert(mailbox_std == (4 + BCM_RX_BUFFERS) % 512);  /* all reposted */
    writes = mailbox_writes;
    assert(bcm_rx_poll(&io, &r, deliver, &r) == 0);
    assert(mailbox_writes == writes);                   /* nothing to report */
}

/* Tagged status: the chip bumps the tag with each status block update and
 * keeps INTA asserted until the mailbox acknowledges that tag. */
static void chip_update(void)
{
    r.status->tag = (r.status->tag + 1) & 0xff;
    chip_wants_irq = 1;
}

static void chip_check_ack(void)
{
    if (mailbox_irq != 1 && (mailbox_irq >> 24) == r.status->tag)
        chip_wants_irq = 0;
}

static void test_interrupts(void)
{
    uint32_t tag;
    setup();
    mailbox_irq = 0;
    chip_wants_irq = 0;
    /* Another device on a shared line: not claimed, mailbox untouched. */
    assert(!bcm_irq_claim(&io, &r) && mailbox_irq == 0);

    chip_update();
    assert(inta());
    assert(bcm_irq_claim(&io, &r));
    assert(mailbox_irq == 1 && !inta());     /* masked before the EOI */
    /* The other device again while our DPC is pending: not ours. */
    assert(!bcm_irq_claim(&io, &r));

    tag = bcm_irq_tag(&r);
    chip_update();                           /* more work meanwhile */
    bcm_irq_done(&io, &r, tag);
    chip_check_ack();
    assert(r.last_tag == tag && mailbox_irq == tag << 24);
    /* The newer tag was not acknowledged: the chip interrupts again. */
    assert(inta() && bcm_irq_claim(&io, &r));
    tag = bcm_irq_tag(&r);
    bcm_irq_done(&io, &r, tag);
    chip_check_ack();
    assert(!inta() && !bcm_irq_claim(&io, &r));

    /* Tag wrap-around. */
    r.status->tag = 0xff;
    r.irq_tag = 0xff;
    chip_update();
    assert(r.status->tag == 0 && bcm_irq_claim(&io, &r));
    bcm_irq_done(&io, &r, bcm_irq_tag(&r));
    chip_check_ack();
    assert(!inta() && mailbox_irq == 0);
}

int main(void)
{
    test_init();
    test_loopback();
    test_tx_full();
    test_rx_posting();
    test_rx_errors();
    test_interrupts();
    puts("Ring tests: ring setup, 2000-frame loopback through a simulated chip, send limits, receive posting and errors, tagged interrupts passed");
    return 0;
}
