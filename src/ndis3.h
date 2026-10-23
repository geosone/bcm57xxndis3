/* WfW NDIS 3.0 full-MAC ABI subset, using 32-bit cdecl callbacks.
 * Layouts and argument orders were checked against the WfW 3.11 NDIS.386,
 * NE2000.386, NE3200.386 and a PCI bus-master MAC (docs/ddk-review.md). */
#ifndef BCM57_NDIS3_H
#define BCM57_NDIS3_H

#include <stdint.h>

typedef uint32_t bcm_ndis_status;
typedef void *bcm_ndis_handle;

#define BCM_NDIS_SUCCESS 0UL
#define BCM_NDIS_PENDING 0x00000103UL
#define BCM_NDIS_NOT_RECOGNIZED 0x00010001UL
#define BCM_NDIS_RESET_START 0x40010004UL
#define BCM_NDIS_RESET_END 0x40010005UL
#define BCM_NDIS_FAILURE 0xc0000001UL
#define BCM_NDIS_RESOURCES 0xc000009aUL
#define BCM_NDIS_NOT_SUPPORTED 0xc00000bbUL
#define BCM_NDIS_CLOSING 0xc0010002UL
#define BCM_NDIS_ADAPTER_NOT_FOUND 0xc0010006UL
#define BCM_NDIS_MULTICAST_FULL 0xc0010009UL
#define BCM_NDIS_REQUEST_ABORTED 0xc001000cUL
#define BCM_NDIS_RESET_IN_PROGRESS 0xc001000dUL
#define BCM_NDIS_INVALID_PACKET 0xc001000fUL
#define BCM_NDIS_INVALID_LENGTH 0xc0010014UL
#define BCM_NDIS_INVALID_DATA 0xc0010015UL
#define BCM_NDIS_BUFFER_TOO_SHORT 0xc0010016UL
#define BCM_NDIS_INVALID_OID 0xc0010017UL
#define BCM_NDIS_UNSUPPORTED_MEDIA 0xc0010019UL

#define BCM_NDIS_MEDIUM_802_3 0

#define BCM_NDIS_REQUEST_QUERY 0
#define BCM_NDIS_REQUEST_SET 1

#pragma pack(push, 1)
struct bcm_ndis_string {
    uint16_t length, maximum_length;
    char *buffer;
};
struct bcm_mac_characteristics {
    uint8_t major, minor;
    uint32_t reserved;
    bcm_ndis_status (*open)(bcm_ndis_status *, bcm_ndis_handle *, unsigned *,
        unsigned *, unsigned, bcm_ndis_handle, bcm_ndis_handle, unsigned,
        struct bcm_ndis_string *);
    bcm_ndis_status (*close)(bcm_ndis_handle);
    bcm_ndis_status (*send)(bcm_ndis_handle, void *);
    bcm_ndis_status (*transfer)(bcm_ndis_handle, bcm_ndis_handle, unsigned,
        unsigned, void *, unsigned *);
    bcm_ndis_status (*reset)(bcm_ndis_handle);
    bcm_ndis_status (*request)(bcm_ndis_handle, void *);
    bcm_ndis_status (*statistics)(bcm_ndis_handle, void *);
    void (*unload)(bcm_ndis_handle);
    bcm_ndis_status (*add)(bcm_ndis_handle, bcm_ndis_handle,
        struct bcm_ndis_string *);
    void (*remove)(bcm_ndis_handle);
    struct bcm_ndis_string name;
};
/* MacReserved, type, then the query/set union; NE2000.386 reads the OID at
 * offset 20 and stores BytesWritten/BytesNeeded at 32/36. */
struct bcm_ndis_request {
    uint8_t mac_reserved[16];
    uint32_t type;
    uint32_t oid;
    void *buffer;
    unsigned length, bytes_done, bytes_needed;
};
/* NT 3.1 NDIS_ADAPTER_INFORMATION, byte-packed: 38 bytes with one port
 * descriptor. NE2000.386 zeroes exactly 38 bytes. */
struct bcm_ndis_adapter_information {
    uint32_t dma_channel;
    uint8_t master, dma32;
    void *activate;
    uint32_t adapter_type;
    uint32_t map_registers, maximum_mapping;
    uint32_t port_count;
    uint32_t initial_port, port_length;
    void *port_offset;
};
#pragma pack(pop)

/* Filled by NdisInitializeInterrupt: flags, context, ISR, DPC, VPICD handle,
 * next shared interrupt. */
struct bcm_ndis_interrupt {
    uint32_t opaque[6];
};

/* WfW keeps the VMM time-out handle in the first field and clears it before
 * calling function(0, context). A 0 ms timer runs as a global event. */
struct bcm_ndis_timer {
    void *handle;
    void (*function)(void *, void *);
    void *context;
};

/* Fail at compile time if the production compiler changes the wire ABI. */
#ifdef __WATCOMC__
typedef char bcm_ndis_string_size[(sizeof(struct bcm_ndis_string) == 8) ? 1 : -1];
typedef char bcm_mac_size[(sizeof(struct bcm_mac_characteristics) == 54) ? 1 : -1];
typedef char bcm_request_size[(sizeof(struct bcm_ndis_request) == 40) ? 1 : -1];
typedef char bcm_information_size[
    (sizeof(struct bcm_ndis_adapter_information) == 38) ? 1 : -1];
#endif

/* NDIS.386 services (entry.asm); host tests provide their own. */
void bcm_ndis_initialize(bcm_ndis_handle *, void *, void *, void *);
void bcm_ndis_register(bcm_ndis_status *, bcm_ndis_handle *, bcm_ndis_handle,
    bcm_ndis_handle, struct bcm_mac_characteristics *, unsigned);
void bcm_ndis_deregister(bcm_ndis_status *, bcm_ndis_handle);
void bcm_ndis_terminate(bcm_ndis_handle, void *);
bcm_ndis_status bcm_ndis_register_adapter(bcm_ndis_handle *adapter,
    bcm_ndis_handle mac, bcm_ndis_handle context,
    bcm_ndis_handle configuration, struct bcm_ndis_string *name,
    struct bcm_ndis_adapter_information *);
bcm_ndis_status bcm_ndis_deregister_adapter(bcm_ndis_handle adapter);
void bcm_ndis_initialize_interrupt(bcm_ndis_status *,
    struct bcm_ndis_interrupt *, bcm_ndis_handle adapter,
    int (*isr)(void *), void *context,
    void (*dpc)(void *, void *, void *, void *),
    unsigned vector, unsigned level, int shared, unsigned mode);
void bcm_ndis_remove_interrupt(struct bcm_ndis_interrupt *);
void bcm_ndis_set_timer(struct bcm_ndis_timer *, unsigned milliseconds);
void bcm_ndis_cancel_timer(struct bcm_ndis_timer *, uint8_t *cancelled);
void bcm_ndis_query_packet(void *packet, unsigned *physical,
    unsigned *buffers, void **first, unsigned *length);
void bcm_ndis_query_buffer(void *buffer, void **address, unsigned *length);
void bcm_ndis_next_buffer(void *buffer, void **next);
void bcm_ndis_complete_send(bcm_ndis_handle binding, void *packet,
    bcm_ndis_status);
void bcm_ndis_indicate_receive(bcm_ndis_status *, bcm_ndis_handle binding,
    void *receive_context, void *header, unsigned header_size,
    void *lookahead, unsigned lookahead_size, unsigned packet_size);
void bcm_ndis_indicate_receive_complete(bcm_ndis_handle binding);
void bcm_ndis_indicate_status(bcm_ndis_handle binding, bcm_ndis_status,
    void *buffer, unsigned size);
void bcm_ndis_indicate_status_complete(bcm_ndis_handle binding);

/* Interrupt flag save/restore around state shared with interrupt-time code. */
uint32_t bcm_lock(void);
void bcm_unlock(uint32_t flags);

bcm_ndis_status bcm_mac_start(void);
void bcm_mac_stop(void);
void bcm_log_text(const char *);
void bcm_log_hex(uint32_t);

#endif
