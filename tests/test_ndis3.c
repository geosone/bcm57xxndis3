#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "ndis3.h"
#include "adapter.h"
#include "nic.h"

static unsigned initialized, registered, deregistered, terminated;
static unsigned added, removed, sent;
static int fail_init, fail_deregister;
static bcm_ndis_status register_status, add_status;
static bcm_ndis_handle ndis_mac = &registered;
static struct bcm_ndis_string *add_name;
static struct bcm_mac_characteristics *registered_mac;

void bcm_log_text(const char *text) { (void)text; }
void bcm_log_hex(uint32_t value) { (void)value; }
uint32_t bcm_lock(void) { return 0; }
void bcm_unlock(uint32_t flags) { (void)flags; }

/* Only what the forwarding checks below reach: a packet is one buffer. */
void bcm_ndis_query_packet(void *packet, unsigned *physical, unsigned *buffers,
                           void **first, unsigned *length)
{
    (void)physical; (void)buffers;
    *first = packet;
    if (length)
        *length = 60;
}
void bcm_ndis_query_buffer(void *buffer, void **address, unsigned *length)
{
    *address = buffer;
    *length = 60;
}
void bcm_ndis_next_buffer(void *buffer, void **next) { (void)buffer; *next = 0; }
void bcm_ndis_complete_send(bcm_ndis_handle b, void *p, bcm_ndis_status s)
{ (void)b; (void)p; (void)s; assert(0); }
void bcm_ndis_indicate_receive(bcm_ndis_status *s, bcm_ndis_handle b, void *c,
    void *h, unsigned hs, void *l, unsigned ls, unsigned ps)
{ (void)s; (void)b; (void)c; (void)h; (void)hs; (void)l; (void)ls; (void)ps; assert(0); }
void bcm_ndis_indicate_receive_complete(bcm_ndis_handle b) { (void)b; assert(0); }
void bcm_ndis_indicate_status(bcm_ndis_handle b, bcm_ndis_status s, void *p,
                              unsigned n) { (void)b; (void)s; (void)p; (void)n; }
void bcm_ndis_indicate_status_complete(bcm_ndis_handle b) { (void)b; }
void bcm_ndis_set_timer(struct bcm_ndis_timer *t, unsigned ms) { (void)t; (void)ms; }

bcm_ndis_status bcm_nic_add(bcm_ndis_handle mac, bcm_ndis_handle configuration,
                            struct bcm_ndis_string *name)
{
    assert(mac == ndis_mac && configuration == &added && name == add_name);
    ++added;
    return add_status;
}

void bcm_nic_remove(void)
{
    ++removed;
}

void bcm_nic_shutdown(void) {}

void bcm_ndis_initialize(bcm_ndis_handle *handle, void *ids, void *path, void *other)
{
    uint16_t *counts = ids;
    assert(counts[0] == 0 && counts[1] == 0 && !path && !other);
    ++initialized;
    *handle = fail_init ? 0 : &initialized;
}

/* As WfW: the MAC handle is stored through MacMacContext before AddAdapter
 * runs inside NdisRegisterMac, and a failing AddAdapter fails it. */
void bcm_ndis_register(bcm_ndis_status *status, bcm_ndis_handle *handle,
    bcm_ndis_handle wrapper, bcm_ndis_handle context,
    struct bcm_mac_characteristics *mac, unsigned size)
{
    static char name_text[] = "BCM57$";
    static struct bcm_ndis_string name = {6, 7, name_text};
    bcm_ndis_status result;
    assert(wrapper == &initialized && context == handle);
    assert(size == sizeof(*mac) && mac->major == 3 && mac->minor == 0);
    assert(mac->reserved == 0 && mac->name.length == 8);
    assert(mac->name.maximum_length == 9 && !strcmp(mac->name.buffer, "BCM57DBG"));
    ++registered;
    registered_mac = mac;
    *handle = ndis_mac;
    add_name = &name;
    result = mac->add(context, &added, &name);
    *status = result ? result : register_status;
    if (*status)
        *handle = 0;
}

void bcm_ndis_deregister(bcm_ndis_status *status, bcm_ndis_handle handle)
{
    assert(handle == ndis_mac);
    ++deregistered;
    *status = fail_deregister ? BCM_NDIS_FAILURE : BCM_NDIS_SUCCESS;
}

void bcm_ndis_terminate(bcm_ndis_handle handle, void *specific)
{
    assert(handle == &initialized && !specific);
    ++terminated;
}

static int transmit(void *context, const uint8_t *frame, unsigned length)
{
    (void)context; (void)frame;
    assert(length == 60);
    ++sent;
    return 0;
}
static void promiscuous(void *context, int on) { (void)context; (void)on; }
static int reset(void *context) { (void)context; return 0; }

/* The characteristics forward to the adapter layer with the contexts NDIS
 * passes: MacAdapterContext to open and statistics, the binding otherwise. */
static void test_forwarding(struct bcm_mac_characteristics *mac)
{
    static const struct bcm_adapter_hw hw = {
        0, transmit, promiscuous, reset, 32, 64, 1536
    };
    static const uint8_t station[6] = {0, 0x10, 0x18, 1, 2, 3};
    static struct bcm_adapter adapter;
    static int protocol;
    static uint8_t frame[60];
    unsigned media[2] = {7, BCM_NDIS_MEDIUM_802_3}, selected = 9, transferred;
    bcm_ndis_status error;
    bcm_ndis_handle binding;
    struct bcm_ndis_request r;
    uint8_t address[6];

    bcm_adapter_init(&adapter, &hw, station);
    assert(mac->open(&error, &binding, &selected, media, 2, &protocol,
                     &adapter, 0, 0) == BCM_NDIS_SUCCESS && selected == 1);
    memset(frame, 0xff, 6);
    assert(mac->send(binding, frame) == BCM_NDIS_SUCCESS && sent == 1);
    memset(&r, 0, sizeof(r));
    r.type = BCM_NDIS_REQUEST_QUERY;
    r.oid = 0x01010102;
    r.buffer = address;
    r.length = 6;
    assert(mac->request(binding, &r) == BCM_NDIS_SUCCESS &&
           !memcmp(address, station, 6));
    r.oid = 0x01010101;
    assert(mac->statistics(&adapter, &r) == BCM_NDIS_SUCCESS);
    assert(mac->transfer(binding, 0, 0, 1, frame, &transferred) ==
           BCM_NDIS_FAILURE && transferred == 0);
    assert(mac->reset(binding) == BCM_NDIS_SUCCESS);
    assert(mac->close(binding) == BCM_NDIS_SUCCESS);
    mac->remove(&adapter);
    assert(removed == 1);
}

int main(void)
{
    assert(offsetof(struct bcm_mac_characteristics, open) == 6);
    assert(offsetof(struct bcm_mac_characteristics, name) == 6 + 10 * sizeof(void *));
    assert(offsetof(struct bcm_ndis_request, oid) == 20);
    assert(offsetof(struct bcm_ndis_request, bytes_done) == 28 + sizeof(void *));
    assert(offsetof(struct bcm_ndis_request, bytes_needed) == 32 + sizeof(void *));
    assert(offsetof(struct bcm_ndis_adapter_information, adapter_type) ==
           6 + sizeof(void *));
    assert(offsetof(struct bcm_ndis_adapter_information, port_count) ==
           18 + sizeof(void *));

    /* No card: AddAdapter fails, and so does the registration. */
    add_status = BCM_NDIS_ADAPTER_NOT_FOUND;
    assert(bcm_mac_start() == BCM_NDIS_ADAPTER_NOT_FOUND);
    assert(added == 1 && initialized == 1 && terminated == 1);

    add_status = BCM_NDIS_SUCCESS;
    assert(bcm_mac_start() == BCM_NDIS_SUCCESS && added == 2);
    assert(bcm_mac_start() == BCM_NDIS_SUCCESS && initialized == 2 && registered == 2);
    test_forwarding(registered_mac);
    fail_deregister = 1;
    bcm_mac_stop();
    assert(deregistered == 1 && terminated == 1);
    fail_deregister = 0;
    bcm_mac_stop();
    bcm_mac_stop();
    assert(deregistered == 2 && terminated == 2);
    register_status = BCM_NDIS_FAILURE;
    assert(bcm_mac_start() == BCM_NDIS_FAILURE);
    assert(initialized == 3 && registered == 3 && terminated == 3);
    fail_init = 1;
    assert(bcm_mac_start() == BCM_NDIS_FAILURE);
    assert(initialized == 4 && registered == 3 && terminated == 3);
    bcm_mac_stop();
    puts("NDIS tests: packed layouts, AddAdapter inside registration, forwarding to the adapter layer, lifecycle and failure cleanup passed");
    return 0;
}
