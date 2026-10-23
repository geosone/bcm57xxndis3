#include "ndis3.h"

static bcm_ndis_handle wrapper_handle;
static bcm_ndis_handle mac_handle;
static char mac_name[] = "BCM57DBG";
/* No EISA or MCA IDs; PCI discovery is handled by our hardware layer. */
static uint16_t adapter_ids[2];

static bcm_ndis_status no_open(bcm_ndis_status *error, bcm_ndis_handle *binding,
    unsigned *selected, unsigned *media, unsigned count, bcm_ndis_handle ndis,
    bcm_ndis_handle adapter, unsigned options, struct bcm_ndis_string *address)
{
    (void)media; (void)count; (void)ndis; (void)adapter; (void)options;
    (void)address;
    *error = BCM_NDIS_ADAPTER_NOT_FOUND;
    *binding = 0;
    *selected = 0;
    return BCM_NDIS_ADAPTER_NOT_FOUND;
}

static bcm_ndis_status no_close(bcm_ndis_handle binding)
{
    (void)binding;
    return BCM_NDIS_ADAPTER_NOT_FOUND;
}

static bcm_ndis_status no_packet(bcm_ndis_handle binding, void *packet)
{
    (void)binding; (void)packet;
    return BCM_NDIS_NOT_SUPPORTED;
}

static bcm_ndis_status no_transfer(bcm_ndis_handle binding,
    bcm_ndis_handle receive, unsigned offset, unsigned length, void *packet,
    unsigned *transferred)
{
    (void)binding; (void)receive; (void)offset; (void)length; (void)packet;
    *transferred = 0;
    return BCM_NDIS_NOT_SUPPORTED;
}

static void no_unload(bcm_ndis_handle context)
{
    (void)context;
}

static bcm_ndis_status no_adapter(bcm_ndis_handle context,
    bcm_ndis_handle configuration, struct bcm_ndis_string *name)
{
    (void)context; (void)configuration; (void)name;
    bcm_log_text("BCM57DBG: AddAdapter: hardware path not enabled\r\n");
    return BCM_NDIS_ADAPTER_NOT_FOUND;
}

static struct bcm_mac_characteristics characteristics = {
    3, 0, 0, no_open, no_close, no_packet, no_transfer, no_close,
    no_packet, no_packet, no_unload, no_adapter, no_unload,
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
    bcm_ndis_register(&status, &mac_handle, wrapper_handle, 0,
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
