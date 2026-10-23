#include "bcm5722_ring.h"

#define MAILBOX_STD_PRODUCER 0x26c
#define MAILBOX_RETURN1_CONSUMER 0x284
#define MAILBOX_TX_PRODUCER 0x304
#define MAILBOX_INTERRUPT 0x204
#define PCI_STATE 0x70
#define PCI_STATE_INT_NOT_ACTIVE 2UL
#define RING_MASK (BCM_RING_ENTRIES - 1)

static void copy(uint8_t *to, const uint8_t *from, unsigned length)
{
    while (length--)
        *to++ = *from++;
}

void bcm_rings_setup(struct bcm_rings *r, void *block, uint32_t block_phys)
{
    uint8_t *base = block;
    r->tx = (volatile struct bcm_tx_descriptor *)(base + BCM_RING_TX_OFFSET);
    r->std = (volatile struct bcm_rx_descriptor *)(base + BCM_RING_STD_OFFSET);
    r->ret = (volatile struct bcm_rx_descriptor *)(base + BCM_RING_RETURN_OFFSET);
    r->status = (volatile struct bcm_status_block *)(base + BCM_RING_STATUS_OFFSET);
    r->block_phys = block_phys;
}

void bcm_rings_init(struct bcm_rings *r)
{
    volatile uint8_t *p = (volatile uint8_t *)r->tx;
    unsigned i, buffer;
    for (i = 0; i < BCM_RING_BLOCK_SIZE; ++i)
        p[i] = 0;
    /* The standard ring never changes: slot s always points at buffer s % n. */
    for (i = 0; i < BCM_RING_ENTRIES; ++i) {
        buffer = i % BCM_RX_BUFFERS;
        r->std[i].address_low = r->rx_phys[buffer];
        r->std[i].length = BCM_BUFFER_SIZE;
        r->std[i].index = (uint16_t)buffer;
        r->std[i].flags = BCM_RXD_END;
    }
    r->tx_producer = r->tx_consumer = r->ret_consumer = 0;
    r->rx_producer = BCM_RX_BUFFERS;
    r->rx_errors = r->rx_bad_index = 0;
    r->irq_tag = r->last_tag = 0;
}

int bcm_tx_send(const struct bcm_hw_io *io, struct bcm_rings *r,
                const void *frame, unsigned length)
{
    unsigned slot, buffer;
    uint8_t *data;
    if (r->tx_producer - r->tx_consumer >= BCM_TX_BUFFERS ||
            length < 14 || length > BCM_FRAME_MAX)
        return -1;
    slot = r->tx_producer & RING_MASK;
    buffer = slot % BCM_TX_BUFFERS;
    data = r->tx_buffer[buffer];
    copy(data, frame, length);
    while (length < BCM_FRAME_MIN)
        data[length++] = 0;
    r->tx[slot].address_high = 0;
    r->tx[slot].address_low = r->tx_phys[buffer];
    r->tx[slot].length = (uint16_t)length;
    r->tx[slot].flags = BCM_TXD_END;
    r->tx[slot].vlan_tag = 0;
    r->tx[slot].mss = 0;
    ++r->tx_producer;
    io->write32(io->context, MAILBOX_TX_PRODUCER, r->tx_producer & RING_MASK);
    return 0;
}

unsigned bcm_tx_complete(struct bcm_rings *r)
{
    unsigned done = 0, consumer = r->status->tx_consumer & RING_MASK;
    /* A consumer outside the outstanding range is ignored. */
    if (((consumer - r->tx_consumer) & RING_MASK) >
            r->tx_producer - r->tx_consumer)
        return 0;
    while ((r->tx_consumer & RING_MASK) != consumer) {
        ++r->tx_consumer;
        ++done;
    }
    return done;
}

unsigned bcm_rx_poll(const struct bcm_hw_io *io, struct bcm_rings *r,
                     bcm_rx_fn deliver, void *context)
{
    unsigned count = 0, producer = r->status->return1_producer & RING_MASK;
    unsigned index, length;
    volatile struct bcm_rx_descriptor *d;
    while ((r->ret_consumer & RING_MASK) != producer) {
        d = &r->ret[r->ret_consumer & RING_MASK];
        index = d->index;
        length = d->length;
        if (index >= BCM_RX_BUFFERS)
            ++r->rx_bad_index;
        else if ((d->flags & BCM_RXD_ERROR) || length < BCM_FRAME_MIN + 4 ||
                 length > BCM_FRAME_MAX + 4)
            ++r->rx_errors;
        else
            deliver(context, r->rx_buffer[index], length - 4);
        /* The buffer is free again: post the slot that reuses it. */
        ++r->ret_consumer;
        ++r->rx_producer;
        ++count;
    }
    if (count) {
        io->write32(io->context, MAILBOX_RETURN1_CONSUMER,
                    r->ret_consumer & RING_MASK);
        io->write32(io->context, MAILBOX_STD_PRODUCER,
                    r->rx_producer & RING_MASK);
    }
    return count;
}

int bcm_irq_claim(const struct bcm_hw_io *io, struct bcm_rings *r)
{
    uint32_t tag = r->status->tag;
    if (tag == r->irq_tag &&
            (io->read32(io->context, PCI_STATE) & PCI_STATE_INT_NOT_ACTIVE))
        return 0;
    io->write32(io->context, MAILBOX_INTERRUPT, 1);
    (void)io->read32(io->context, MAILBOX_INTERRUPT);
    r->irq_tag = tag;
    return 1;
}

uint32_t bcm_irq_tag(const struct bcm_rings *r)
{
    return r->status->tag;
}

void bcm_irq_done(const struct bcm_hw_io *io, struct bcm_rings *r,
                  uint32_t tag)
{
    r->last_tag = tag;
    io->write32(io->context, MAILBOX_INTERRUPT, tag << 24);
}
