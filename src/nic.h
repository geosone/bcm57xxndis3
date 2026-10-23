/* The BCM5722 behind the NDIS MAC: bring-up in AddAdapter, the interrupt
 * handler and DPC, link polling, and teardown. One adapter is supported. */
#ifndef BCM57_NIC_H
#define BCM57_NIC_H

#include "ndis3.h"

/* MacAddAdapter: finds the card, resets it, allocates the rings, starts it,
 * registers the adapter and its shared interrupt. Without a card: logs and
 * returns BCM_NDIS_ADAPTER_NOT_FOUND. */
bcm_ndis_status bcm_nic_add(bcm_ndis_handle mac, bcm_ndis_handle configuration,
                            struct bcm_ndis_string *name);

/* MacRemoveAdapter: stops the chip, releases interrupt, adapter and memory. */
void bcm_nic_remove(void);

/* System_Exit: stops interrupts and DMA without calling NDIS. */
void bcm_nic_shutdown(void);

#endif
