#include "adapter.h"

#define OID_GEN_SUPPORTED_LIST 0x00010101UL
#define OID_GEN_HARDWARE_STATUS 0x00010102UL
#define OID_GEN_MEDIA_SUPPORTED 0x00010103UL
#define OID_GEN_MEDIA_IN_USE 0x00010104UL
#define OID_GEN_MAXIMUM_LOOKAHEAD 0x00010105UL
#define OID_GEN_MAXIMUM_FRAME_SIZE 0x00010106UL
#define OID_GEN_LINK_SPEED 0x00010107UL
#define OID_GEN_TRANSMIT_BUFFER_SPACE 0x00010108UL
#define OID_GEN_RECEIVE_BUFFER_SPACE 0x00010109UL
#define OID_GEN_TRANSMIT_BLOCK_SIZE 0x0001010aUL
#define OID_GEN_RECEIVE_BLOCK_SIZE 0x0001010bUL
#define OID_GEN_VENDOR_ID 0x0001010cUL
#define OID_GEN_VENDOR_DESCRIPTION 0x0001010dUL
#define OID_GEN_CURRENT_PACKET_FILTER 0x0001010eUL
#define OID_GEN_CURRENT_LOOKAHEAD 0x0001010fUL
#define OID_GEN_DRIVER_VERSION 0x00010110UL
#define OID_GEN_MAXIMUM_TOTAL_SIZE 0x00010111UL
#define OID_GEN_PROTOCOL_OPTIONS 0x00010112UL
#define OID_GEN_MAC_OPTIONS 0x00010113UL
#define OID_GEN_XMIT_OK 0x00020101UL
#define OID_GEN_RCV_OK 0x00020102UL
#define OID_GEN_XMIT_ERROR 0x00020103UL
#define OID_GEN_RCV_ERROR 0x00020104UL
#define OID_GEN_RCV_NO_BUFFER 0x00020105UL
#define OID_802_3_PERMANENT_ADDRESS 0x01010101UL
#define OID_802_3_CURRENT_ADDRESS 0x01010102UL
#define OID_802_3_MULTICAST_LIST 0x01010103UL
#define OID_802_3_MAXIMUM_LIST_SIZE 0x01010104UL
#define OID_802_3_RCV_ERROR_ALIGNMENT 0x01020101UL
#define OID_802_3_XMIT_ONE_COLLISION 0x01020102UL
#define OID_802_3_XMIT_MORE_COLLISIONS 0x01020103UL

#define HARDWARE_STATUS_READY 0
#define HARDWARE_STATUS_RESET 2
/* Copy lookahead, receive serialized, transfers never pend. */
#define MAC_OPTIONS 7UL

static const uint32_t supported_oids[] = {
    OID_GEN_SUPPORTED_LIST, OID_GEN_HARDWARE_STATUS, OID_GEN_MEDIA_SUPPORTED,
    OID_GEN_MEDIA_IN_USE, OID_GEN_MAXIMUM_LOOKAHEAD,
    OID_GEN_MAXIMUM_FRAME_SIZE, OID_GEN_LINK_SPEED,
    OID_GEN_TRANSMIT_BUFFER_SPACE, OID_GEN_RECEIVE_BUFFER_SPACE,
    OID_GEN_TRANSMIT_BLOCK_SIZE, OID_GEN_RECEIVE_BLOCK_SIZE,
    OID_GEN_VENDOR_ID, OID_GEN_VENDOR_DESCRIPTION,
    OID_GEN_CURRENT_PACKET_FILTER, OID_GEN_CURRENT_LOOKAHEAD,
    OID_GEN_DRIVER_VERSION, OID_GEN_MAXIMUM_TOTAL_SIZE,
    OID_GEN_PROTOCOL_OPTIONS, OID_GEN_MAC_OPTIONS, OID_GEN_XMIT_OK,
    OID_GEN_RCV_OK, OID_GEN_XMIT_ERROR, OID_GEN_RCV_ERROR,
    OID_GEN_RCV_NO_BUFFER, OID_802_3_PERMANENT_ADDRESS,
    OID_802_3_CURRENT_ADDRESS, OID_802_3_MULTICAST_LIST,
    OID_802_3_MAXIMUM_LIST_SIZE, OID_802_3_RCV_ERROR_ALIGNMENT,
    OID_802_3_XMIT_ONE_COLLISION, OID_802_3_XMIT_MORE_COLLISIONS
};

static const char vendor_description[] = "Broadcom BCM5722 NetXtreme";

/* What NdisIndicateReceive passes as MacReceiveContext; valid only during
 * the indication. */
struct bcm_receive {
    const uint8_t *frame;
    unsigned length;
};

static void copy(void *to, const void *from, unsigned length)
{
    uint8_t *t = to;
    const uint8_t *f = from;
    while (length--)
        *t++ = *f++;
}

static int same_address(const uint8_t *a, const uint8_t *b)
{
    unsigned i;
    for (i = 0; i < 6; ++i)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static int accepts(const struct bcm_adapter *a, const struct bcm_binding *b,
                   const uint8_t *destination)
{
    static const uint8_t broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    uint32_t filter = b->filter;
    unsigned i;
    if (!b->open)
        return 0;
    if (filter & BCM_FILTER_PROMISCUOUS)
        return 1;
    if (!(destination[0] & 1))
        return (filter & BCM_FILTER_DIRECTED) &&
               same_address(destination, a->current);
    if (same_address(destination, broadcast))
        return (filter & BCM_FILTER_BROADCAST) != 0;
    if (filter & BCM_FILTER_ALL_MULTICAST)
        return 1;
    if (filter & BCM_FILTER_MULTICAST)
        for (i = 0; i < b->multicast_count; ++i)
            if (same_address(destination, b->multicast[i]))
                return 1;
    return 0;
}

static int any_accepts(const struct bcm_adapter *a, const uint8_t *destination)
{
    unsigned i;
    for (i = 0; i < BCM_MAX_BINDINGS; ++i)
        if (accepts(a, &a->bindings[i], destination))
            return 1;
    return 0;
}

static void update_promiscuous(struct bcm_adapter *a)
{
    unsigned i;
    int on = 0;
    for (i = 0; i < BCM_MAX_BINDINGS; ++i)
        if (a->bindings[i].open &&
                (a->bindings[i].filter & BCM_FILTER_PROMISCUOUS))
            on = 1;
    if (on != a->promiscuous) {
        a->promiscuous = on;
        a->hw->set_promiscuous(a->hw->context, on);
    }
}

/* Indicates a frame to every binding whose filter accepts it. */
static void indicate(struct bcm_adapter *a, const uint8_t *frame,
                     unsigned length)
{
    struct bcm_receive receive;
    bcm_ndis_status status;
    unsigned i;
    receive.frame = frame;
    receive.length = length;
    for (i = 0; i < BCM_MAX_BINDINGS; ++i) {
        struct bcm_binding *b = &a->bindings[i];
        if (!accepts(a, b, frame))
            continue;
        bcm_ndis_indicate_receive(&status, b->ndis, &receive, (void *)frame,
            BCM_ETHERNET_HEADER, (void *)(frame + BCM_ETHERNET_HEADER),
            length - BCM_ETHERNET_HEADER, length - BCM_ETHERNET_HEADER);
        b->indicated = 1;
    }
}

static void loopback_event(void *unused, void *context)
{
    struct bcm_adapter *a = context;
    struct bcm_loopback *slot;
    uint32_t flags;
    (void)unused;
    for (;;) {
        flags = bcm_lock();
        slot = a->loopback_count ? &a->loopback[a->loopback_head] : 0;
        bcm_unlock(flags);
        if (!slot)
            break;
        /* The slot stays occupied while it is indicated, so a send from
         * within the indication cannot overwrite it. */
        indicate(a, slot->data, slot->length);
        flags = bcm_lock();
        a->loopback_head = (a->loopback_head + 1) % BCM_LOOPBACK_SLOTS;
        --a->loopback_count;
        bcm_unlock(flags);
    }
    bcm_adapter_receive_complete(a);
}

/* Under bcm_lock. */
static void loopback_add(struct bcm_adapter *a, const uint8_t *frame,
                         unsigned length)
{
    struct bcm_loopback *slot;
    if (a->loopback_count == BCM_LOOPBACK_SLOTS) {
        ++a->loopback_dropped;
        return;
    }
    slot = &a->loopback[(a->loopback_head + a->loopback_count) %
                        BCM_LOOPBACK_SLOTS];
    copy(slot->data, frame, length);
    slot->length = length;
    ++a->loopback_count;
    bcm_ndis_set_timer(&a->loopback_timer, 0);
}

/* Copies the packet into a->frame; returns its length, 0 if invalid. */
static unsigned flatten(struct bcm_adapter *a, void *packet)
{
    void *buffer, *address;
    unsigned total, length, done = 0;
    bcm_ndis_query_packet(packet, 0, 0, &buffer, &total);
    if (total < BCM_ETHERNET_HEADER || total > BCM_ETHERNET_MAX)
        return 0;
    while (buffer && done < total) {
        bcm_ndis_query_buffer(buffer, &address, &length);
        if (length > total - done)
            length = total - done;
        copy(a->frame + done, address, length);
        done += length;
        bcm_ndis_next_buffer(buffer, &buffer);
    }
    return done == total ? total : 0;
}

/* Under bcm_lock. Frames to the station address are only looped back; all
 * others go to the wire and, if a local binding accepts them, are looped
 * back too. PENDING means no send buffer is free. */
static bcm_ndis_status transmit(struct bcm_adapter *a, void *packet)
{
    unsigned length = flatten(a, packet);
    int local, wire;
    if (!length) {
        ++a->xmit_error;
        return BCM_NDIS_INVALID_PACKET;
    }
    local = any_accepts(a, a->frame);
    wire = !same_address(a->frame, a->current);
    if (wire && a->hw->transmit(a->hw->context, a->frame, length))
        return BCM_NDIS_PENDING;
    if (local)
        loopback_add(a, a->frame, length);
    ++a->xmit_ok;
    return BCM_NDIS_SUCCESS;
}

void bcm_adapter_init(struct bcm_adapter *a, const struct bcm_adapter_hw *hw,
                      const uint8_t mac[6])
{
    uint8_t *p = (uint8_t *)a;
    unsigned i;
    for (i = 0; i < sizeof(*a); ++i)
        p[i] = 0;
    a->hw = hw;
    copy(a->permanent, mac, 6);
    copy(a->current, mac, 6);
    for (i = 0; i < BCM_MAX_BINDINGS; ++i)
        a->bindings[i].adapter = a;
    a->loopback_timer.function = loopback_event;
    a->loopback_timer.context = a;
}

bcm_ndis_status bcm_adapter_open(struct bcm_adapter *a, bcm_ndis_status *error,
    bcm_ndis_handle *binding, unsigned *selected, const unsigned *media,
    unsigned media_count, bcm_ndis_handle ndis_binding)
{
    struct bcm_binding *b = 0;
    unsigned i;
    *error = BCM_NDIS_SUCCESS;
    for (i = 0; i < media_count; ++i)
        if (media[i] == BCM_NDIS_MEDIUM_802_3)
            break;
    if (i == media_count)
        return BCM_NDIS_UNSUPPORTED_MEDIA;
    *selected = i;
    for (i = 0; i < BCM_MAX_BINDINGS && !b; ++i)
        if (!a->bindings[i].open)
            b = &a->bindings[i];
    if (!b)
        return BCM_NDIS_RESOURCES;
    b->ndis = ndis_binding;
    b->filter = 0;
    b->lookahead = BCM_LOOKAHEAD_MAX;
    b->multicast_count = 0;
    b->indicated = 0;
    b->open = 1;
    *binding = b;
    return BCM_NDIS_SUCCESS;
}

bcm_ndis_status bcm_adapter_close(struct bcm_binding *b)
{
    struct bcm_adapter *a = b->adapter;
    struct bcm_send aborted[BCM_SEND_QUEUE];
    unsigned i, kept = 0, count = 0;
    uint32_t flags;
    b->open = 0;
    flags = bcm_lock();
    for (i = 0; i < a->queue_count; ++i) {
        struct bcm_send s = a->queue[(a->queue_head + i) % BCM_SEND_QUEUE];
        if (s.binding == b)
            aborted[count++] = s;
        else
            a->queue[(a->queue_head + kept++) % BCM_SEND_QUEUE] = s;
    }
    a->queue_count = kept;
    bcm_unlock(flags);
    for (i = 0; i < count; ++i)
        bcm_ndis_complete_send(b->ndis, aborted[i].packet,
                               BCM_NDIS_REQUEST_ABORTED);
    update_promiscuous(a);
    return BCM_NDIS_SUCCESS;
}

bcm_ndis_status bcm_adapter_send(struct bcm_binding *b, void *packet)
{
    struct bcm_adapter *a = b->adapter;
    bcm_ndis_status status;
    uint32_t flags;
    if (!b->open)
        return BCM_NDIS_CLOSING;
    if (a->resetting)
        return BCM_NDIS_RESET_IN_PROGRESS;
    flags = bcm_lock();
    /* Queued sends go first, so the order on the wire is kept. */
    status = a->queue_count ? BCM_NDIS_PENDING : transmit(a, packet);
    if (status == BCM_NDIS_PENDING) {
        if (a->queue_count == BCM_SEND_QUEUE) {
            status = BCM_NDIS_RESOURCES;
        } else {
            struct bcm_send *s = &a->queue[(a->queue_head + a->queue_count) %
                                           BCM_SEND_QUEUE];
            s->binding = b;
            s->packet = packet;
            ++a->queue_count;
        }
    }
    bcm_unlock(flags);
    return status;
}

void bcm_adapter_send_space(struct bcm_adapter *a)
{
    struct bcm_send s;
    bcm_ndis_status status;
    uint32_t flags;
    for (;;) {
        flags = bcm_lock();
        if (!a->queue_count || a->resetting) {
            bcm_unlock(flags);
            return;
        }
        s = a->queue[a->queue_head];
        status = transmit(a, s.packet);
        if (status == BCM_NDIS_PENDING) {
            bcm_unlock(flags);
            return;
        }
        a->queue_head = (a->queue_head + 1) % BCM_SEND_QUEUE;
        --a->queue_count;
        bcm_unlock(flags);
        bcm_ndis_complete_send(s.binding->ndis, s.packet, status);
    }
}

bcm_ndis_status bcm_adapter_transfer(struct bcm_binding *b,
    void *receive_context, unsigned offset, unsigned length, void *packet,
    unsigned *transferred)
{
    const struct bcm_receive *receive = receive_context;
    const uint8_t *data;
    void *buffer, *address;
    unsigned available, size;
    (void)b;
    *transferred = 0;
    if (!receive)
        return BCM_NDIS_FAILURE;
    available = receive->length - BCM_ETHERNET_HEADER;
    if (offset >= available)
        return BCM_NDIS_SUCCESS;
    if (length > available - offset)
        length = available - offset;
    data = receive->frame + BCM_ETHERNET_HEADER + offset;
    bcm_ndis_query_packet(packet, 0, 0, &buffer, 0);
    while (buffer && length) {
        bcm_ndis_query_buffer(buffer, &address, &size);
        if (size > length)
            size = length;
        copy(address, data, size);
        data += size;
        length -= size;
        *transferred += size;
        bcm_ndis_next_buffer(buffer, &buffer);
    }
    return BCM_NDIS_SUCCESS;
}

static bcm_ndis_status query(struct bcm_adapter *a, struct bcm_binding *b,
                             struct bcm_ndis_request *r)
{
    uint32_t value = 0;
    uint16_t version = 0x0300;
    const void *data = &value;
    unsigned size = 4, i;

    switch (r->oid) {
    case OID_GEN_SUPPORTED_LIST:
        data = supported_oids;
        size = sizeof(supported_oids);
        break;
    case OID_GEN_HARDWARE_STATUS:
        value = a->resetting ? HARDWARE_STATUS_RESET : HARDWARE_STATUS_READY;
        break;
    case OID_GEN_MEDIA_SUPPORTED:
    case OID_GEN_MEDIA_IN_USE:
        value = BCM_NDIS_MEDIUM_802_3;
        break;
    case OID_GEN_MAXIMUM_LOOKAHEAD:
    case OID_GEN_MAXIMUM_FRAME_SIZE:
        value = BCM_LOOKAHEAD_MAX;
        break;
    case OID_GEN_MAXIMUM_TOTAL_SIZE:
    case OID_GEN_TRANSMIT_BLOCK_SIZE:
    case OID_GEN_RECEIVE_BLOCK_SIZE:
        value = BCM_ETHERNET_MAX;
        break;
    case OID_GEN_LINK_SPEED:            /* units of 100 bit/s */
        value = (a->link_speed ? a->link_speed : 10) * 10000UL;
        break;
    case OID_GEN_TRANSMIT_BUFFER_SPACE:
        value = (uint32_t)a->hw->send_buffers * a->hw->buffer_size;
        break;
    case OID_GEN_RECEIVE_BUFFER_SPACE:
        value = (uint32_t)a->hw->receive_buffers * a->hw->buffer_size;
        break;
    case OID_GEN_VENDOR_ID:             /* OUI, then our adapter number 0 */
        value = a->permanent[0] | (uint32_t)a->permanent[1] << 8 |
                (uint32_t)a->permanent[2] << 16;
        break;
    case OID_GEN_VENDOR_DESCRIPTION:
        data = vendor_description;
        size = sizeof(vendor_description);
        break;
    case OID_GEN_CURRENT_PACKET_FILTER: /* globally: all open bindings */
        for (i = 0; i < BCM_MAX_BINDINGS; ++i)
            if (b ? b == &a->bindings[i] : a->bindings[i].open)
                value |= a->bindings[i].filter;
        break;
    case OID_GEN_CURRENT_LOOKAHEAD:
        value = b ? b->lookahead : BCM_LOOKAHEAD_MAX;
        break;
    case OID_GEN_DRIVER_VERSION:
        data = &version;
        size = 2;
        break;
    case OID_GEN_MAC_OPTIONS:
        value = MAC_OPTIONS;
        break;
    case OID_GEN_XMIT_OK:
        value = a->xmit_ok;
        break;
    case OID_GEN_RCV_OK:
        value = a->rcv_ok;
        break;
    case OID_GEN_XMIT_ERROR:
        value = a->xmit_error;
        break;
    case OID_GEN_RCV_ERROR:
        value = a->rcv_error;
        break;
    case OID_GEN_RCV_NO_BUFFER:
        value = a->rcv_no_buffer;
        break;
    case OID_802_3_PERMANENT_ADDRESS:
        data = a->permanent;
        size = 6;
        break;
    case OID_802_3_CURRENT_ADDRESS:
        data = a->current;
        size = 6;
        break;
    case OID_802_3_MULTICAST_LIST:
        data = b ? b->multicast[0] : 0;
        size = b ? b->multicast_count * 6 : 0;
        break;
    case OID_802_3_MAXIMUM_LIST_SIZE:
        value = BCM_MULTICAST_MAX;
        break;
    case OID_802_3_RCV_ERROR_ALIGNMENT:
    case OID_802_3_XMIT_ONE_COLLISION:
    case OID_802_3_XMIT_MORE_COLLISIONS:
        break;
    default:
        r->bytes_done = r->bytes_needed = 0;
        return BCM_NDIS_INVALID_OID;
    }
    if (r->length < size) {
        r->bytes_done = 0;
        r->bytes_needed = size;
        return BCM_NDIS_BUFFER_TOO_SHORT;
    }
    copy(r->buffer, data, size);
    r->bytes_done = size;
    r->bytes_needed = 0;
    return BCM_NDIS_SUCCESS;
}

static bcm_ndis_status set(struct bcm_binding *b, struct bcm_ndis_request *r)
{
    uint32_t value = 0;
    unsigned count;
    r->bytes_done = r->bytes_needed = 0;
    switch (r->oid) {
    case OID_GEN_CURRENT_PACKET_FILTER:
    case OID_GEN_CURRENT_LOOKAHEAD:
    case OID_GEN_PROTOCOL_OPTIONS:
        if (r->length != 4) {
            r->bytes_needed = 4;
            return BCM_NDIS_INVALID_LENGTH;
        }
        copy(&value, r->buffer, 4);
        if (r->oid == OID_GEN_CURRENT_PACKET_FILTER) {
            if (value & ~BCM_FILTER_SUPPORTED)
                return BCM_NDIS_NOT_SUPPORTED;
            b->filter = value;
            update_promiscuous(b->adapter);
        } else if (r->oid == OID_GEN_CURRENT_LOOKAHEAD) {
            if (value > BCM_LOOKAHEAD_MAX)
                return BCM_NDIS_INVALID_DATA;
            b->lookahead = value;
        }
        break;
    case OID_802_3_MULTICAST_LIST:
        if (r->length % 6) {
            r->bytes_needed = (r->length / 6 + 1) * 6;
            return BCM_NDIS_INVALID_LENGTH;
        }
        count = r->length / 6;
        if (count > BCM_MULTICAST_MAX)
            return BCM_NDIS_MULTICAST_FULL;
        copy(b->multicast, r->buffer, r->length);
        b->multicast_count = count;
        break;
    default:
        return BCM_NDIS_INVALID_OID;
    }
    r->bytes_done = r->length;
    return BCM_NDIS_SUCCESS;
}

bcm_ndis_status bcm_adapter_request(struct bcm_binding *b,
                                    struct bcm_ndis_request *r)
{
    if (!b->open)
        return BCM_NDIS_CLOSING;
    if (r->type == BCM_NDIS_REQUEST_QUERY)
        return query(b->adapter, b, r);
    if (r->type == BCM_NDIS_REQUEST_SET)
        return set(b, r);
    return BCM_NDIS_NOT_RECOGNIZED;
}

bcm_ndis_status bcm_adapter_query_global(struct bcm_adapter *a,
                                         struct bcm_ndis_request *r)
{
    return query(a, 0, r);
}

bcm_ndis_status bcm_adapter_reset(struct bcm_binding *b)
{
    struct bcm_adapter *a = b->adapter;
    unsigned i;
    int failed;
    if (a->resetting)
        return BCM_NDIS_RESET_IN_PROGRESS;
    a->resetting = 1;
    for (i = 0; i < BCM_MAX_BINDINGS; ++i)
        if (a->bindings[i].open)
            bcm_ndis_indicate_status(a->bindings[i].ndis,
                                     BCM_NDIS_RESET_START, 0, 0);
    failed = a->hw->reset(a->hw->context);
    a->hw->set_promiscuous(a->hw->context, a->promiscuous);
    a->resetting = 0;
    for (i = 0; i < BCM_MAX_BINDINGS; ++i)
        if (a->bindings[i].open) {
            bcm_ndis_indicate_status(a->bindings[i].ndis,
                                     BCM_NDIS_RESET_END, 0, 0);
            bcm_ndis_indicate_status_complete(a->bindings[i].ndis);
        }
    bcm_adapter_send_space(a);
    return failed ? BCM_NDIS_FAILURE : BCM_NDIS_SUCCESS;
}

void bcm_adapter_receive(struct bcm_adapter *a, const uint8_t *frame,
                         unsigned length)
{
    if (length < BCM_ETHERNET_HEADER || length > BCM_ETHERNET_MAX) {
        ++a->rcv_error;
        return;
    }
    ++a->rcv_ok;
    indicate(a, frame, length);
}

void bcm_adapter_receive_complete(struct bcm_adapter *a)
{
    unsigned i;
    for (i = 0; i < BCM_MAX_BINDINGS; ++i)
        if (a->bindings[i].indicated) {
            a->bindings[i].indicated = 0;
            if (a->bindings[i].open)
                bcm_ndis_indicate_receive_complete(a->bindings[i].ndis);
        }
}

unsigned bcm_adapter_bindings(const struct bcm_adapter *a)
{
    unsigned i, count = 0;
    for (i = 0; i < BCM_MAX_BINDINGS; ++i)
        count += a->bindings[i].open != 0;
    return count;
}
