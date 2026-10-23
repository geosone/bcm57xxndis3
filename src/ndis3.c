#include "ndis3.h"
#include "adapter.h"
#include "nic.h"

static bcm_ndis_handle wrapper_handle;
static bcm_ndis_handle mac_handle;
static char mac_name[] = "BCM57DBG";
/* No EISA or MCA IDs; PCI discovery is handled by our hardware layer. */
static uint16_t adapter_ids[2];

/* MacAdapterContext is the struct bcm_adapter given to NdisRegisterAdapter;
 * MacBindingHandle is a struct bcm_binding. */
static bcm_ndis_status mac_open(bcm_ndis_status *error,
    bcm_ndis_handle *binding, unsigned *selected, unsigned *media,
    unsigned count, bcm_ndis_handle ndis, bcm_ndis_handle adapter,
    unsigned options, struct bcm_ndis_string *address)
{
    (void)options; (void)address;
    return bcm_adapter_open(adapter, error, binding, selected, media, count,
                            ndis);
}

static bcm_ndis_status mac_close(bcm_ndis_handle binding)
{
    return bcm_adapter_close(binding);
}

static bcm_ndis_status mac_send(bcm_ndis_handle binding, void *packet)
{
    return bcm_adapter_send(binding, packet);
}

static bcm_ndis_status mac_transfer(bcm_ndis_handle binding,
    bcm_ndis_handle receive, unsigned offset, unsigned length, void *packet,
    unsigned *transferred)
{
    return bcm_adapter_transfer(binding, receive, offset, length, packet,
                                transferred);
}

static bcm_ndis_status mac_reset(bcm_ndis_handle binding)
{
    return bcm_adapter_reset(binding);
}

static bcm_ndis_status mac_request(bcm_ndis_handle binding, void *request)
{
    return bcm_adapter_request(binding, request);
}

static bcm_ndis_status mac_statistics(bcm_ndis_handle adapter, void *request)
{
    return bcm_adapter_query_global(adapter, request);
}

static void mac_unload(bcm_ndis_handle context)
{
    (void)context;
}

/* NDIS stores the MAC handle through our MacMacContext before it calls
 * AddAdapter, as NE2000.386 relies on. */
static bcm_ndis_status mac_add(bcm_ndis_handle context,
    bcm_ndis_handle configuration, struct bcm_ndis_string *name)
{
    return bcm_nic_add(*(bcm_ndis_handle *)context, configuration, name);
}

static void mac_remove(bcm_ndis_handle adapter)
{
    (void)adapter;
    bcm_nic_remove();
}

static struct bcm_mac_characteristics characteristics = {
    3, 0, 0, mac_open, mac_close, mac_send, mac_transfer, mac_reset,
    mac_request, mac_statistics, mac_unload, mac_add, mac_remove,
    {8, 9, mac_name}
};

bcm_ndis_status bcm_mac_start(void)
{
    bcm_ndis_status status = BCM_NDIS_FAILURE;
    if (mac_handle)
        return BCM_NDIS_SUCCESS;
    if (wrapper_handle)
        return BCM_NDIS_FAILURE;
    bcm_ndis_initialize(&wrapper_handle, adapter_ids, 0, 0);
    if (!wrapper_handle)
        return BCM_NDIS_FAILURE;
    bcm_ndis_register(&status, &mac_handle, wrapper_handle, &mac_handle,
        &characteristics, sizeof(characteristics));
    if (status != BCM_NDIS_SUCCESS) {
        mac_handle = 0;
        bcm_ndis_terminate(wrapper_handle, 0);
        wrapper_handle = 0;
    }
    return status;
}

void bcm_mac_stop(void)
{
    bcm_ndis_status status = BCM_NDIS_FAILURE;
    if (mac_handle) {
        bcm_ndis_deregister(&status, mac_handle);
        if (status != BCM_NDIS_SUCCESS) {
            bcm_log_text("BCM57DBG: NdisDeregisterMac failed: ");
            bcm_log_hex(status);
            bcm_log_text("\r\n");
            return; /* Keep the wrapper while NDIS still references this MAC. */
        }
        mac_handle = 0;
    }
    if (wrapper_handle) {
        bcm_ndis_terminate(wrapper_handle, 0);
        wrapper_handle = 0;
    }
}
