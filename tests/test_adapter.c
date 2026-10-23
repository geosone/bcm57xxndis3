#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "adapter.h"

/* NDIS packets: a chain of buffers. */
struct buffer {
    uint8_t *data;
    unsigned length;
    struct buffer *next;
};
struct packet {
    struct buffer *first;
};

static unsigned lock_depth;

uint32_t bcm_lock(void)
{
    return lock_depth++;
}

void bcm_unlock(uint32_t flags)
{
    assert(lock_depth == flags + 1);
    --lock_depth;
}

void bcm_ndis_query_packet(void *p, unsigned *physical, unsigned *buffers,
                           void **first, unsigned *length)
{
    struct packet *packet = p;
    struct buffer *b;
    unsigned total = 0, count = 0;
    for (b = packet->first; b; b = b->next) {
        total += b->length;
        ++count;
    }
    if (physical) *physical = count;
    if (buffers) *buffers = count;
    if (first) *first = packet->first;
    if (length) *length = total;
}

void bcm_ndis_query_buffer(void *buffer, void **address, unsigned *length)
{
    struct buffer *b = buffer;
    *address = b->data;
    *length = b->length;
}

void bcm_ndis_next_buffer(void *buffer, void **next)
{
    *next = ((struct buffer *)buffer)->next;
}

/* Protocols: the NDIS binding context is the protocol's index. */
static int protocol[BCM_MAX_BINDINGS] = {0, 1, 2, 3};
static struct bcm_binding *binding[BCM_MAX_BINDINGS];

struct event {
    char kind;                /* 'r'eceive, 'c'omplete, 's'end, 't'atus */
    int protocol;
    unsigned length;
    uint32_t status;
    void *packet;
    uint8_t data[BCM_ETHERNET_MAX];
};
static struct event events[64];
static unsigned event_count;

static struct event *log_event(char kind, void *ndis)
{
    struct event *e = &events[event_count++];
    assert(event_count <= 64);
    assert(lock_depth == 0);              /* never call NDIS with cli */
    memset(e, 0, sizeof(*e));
    e->kind = kind;
    e->protocol = *(int *)ndis;
    return e;
}

/* Called inside an indication, if set. */
static void (*on_receive)(int protocol, void *context);
static const uint8_t *indicated_header;

void bcm_ndis_indicate_receive(bcm_ndis_status *status, bcm_ndis_handle ndis,
    void *context, void *header, unsigned header_size, void *lookahead,
    unsigned lookahead_size, unsigned packet_size)
{
    struct event *e = log_event('r', ndis);
    assert(header_size == 14 && lookahead == (uint8_t *)header + 14);
    assert(lookahead_size == packet_size);
    e->length = header_size + packet_size;
    memcpy(e->data, header, e->length);
    *status = BCM_NDIS_SUCCESS;
    indicated_header = header;
    if (on_receive)
        on_receive(*(int *)ndis, context);
}

void bcm_ndis_indicate_receive_complete(bcm_ndis_handle ndis)
{
    log_event('c', ndis);
}

void bcm_ndis_complete_send(bcm_ndis_handle ndis, void *packet,
                            bcm_ndis_status status)
{
    struct event *e = log_event('s', ndis);
    e->packet = packet;
    e->status = status;
}

void bcm_ndis_indicate_status(bcm_ndis_handle ndis, bcm_ndis_status status,
                              void *buffer, unsigned size)
{
    struct event *e = log_event('t', ndis);
    assert(!buffer && !size);
    e->status = status;
}

void bcm_ndis_indicate_status_complete(bcm_ndis_handle ndis)
{
    log_event('t', ndis)->status = 1;
}

static struct bcm_ndis_timer *scheduled;

void bcm_ndis_set_timer(struct bcm_ndis_timer *timer, unsigned ms)
{
    assert(ms == 0);
    if (!timer->handle) {
        timer->handle = timer;
        scheduled = timer;
    }
}

/* Runs a pending 0 ms timer the way WfW's event callback does. */
static void run_events(void)
{
    while (scheduled) {
        struct bcm_ndis_timer *timer = scheduled;
        scheduled = 0;
        timer->handle = 0;
        timer->function(0, timer->context);
    }
}

/* The NIC. */
static uint8_t wire[8][BCM_ETHERNET_MAX];
static unsigned wire_length[8], wire_count, hw_free = 1000, resets;
static int promiscuous, sending_during_reset;
static struct packet *reset_packet;

static int transmit(void *context, const uint8_t *frame, unsigned length)
{
    assert(context == &wire && lock_depth == 1);
    if (!hw_free)
        return -1;
    --hw_free;
    if (wire_count < 8) {
        memcpy(wire[wire_count], frame, length);
        wire_length[wire_count] = length;
    }
    ++wire_count;
    return 0;
}

static void set_promiscuous(void *context, int on)
{
    assert(context == &wire);
    promiscuous = on;
}

static int reset(void *context)
{
    assert(context == &wire);
    ++resets;
    if (sending_during_reset)
        assert(bcm_adapter_send(binding[0], reset_packet) ==
               BCM_NDIS_RESET_IN_PROGRESS);
    return 0;
}

static const struct bcm_adapter_hw hw = {
    &wire, transmit, set_promiscuous, reset, 32, 64, 1536
};
static const uint8_t station[6] = {0x00, 0x10, 0x18, 0x01, 0x02, 0x03};
static const uint8_t other[6] = {0x00, 0x10, 0x18, 0x09, 0x09, 0x09};
static const uint8_t broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static const uint8_t group1[6] = {0x01, 0x00, 0x5e, 0x00, 0x00, 0x01};
static const uint8_t group2[6] = {0x01, 0x00, 0x5e, 0x00, 0x00, 0x02};
static struct bcm_adapter adapter;

static void setup(void)
{
    unsigned i, selected;
    bcm_ndis_status error;
    static const unsigned media[] = {0};
    bcm_ndis_handle handle;
    bcm_adapter_init(&adapter, &hw, station);
    for (i = 0; i < 3; ++i) {
        assert(bcm_adapter_open(&adapter, &error, &handle, &selected, media, 1,
                                &protocol[i]) == BCM_NDIS_SUCCESS);
        assert(error == BCM_NDIS_SUCCESS && selected == 0);
        binding[i] = handle;
    }
    event_count = wire_count = resets = 0;
    hw_free = 1000;
    promiscuous = sending_during_reset = 0;
    on_receive = 0;
    scheduled = 0;
}

static bcm_ndis_status request(int p, uint32_t type, uint32_t oid,
                               void *buffer, unsigned length,
                               struct bcm_ndis_request *r)
{
    memset(r, 0x55, sizeof(*r));
    r->type = type;
    r->oid = oid;
    r->buffer = buffer;
    r->length = length;
    return bcm_adapter_request(binding[p], r);
}

static void set_filter(int p, uint32_t filter)
{
    struct bcm_ndis_request r;
    assert(request(p, BCM_NDIS_REQUEST_SET, 0x0001010e, &filter, 4, &r) ==
           BCM_NDIS_SUCCESS && r.bytes_done == 4);
}

static unsigned make_frame(uint8_t *frame, const uint8_t *to, unsigned length,
                           uint8_t seed)
{
    unsigned i;
    memcpy(frame, to, 6);
    memcpy(frame + 6, station, 6);
    for (i = 12; i < length; ++i)
        frame[i] = (uint8_t)(seed + i);
    return length;
}

/* Which protocols got the frame, as a bit mask; clears the log. */
static unsigned receivers(const uint8_t *to)
{
    uint8_t frame[100];
    unsigned i, mask = 0;
    event_count = 0;
    bcm_adapter_receive(&adapter, frame, make_frame(frame, to, 100, 1));
    bcm_adapter_receive_complete(&adapter);
    for (i = 0; i < event_count; ++i)
        if (events[i].kind == 'r') {
            assert(events[i].length == 100 && !memcmp(events[i].data, frame, 100));
            mask |= 1u << events[i].protocol;
        }
    /* One receive-complete per protocol that got an indication. */
    for (i = 0; i < event_count; ++i)
        if (events[i].kind == 'c')
            mask ^= 0x100u << events[i].protocol;
    assert((mask >> 8) == (mask & 0xff));
    event_count = 0;
    return mask & 0xff;
}

static void test_open(void)
{
    static const unsigned media[] = {5, 3, 0};
    static const unsigned token_ring[] = {1};
    bcm_ndis_handle b;
    bcm_ndis_status error;
    unsigned selected = 99;
    setup();
    assert(bcm_adapter_open(&adapter, &error, &b, &selected, token_ring, 1,
                            &protocol[3]) == BCM_NDIS_UNSUPPORTED_MEDIA);
    assert(bcm_adapter_open(&adapter, &error, &b, &selected, media, 3,
                            &protocol[3]) == BCM_NDIS_SUCCESS && selected == 2);
    assert(bcm_adapter_open(&adapter, &error, &b, &selected, media, 3,
                            &protocol[3]) == BCM_NDIS_RESOURCES);
    assert(bcm_adapter_bindings(&adapter) == 4);
    assert(bcm_adapter_close(binding[1]) == BCM_NDIS_SUCCESS);
    assert(bcm_adapter_bindings(&adapter) == 3);
}

static void test_filters(void)
{
    struct bcm_ndis_request r;
    uint8_t list[12];
    setup();
    /* A new binding receives nothing until it sets a filter. */
    assert(receivers(station) == 0 && receivers(broadcast) == 0);
    set_filter(0, BCM_FILTER_DIRECTED | BCM_FILTER_BROADCAST);
    set_filter(1, BCM_FILTER_MULTICAST);
    memcpy(list, group1, 6);
    memcpy(list + 6, group2, 6);
    assert(request(1, BCM_NDIS_REQUEST_SET, 0x01010103, list, 6, &r) ==
           BCM_NDIS_SUCCESS);
    assert(receivers(station) == 1);
    assert(receivers(other) == 0);
    assert(receivers(broadcast) == 1);
    assert(receivers(group1) == 2);
    assert(receivers(group2) == 0);
    assert(request(1, BCM_NDIS_REQUEST_SET, 0x01010103, list, 12, &r) ==
           BCM_NDIS_SUCCESS);
    assert(receivers(group2) == 2);
    set_filter(2, BCM_FILTER_ALL_MULTICAST);
    assert(receivers(group2) == 6 && receivers(broadcast) == 1);
    /* Promiscuous: everything, and the chip follows the union. */
    assert(!promiscuous);
    set_filter(2, BCM_FILTER_PROMISCUOUS);
    assert(promiscuous);
    assert(receivers(other) == 4 && receivers(broadcast) == 5);
    assert(bcm_adapter_close(binding[2]) == BCM_NDIS_SUCCESS);
    assert(!promiscuous && receivers(other) == 0);
    /* Runts and giants are counted, not indicated. */
    {
        uint8_t frame[BCM_ETHERNET_MAX + 1];
        make_frame(frame, station, sizeof(frame), 0);
        bcm_adapter_receive(&adapter, frame, 13);
        bcm_adapter_receive(&adapter, frame, sizeof(frame));
        assert(event_count == 0 && adapter.rcv_error == 2);
    }
}

static struct packet transfer_packet;
static struct buffer transfer_buffers[3];
static uint8_t transfer_data[3][1000];
static unsigned transfer_offset, transfer_length, transferred;
static bcm_ndis_status transfer_status;

static void do_transfer(int p, void *context)
{
    transfer_status = bcm_adapter_transfer(binding[p], context, transfer_offset,
        transfer_length, &transfer_packet, &transferred);
}

static void test_transfer(void)
{
    uint8_t frame[200];
    unsigned i;
    setup();
    set_filter(0, BCM_FILTER_DIRECTED);
    transfer_packet.first = &transfer_buffers[0];
    transfer_buffers[0].data = transfer_data[0];
    transfer_buffers[0].length = 7;
    transfer_buffers[0].next = &transfer_buffers[1];
    transfer_buffers[1].data = transfer_data[1];
    transfer_buffers[1].length = 50;
    transfer_buffers[1].next = &transfer_buffers[2];
    transfer_buffers[2].data = transfer_data[2];
    transfer_buffers[2].length = 1000;
    transfer_buffers[2].next = 0;
    make_frame(frame, station, sizeof(frame), 7);
    on_receive = do_transfer;

    /* From offset 10 after the header to the end of the frame. */
    memset(transfer_data, 0, sizeof(transfer_data));
    transfer_offset = 10;
    transfer_length = 1000;
    bcm_adapter_receive(&adapter, frame, sizeof(frame));
    assert(transfer_status == BCM_NDIS_SUCCESS && transferred == 200 - 14 - 10);
    assert(!memcmp(transfer_data[0], frame + 24, 7));
    assert(!memcmp(transfer_data[1], frame + 31, 50));
    assert(!memcmp(transfer_data[2], frame + 81, 200 - 81));
    assert(transfer_data[2][200 - 81] == 0);

    /* A short transfer stops inside the second buffer. */
    memset(transfer_data, 0, sizeof(transfer_data));
    transfer_offset = 0;
    transfer_length = 20;
    bcm_adapter_receive(&adapter, frame, sizeof(frame));
    assert(transferred == 20 && !memcmp(transfer_data[1], frame + 21, 13));
    for (i = 13; i < 50; ++i)
        assert(transfer_data[1][i] == 0);

    /* Beyond the end: nothing. */
    transfer_offset = 186;
    bcm_adapter_receive(&adapter, frame, sizeof(frame));
    assert(transfer_status == BCM_NDIS_SUCCESS && transferred == 0);
}

/* A packet of three buffers. */
static struct packet send_packet[40];
static struct buffer send_buffers[40][3];
static uint8_t send_data[40][BCM_ETHERNET_MAX];

static struct packet *packet(unsigned n, const uint8_t *to, unsigned length)
{
    unsigned split1 = length / 3, split2 = length / 2;
    make_frame(send_data[n], to, length, (uint8_t)n);
    send_buffers[n][0].data = send_data[n];
    send_buffers[n][0].length = split1;
    send_buffers[n][0].next = &send_buffers[n][1];
    send_buffers[n][1].data = send_data[n] + split1;
    send_buffers[n][1].length = split2 - split1;
    send_buffers[n][1].next = &send_buffers[n][2];
    send_buffers[n][2].data = send_data[n] + split2;
    send_buffers[n][2].length = length - split2;
    send_buffers[n][2].next = 0;
    send_packet[n].first = &send_buffers[n][0];
    return &send_packet[n];
}

static void test_send(void)
{
    unsigned i;
    setup();
    set_filter(0, BCM_FILTER_DIRECTED | BCM_FILTER_BROADCAST);
    set_filter(1, BCM_FILTER_DIRECTED);

    /* To another station: only the wire, gathered from all buffers. */
    assert(bcm_adapter_send(binding[1], packet(0, other, 100)) ==
           BCM_NDIS_SUCCESS);
    assert(wire_count == 1 && wire_length[0] == 100);
    assert(!memcmp(wire[0], send_data[0], 100));
    run_events();
    assert(event_count == 0);

    /* Broadcast: the wire and a deferred loopback to protocol 0. */
    assert(bcm_adapter_send(binding[1], packet(1, broadcast, 60)) ==
           BCM_NDIS_SUCCESS);
    assert(wire_count == 2 && event_count == 0);
    run_events();
    assert(event_count == 2 && events[0].kind == 'r' && events[0].protocol == 0);
    assert(!memcmp(events[0].data, send_data[1], 60));
    assert(events[1].kind == 'c' && events[1].protocol == 0);

    /* To the station address: never on the wire, both protocols get it. */
    event_count = 0;
    assert(bcm_adapter_send(binding[0], packet(2, station, 80)) ==
           BCM_NDIS_SUCCESS);
    assert(wire_count == 2);
    run_events();
    assert(event_count == 4 && events[0].protocol == 0 && events[1].protocol == 1);

    /* Invalid lengths. */
    assert(bcm_adapter_send(binding[0], packet(3, other, 13)) ==
           BCM_NDIS_INVALID_PACKET);
    assert(bcm_adapter_send(binding[0], packet(3, other, BCM_ETHERNET_MAX + 1)) ==
           BCM_NDIS_INVALID_PACKET);
    assert(wire_count == 2 && adapter.xmit_error == 2 && adapter.xmit_ok == 3);

    /* No free send buffer: queued, in order, completed later. */
    event_count = 0;
    hw_free = 0;
    assert(bcm_adapter_send(binding[0], packet(4, other, 70)) == BCM_NDIS_PENDING);
    hw_free = 1;
    /* Still pending: queued packets go first. */
    assert(bcm_adapter_send(binding[1], packet(5, other, 71)) == BCM_NDIS_PENDING);
    assert(wire_count == 2);
    bcm_adapter_send_space(&adapter);
    assert(wire_count == 3 && !memcmp(wire[2], send_data[4], 70));
    assert(event_count == 1 && events[0].kind == 's' && events[0].protocol == 0 &&
           events[0].packet == &send_packet[4] &&
           events[0].status == BCM_NDIS_SUCCESS);
    hw_free = 5;
    bcm_adapter_send_space(&adapter);
    assert(wire_count == 4 && !memcmp(wire[3], send_data[5], 71));
    assert(event_count == 2 && events[1].protocol == 1 &&
           events[1].packet == &send_packet[5]);
    bcm_adapter_send_space(&adapter);
    assert(event_count == 2);

    /* A full queue refuses. */
    hw_free = 0;
    for (i = 0; i < BCM_SEND_QUEUE; ++i)
        assert(bcm_adapter_send(binding[i & 1], packet(6 + i, other, 64)) ==
               BCM_NDIS_PENDING);
    assert(bcm_adapter_send(binding[0], packet(39, other, 64)) ==
           BCM_NDIS_RESOURCES);

    /* Closing aborts only that binding's sends; the rest keep their order. */
    event_count = 0;
    assert(bcm_adapter_close(binding[1]) == BCM_NDIS_SUCCESS);
    assert(event_count == BCM_SEND_QUEUE / 2);
    for (i = 0; i < event_count; ++i)
        assert(events[i].protocol == 1 &&
               events[i].packet == &send_packet[6 + 2 * i + 1] &&
               events[i].status == BCM_NDIS_REQUEST_ABORTED);
    assert(bcm_adapter_send(binding[1], packet(39, other, 64)) == BCM_NDIS_CLOSING);
    event_count = 0;
    hw_free = 1000;
    wire_count = 0;
    bcm_adapter_send_space(&adapter);
    assert(event_count == BCM_SEND_QUEUE / 2 && wire_count == BCM_SEND_QUEUE / 2);
    for (i = 0; i < event_count; ++i)
        assert(events[i].packet == &send_packet[6 + 2 * i]);
    assert(!memcmp(wire[1], send_data[8], 64));
}

/* A protocol answering a loopback frame from within the indication. */
static void reply(int p, void *context)
{
    (void)context;
    if (p == 0 && event_count < 6)
        assert(bcm_adapter_send(binding[0], packet(10 + event_count, station,
                                                   64)) == BCM_NDIS_SUCCESS);
}

/* Sends more loopback frames than there are free slots, then checks that
 * the frame being indicated was not overwritten. */
static void flood(int p, void *context)
{
    unsigned i, length = events[event_count - 1].length;
    (void)context;
    if (p != 0 || event_count > 1)
        return;
    for (i = 0; i < BCM_LOOPBACK_SLOTS; ++i)
        assert(bcm_adapter_send(binding[0], packet(20 + i, station, 64)) ==
               BCM_NDIS_SUCCESS);
    assert(!memcmp(indicated_header, events[event_count - 1].data, length));
}

static void close_self(int p, void *context)
{
    (void)context;
    assert(bcm_adapter_close(binding[p]) == BCM_NDIS_SUCCESS);
}

static void test_loopback(void)
{
    unsigned i;
    setup();
    set_filter(0, BCM_FILTER_DIRECTED);
    on_receive = reply;
    assert(bcm_adapter_send(binding[0], packet(0, station, 64)) ==
           BCM_NDIS_SUCCESS);
    run_events();
    /* Each indication queued the next frame; all came in order, unchanged. */
    assert(event_count == 7 && events[6].kind == 'c');
    assert(!memcmp(events[0].data, send_data[0], 64));
    for (i = 1; i < 6; ++i)
        assert(!memcmp(events[i].data, send_data[10 + i], 64));
    assert(adapter.loopback_count == 0 && wire_count == 0);

    /* More frames than slots before the event runs: the excess is dropped. */
    on_receive = 0;
    event_count = 0;
    for (i = 0; i < BCM_LOOPBACK_SLOTS + 2; ++i)
        assert(bcm_adapter_send(binding[0], packet(i, station, 64)) ==
               BCM_NDIS_SUCCESS);
    assert(adapter.loopback_dropped == 2);
    run_events();
    assert(event_count == BCM_LOOPBACK_SLOTS + 1);

    /* The slot being indicated stays reserved. */
    setup();
    set_filter(0, BCM_FILTER_DIRECTED);
    on_receive = flood;
    assert(bcm_adapter_send(binding[0], packet(0, station, 100)) ==
           BCM_NDIS_SUCCESS);
    run_events();
    assert(adapter.loopback_dropped == 1);
    assert(event_count == BCM_LOOPBACK_SLOTS + 1);

    /* A binding closed during its indication gets no receive-complete. */
    setup();
    set_filter(0, BCM_FILTER_DIRECTED);
    set_filter(1, BCM_FILTER_DIRECTED);
    on_receive = close_self;
    event_count = 0;
    {
        uint8_t frame[64];
        bcm_adapter_receive(&adapter, frame, make_frame(frame, station, 64, 0));
        bcm_adapter_receive_complete(&adapter);
    }
    assert(event_count == 2 && events[0].kind == 'r' && events[1].kind == 'r');
}

static void test_requests(void)
{
    struct bcm_ndis_request r;
    uint8_t buffer[200];
    uint32_t value;
    uint16_t version;
    setup();
    /* Supported list: too short first, then complete. */
    assert(request(0, BCM_NDIS_REQUEST_QUERY, 0x00010101, buffer, 8, &r) ==
           BCM_NDIS_BUFFER_TOO_SHORT);
    assert(r.bytes_done == 0 && r.bytes_needed == 31 * 4);
    assert(request(0, BCM_NDIS_REQUEST_QUERY, 0x00010101, buffer, 200, &r) ==
           BCM_NDIS_SUCCESS && r.bytes_done == 31 * 4 && r.bytes_needed == 0);
    memcpy(&value, buffer + 30 * 4, 4);
    assert(value == 0x01020103);
    assert(request(0, BCM_NDIS_REQUEST_QUERY, 0x01010102, buffer, 6, &r) ==
           BCM_NDIS_SUCCESS && !memcmp(buffer, station, 6));
    assert(request(0, BCM_NDIS_REQUEST_QUERY, 0x0001010d, buffer, 200, &r) ==
           BCM_NDIS_SUCCESS && !strcmp((char *)buffer, "Broadcom BCM5722 NetXtreme"));
    assert(request(0, BCM_NDIS_REQUEST_QUERY, 0x00010110, &version, 2, &r) ==
           BCM_NDIS_SUCCESS && version == 0x0300);
    assert(request(0, BCM_NDIS_REQUEST_QUERY, 0x0001010c, &value, 4, &r) ==
           BCM_NDIS_SUCCESS && value == 0x181000);
    adapter.link_speed = 1000;
    assert(request(0, BCM_NDIS_REQUEST_QUERY, 0x00010107, &value, 4, &r) ==
           BCM_NDIS_SUCCESS && value == 10000000);
    assert(request(0, BCM_NDIS_REQUEST_QUERY, 0x00010108, &value, 4, &r) ==
           BCM_NDIS_SUCCESS && value == 32 * 1536);
    assert(request(0, BCM_NDIS_REQUEST_QUERY, 0x00010105, &value, 4, &r) ==
           BCM_NDIS_SUCCESS && value == 1500);
    assert(request(0, BCM_NDIS_REQUEST_QUERY, 0x00010113, &value, 4, &r) ==
           BCM_NDIS_SUCCESS && value == 7);
    assert(request(0, BCM_NDIS_REQUEST_QUERY, 0x12345678, &value, 4, &r) ==
           BCM_NDIS_INVALID_OID);
    assert(request(0, 2, 0x00010101, buffer, 200, &r) == BCM_NDIS_NOT_RECOGNIZED);

    /* Packet filter. */
    assert(request(0, BCM_NDIS_REQUEST_SET, 0x0001010e, &value, 2, &r) ==
           BCM_NDIS_INVALID_LENGTH && r.bytes_needed == 4);
    value = 0x10;                                    /* source routing */
    assert(request(0, BCM_NDIS_REQUEST_SET, 0x0001010e, &value, 4, &r) ==
           BCM_NDIS_NOT_SUPPORTED);
    set_filter(0, 0x0b);
    set_filter(1, 0x04);
    assert(request(0, BCM_NDIS_REQUEST_QUERY, 0x0001010e, &value, 4, &r) ==
           BCM_NDIS_SUCCESS && value == 0x0b);
    r.type = BCM_NDIS_REQUEST_QUERY;
    r.oid = 0x0001010e;
    r.buffer = &value;
    r.length = 4;
    assert(bcm_adapter_query_global(&adapter, &r) == BCM_NDIS_SUCCESS &&
           value == 0x0f);

    /* Lookahead. */
    value = 1501;
    assert(request(0, BCM_NDIS_REQUEST_SET, 0x0001010f, &value, 4, &r) ==
           BCM_NDIS_INVALID_DATA);
    value = 128;
    assert(request(0, BCM_NDIS_REQUEST_SET, 0x0001010f, &value, 4, &r) ==
           BCM_NDIS_SUCCESS);
    assert(request(0, BCM_NDIS_REQUEST_QUERY, 0x0001010f, &value, 4, &r) ==
           BCM_NDIS_SUCCESS && value == 128);

    /* Multicast list. */
    memset(buffer, 1, sizeof(buffer));
    assert(request(0, BCM_NDIS_REQUEST_SET, 0x01010103, buffer, 7, &r) ==
           BCM_NDIS_INVALID_LENGTH && r.bytes_needed == 12);
    assert(request(0, BCM_NDIS_REQUEST_SET, 0x01010103, buffer,
                   (BCM_MULTICAST_MAX + 1) * 6, &r) == BCM_NDIS_MULTICAST_FULL);
    memcpy(buffer, group1, 6);
    memcpy(buffer + 6, group2, 6);
    assert(request(0, BCM_NDIS_REQUEST_SET, 0x01010103, buffer, 12, &r) ==
           BCM_NDIS_SUCCESS && r.bytes_done == 12);
    memset(buffer, 0, sizeof(buffer));
    assert(request(0, BCM_NDIS_REQUEST_QUERY, 0x01010103, buffer, 200, &r) ==
           BCM_NDIS_SUCCESS && r.bytes_done == 12);
    assert(!memcmp(buffer, group1, 6) && !memcmp(buffer + 6, group2, 6));
    assert(request(1, BCM_NDIS_REQUEST_QUERY, 0x01010103, buffer, 200, &r) ==
           BCM_NDIS_SUCCESS && r.bytes_done == 0);
    /* Query-only OIDs cannot be set. */
    assert(request(0, BCM_NDIS_REQUEST_SET, 0x01010102, buffer, 6, &r) ==
           BCM_NDIS_INVALID_OID);
}

static void test_reset(void)
{
    unsigned i;
    setup();
    set_filter(2, BCM_FILTER_PROMISCUOUS);
    assert(bcm_adapter_close(binding[1]) == BCM_NDIS_SUCCESS);
    hw_free = 0;
    assert(bcm_adapter_send(binding[0], packet(0, other, 64)) == BCM_NDIS_PENDING);
    hw_free = 10;
    promiscuous = 0;
    event_count = 0;
    sending_during_reset = 1;
    reset_packet = packet(1, other, 64);
    assert(bcm_adapter_reset(binding[0]) == BCM_NDIS_SUCCESS);
    assert(resets == 1 && promiscuous);      /* filter reapplied */
    /* START to both open bindings, then END + complete, then the queue. */
    assert(event_count == 7);
    assert(events[0].protocol == 0 && events[0].status == BCM_NDIS_RESET_START);
    assert(events[1].protocol == 2 && events[1].status == BCM_NDIS_RESET_START);
    for (i = 2; i < 6; ++i)
        assert(events[i].kind == 't' && events[i].protocol == (i < 4 ? 0 : 2));
    assert(events[2].status == BCM_NDIS_RESET_END && events[3].status == 1);
    assert(events[6].kind == 's' && events[6].packet == &send_packet[0]);
    assert(wire_count == 1);
}

int main(void)
{
    test_open();
    test_filters();
    test_transfer();
    test_send();
    test_loopback();
    test_requests();
    test_reset();
    puts("Adapter tests: media selection, per-binding filters, TransferData, send queue and order, loopback, OIDs, reset passed");
    return 0;
}
