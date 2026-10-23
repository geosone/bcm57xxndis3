/* NDIS 3.0 full-MAC semantics on top of a frame-copying NIC: protocol
 * bindings with their own packet filters and multicast lists, send queueing,
 * loopback to local bindings, receive indication with TransferData, and the
 * query/set OIDs. WfW has no Ethernet filter library; filtering is done here.
 *
 * Calls from NDIS and the NIC's DPC run serialized as VM calls or global
 * events. Interrupt-time time-outs of other drivers may still send, so the
 * send queue, the loopback queue and the NIC transmit are protected with
 * bcm_lock. No NDIS indication is made with interrupts disabled.
 */
#ifndef BCM57_ADAPTER_H
#define BCM57_ADAPTER_H

#include <stdint.h>
#include "ndis3.h"

#define BCM_MAX_BINDINGS 4
#define BCM_MULTICAST_MAX 16
#define BCM_SEND_QUEUE 32
#define BCM_LOOPBACK_SLOTS 4
#define BCM_ETHERNET_HEADER 14
#define BCM_ETHERNET_MAX 1514
#define BCM_LOOKAHEAD_MAX (BCM_ETHERNET_MAX - BCM_ETHERNET_HEADER)

#define BCM_FILTER_DIRECTED 0x0001UL
#define BCM_FILTER_MULTICAST 0x0002UL
#define BCM_FILTER_ALL_MULTICAST 0x0004UL
#define BCM_FILTER_BROADCAST 0x0008UL
#define BCM_FILTER_PROMISCUOUS 0x0020UL
#define BCM_FILTER_SUPPORTED 0x002fUL

/* Hardware behind the adapter; the NIC glue or a host test supplies it. */
struct bcm_adapter_hw {
    void *context;
    /* Copies the frame into a send buffer: 0, or -1 if none is free. */
    int (*transmit)(void *, const uint8_t *frame, unsigned length);
    void (*set_promiscuous)(void *, int on);
    /* Reinitializes the chip after a protocol reset; 0 on success. */
    int (*reset)(void *);
    unsigned send_buffers, receive_buffers, buffer_size;
};

struct bcm_adapter;

struct bcm_binding {
    struct bcm_adapter *adapter;
    bcm_ndis_handle ndis;              /* NdisBindingContext */
    int open, indicated;
    uint32_t filter, lookahead;
    unsigned multicast_count;
    uint8_t multicast[BCM_MULTICAST_MAX][6];
};

struct bcm_send {
    struct bcm_binding *binding;
    void *packet;
};

struct bcm_loopback {
    unsigned length;
    uint8_t data[BCM_ETHERNET_MAX];
};

struct bcm_adapter {
    const struct bcm_adapter_hw *hw;
    uint8_t permanent[6], current[6];
    unsigned link_speed;               /* Mbit/s, 0 while the link is down */
    int resetting, promiscuous;
    struct bcm_binding bindings[BCM_MAX_BINDINGS];
    struct bcm_send queue[BCM_SEND_QUEUE];
    unsigned queue_head, queue_count;
    struct bcm_loopback loopback[BCM_LOOPBACK_SLOTS];
    unsigned loopback_head, loopback_count;
    struct bcm_ndis_timer loopback_timer;
    uint8_t frame[BCM_ETHERNET_MAX];   /* send flattening, under bcm_lock */
    uint32_t xmit_ok, xmit_error, rcv_ok, rcv_error, rcv_no_buffer;
    uint32_t loopback_dropped;
};

void bcm_adapter_init(struct bcm_adapter *, const struct bcm_adapter_hw *,
                      const uint8_t mac[6]);

/* MacOpenAdapter: selects 802.3 from the medium array. */
bcm_ndis_status bcm_adapter_open(struct bcm_adapter *, bcm_ndis_status *error,
    bcm_ndis_handle *binding, unsigned *selected, const unsigned *media,
    unsigned media_count, bcm_ndis_handle ndis_binding);
/* MacCloseAdapter: queued sends of the binding complete as aborted. */
bcm_ndis_status bcm_adapter_close(struct bcm_binding *);
bcm_ndis_status bcm_adapter_send(struct bcm_binding *, void *packet);
/* receive_context is what bcm_adapter_receive passed to NdisIndicateReceive;
 * offset counts from the end of the Ethernet header. Never pends. */
bcm_ndis_status bcm_adapter_transfer(struct bcm_binding *,
    void *receive_context, unsigned offset, unsigned length, void *packet,
    unsigned *transferred);
bcm_ndis_status bcm_adapter_request(struct bcm_binding *,
                                    struct bcm_ndis_request *);
/* MacQueryGlobalStatistics: the query OIDs without a binding. */
bcm_ndis_status bcm_adapter_query_global(struct bcm_adapter *,
                                         struct bcm_ndis_request *);
/* Synchronous: RESET_START, hardware reset, RESET_END to every binding. */
bcm_ndis_status bcm_adapter_reset(struct bcm_binding *);

/* From the NIC's DPC: one received frame without CRC, then, once per batch,
 * the receive-complete indications. */
void bcm_adapter_receive(struct bcm_adapter *, const uint8_t *frame,
                         unsigned length);
void bcm_adapter_receive_complete(struct bcm_adapter *);
/* From the NIC's DPC after send buffers were freed: retries queued sends. */
void bcm_adapter_send_space(struct bcm_adapter *);

/* Number of open bindings. */
unsigned bcm_adapter_bindings(const struct bcm_adapter *);

#endif
