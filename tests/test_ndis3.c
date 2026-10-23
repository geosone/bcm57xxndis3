#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "ndis3.h"

static unsigned initialized, registered, deregistered, terminated;
static int fail_init, fail_deregister;
static bcm_ndis_status register_status;

void bcm_log_text(const char *text) { (void)text; }
void bcm_log_hex(uint32_t value) { (void)value; }

void bcm_ndis_initialize(bcm_ndis_handle *handle, void *ids, void *path, void *other)
{
    uint16_t *counts = ids;
    assert(counts[0] == 0 && counts[1] == 0 && !path && !other);
    ++initialized;
    *handle = fail_init ? 0 : &initialized;
}

void bcm_ndis_register(bcm_ndis_status *status, bcm_ndis_handle *handle,
    bcm_ndis_handle wrapper, bcm_ndis_handle context,
    struct bcm_mac_characteristics *mac, unsigned size)
{
    unsigned transferred = 99, selected = 99;
    bcm_ndis_status error = 0;
    bcm_ndis_handle binding = &registered;
    assert(wrapper == &initialized && !context);
    assert(size == sizeof(*mac) && mac->major == 3 && mac->minor == 0);
    assert(mac->reserved == 0 && mac->name.length == 8);
    assert(mac->name.maximum_length == 9 && !strcmp(mac->name.buffer, "BCM57DBG"));
    /* The stub must never report working hardware or a completed transfer. */
    assert(mac->add(0, 0, 0) == BCM_NDIS_ADAPTER_NOT_FOUND);
    assert(mac->open(&error, &binding, &selected, 0, 0, 0, 0, 0, 0)
        == BCM_NDIS_ADAPTER_NOT_FOUND);
    assert(error == BCM_NDIS_ADAPTER_NOT_FOUND && !binding && !selected);
    assert(mac->transfer(0, 0, 0, 42, 0, &transferred) == BCM_NDIS_NOT_SUPPORTED);
    assert(transferred == 0);
    assert(mac->send(0, 0) == BCM_NDIS_NOT_SUPPORTED);
    ++registered;
    *status = register_status;
    *handle = register_status ? 0 : &registered;
}

void bcm_ndis_deregister(bcm_ndis_status *status, bcm_ndis_handle handle)
{
    assert(handle == &registered);
    ++deregistered;
    *status = fail_deregister ? BCM_NDIS_FAILURE : BCM_NDIS_SUCCESS;
}

void bcm_ndis_terminate(bcm_ndis_handle handle, void *specific)
{
    assert(handle == &initialized && !specific);
    ++terminated;
}

int main(void)
{
    assert(offsetof(struct bcm_mac_characteristics, open) == 6);
    assert(offsetof(struct bcm_mac_characteristics, name) == 6 + 10 * sizeof(void *));
    assert(bcm_mac_start() == BCM_NDIS_SUCCESS);
    assert(bcm_mac_start() == BCM_NDIS_SUCCESS && initialized == 1 && registered == 1);
    fail_deregister = 1;
    bcm_mac_stop();
    assert(deregistered == 1 && terminated == 0);
    fail_deregister = 0;
    bcm_mac_stop();
    bcm_mac_stop();
    assert(deregistered == 2 && terminated == 1);
    register_status = BCM_NDIS_FAILURE;
    assert(bcm_mac_start() == BCM_NDIS_FAILURE);
    assert(initialized == 2 && registered == 2 && terminated == 2);
    fail_init = 1;
    assert(bcm_mac_start() == BCM_NDIS_FAILURE);
    assert(initialized == 3 && registered == 2 && terminated == 2);
    bcm_mac_stop();
    puts("NDIS tests: packed callback layout, registration lifecycle, failure cleanup passed");
    return 0;
}
