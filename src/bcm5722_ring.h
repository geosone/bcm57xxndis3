/* Host memory shared with the BCM5722: status block, send ring, standard
 * receive ring, receive return ring and the packet buffers.
 *
 * The chip's rings always have 512 entries. Only BCM_RX_BUFFERS receive and
 * BCM_TX_BUFFERS send buffers exist; ring slot s uses buffer s % count. The
 * chip consumes and returns slots in order, so a slot is posted again only
 * after the previous user of its buffer has been handled.
 */
#ifndef BCM5722_RING_H
#define BCM5722_RING_H

#include <stdint.h>
#include "bcm5722_hw.h"

#define BCM_RING_ENTRIES 512
#define BCM_RX_BUFFERS 64
#define BCM_TX_BUFFERS 32
#define BCM_BUFFER_SIZE 1536
#define BCM_FRAME_MIN 60                /* without CRC */
#define BCM_FRAME_MAX 1514

/* Ring block: page-aligned, physically contiguous. */
#define BCM_RING_TX_OFFSET 0x0000       /* 512 * 16 bytes */
#define BCM_RING_STD_OFFSET 0x2000      /* 512 * 32 bytes */
#define BCM_RING_RETURN_OFFSET 0x6000   /* 512 * 32 bytes */
#define BCM_RING_STATUS_OFFSET 0xa000
#define BCM_RING_BLOCK_SIZE 0xb000

/* Status block with GRC mode word/byte swapping on a little-endian host. */
struct bcm_status_block {
    uint32_t status, tag;
    uint16_t return2_producer, std_consumer, return4_producer, return3_producer;
    uint16_t return1_producer, tx_consumer;
};

#define BCM_STATUS_UPDATED 1UL
#define BCM_STATUS_LINK_CHANGED 2UL

#define BCM_TXD_END 0x0004
#define BCM_RXD_END 0x0004
#define BCM_RXD_ERROR 0x0400

struct bcm_rings {
    volatile struct bcm_tx_descriptor *tx;
    volatile struct bcm_rx_descriptor *std, *ret;
    volatile struct bcm_status_block *status;
    uint32_t block_phys;
    uint8_t *rx_buffer[BCM_RX_BUFFERS], *tx_buffer[BCM_TX_BUFFERS];
    uint32_t rx_phys[BCM_RX_BUFFERS], tx_phys[BCM_TX_BUFFERS];
    /* Free-running counters; ring index = counter % BCM_RING_ENTRIES. */
    unsigned tx_producer, tx_consumer, rx_producer, ret_consumer;
    unsigned rx_errors, rx_bad_index;
};

/* Sets the ring pointers into block (BCM_RING_BLOCK_SIZE bytes). */
void bcm_rings_setup(struct bcm_rings *, void *block, uint32_t block_phys);

/* Clears the rings and posts every receive buffer. The buffer arrays must be
 * filled; each buffer is BCM_BUFFER_SIZE bytes within one physical page. */
void bcm_rings_init(struct bcm_rings *);

/* Copies a frame (no CRC) into the next send buffer, pads it to 60 bytes and
 * hands it to the chip. Returns 0, or -1 if all buffers are in use or the
 * length is invalid. */
int bcm_tx_send(const struct bcm_hw_io *, struct bcm_rings *,
                const void *frame, unsigned length);

/* Releases the send buffers the chip has finished; returns how many. */
unsigned bcm_tx_complete(struct bcm_rings *);

typedef void (*bcm_rx_fn)(void *context, const uint8_t *frame,
                          unsigned length);

/* Hands every received good frame (without CRC) to deliver, reposts the
 * buffers and updates the mailboxes; returns the number of return entries. */
unsigned bcm_rx_poll(const struct bcm_hw_io *, struct bcm_rings *,
                     bcm_rx_fn deliver, void *context);

#endif
