#pragma once
#include <stdbool.h>
#include <string.h>
/* Include platform socket headers before this file. Compare network-order IPs. */
static inline bool ec_setup_address(const struct sockaddr_storage *address,
                                   socklen_t length, uint32_t ap_ip) {
    if (address->ss_family == AF_INET && length >= sizeof(struct sockaddr_in))
        return ((const struct sockaddr_in *)address)->sin_addr.s_addr == ap_ip;
    if (address->ss_family == AF_INET6 && length >= sizeof(struct sockaddr_in6)) {
        const unsigned char *v = (const unsigned char *)&((const struct sockaddr_in6 *)address)->sin6_addr;
        static const unsigned char mapped[12] = {0,0,0,0,0,0,0,0,0,0,255,255};
        return !memcmp(v,mapped,12) && !memcmp(v+12,&ap_ip,4);
    }
    return false;
}
