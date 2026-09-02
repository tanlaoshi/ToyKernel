#ifndef TOY_LWIP_IP_H
#define TOY_LWIP_IP_H

#include "BootTypes.h"
#include "lwip/ip4_addr.h"

static inline void ToyHostIpToLwIp(UINT32 HostStyle, ip4_addr_t *Out) {
    IP4_ADDR(Out,
             (u8_t)((HostStyle >> 24) & 0xFF),
             (u8_t)((HostStyle >> 16) & 0xFF),
             (u8_t)((HostStyle >> 8) & 0xFF),
             (u8_t)(HostStyle & 0xFF));
}

#endif
