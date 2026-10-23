/* WfW NDIS 3.0 full-MAC ABI subset, using 32-bit cdecl callbacks. */
#ifndef BCM57_NDIS3_H
#define BCM57_NDIS3_H

#include <stdint.h>

typedef uint32_t bcm_ndis_status;
typedef void *bcm_ndis_handle;

#define BCM_NDIS_SUCCESS 0UL
#define BCM_NDIS_FAILURE 0xc0000001UL
#define BCM_NDIS_ADAPTER_NOT_FOUND 0xc0010006UL
#define BCM_NDIS_NOT_SUPPORTED 0xc00000bbUL

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
#pragma pack(pop)

/* Fail at compile time if the production compiler changes the wire ABI. */
#ifdef __WATCOMC__
typedef char bcm_ndis_string_size[(sizeof(struct bcm_ndis_string) == 8) ? 1 : -1];
typedef char bcm_mac_size[(sizeof(struct bcm_mac_characteristics) == 54) ? 1 : -1];
#endif

void bcm_ndis_initialize(bcm_ndis_handle *, void *, void *, void *);
void bcm_ndis_register(bcm_ndis_status *, bcm_ndis_handle *, bcm_ndis_handle,
    bcm_ndis_handle, struct bcm_mac_characteristics *, unsigned);
void bcm_ndis_deregister(bcm_ndis_status *, bcm_ndis_handle);
void bcm_ndis_terminate(bcm_ndis_handle, void *);

bcm_ndis_status bcm_mac_start(void);
void bcm_mac_stop(void);
void bcm_log_text(const char *);
void bcm_log_hex(uint32_t);

#endif
