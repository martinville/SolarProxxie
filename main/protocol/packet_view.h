#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool ipv4, fragmented, transport;
    size_t header, offset, length;
} ghost_packet_view_t;

/* Inspect only captured bytes; fragments are never interpreted as transport headers. */
static inline ghost_packet_view_t ghost_packet_view(const uint8_t *p, size_t n) {
    ghost_packet_view_t v = {0};
    if (!p || n < 20 || p[0] >> 4 != 4)
        return v;
    size_t ihl = (p[0] & 15) * 4;
    size_t total = ((size_t)p[2] << 8) | p[3];
    if (ihl < 20 || ihl > n || total < ihl)
        return v;
    v.ipv4 = true;
    v.header = ihl;
    v.fragmented = (p[6] & 0x3f) || p[7];
    if (v.fragmented)
        return v;
    size_t end = total < n ? total : n;
    if (p[9] == 6 && end - ihl >= 20) {
        size_t thl = (p[ihl + 12] >> 4) * 4;
        if (thl < 20 || thl > end - ihl)
            return v;
        v.offset = ihl + thl;
    } else if (p[9] == 17 && end - ihl >= 8) {
        size_t udp = ((size_t)p[ihl + 4] << 8) | p[ihl + 5];
        if (udp < 8 || udp > total - ihl)
            return v;
        if (ihl + udp < end)
            end = ihl + udp;
        v.offset = ihl + 8;
    } else {
        return v;
    }
    v.transport = true;
    v.length = end - v.offset;
    return v;
}
