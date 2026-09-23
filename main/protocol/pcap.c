#include "pcap.h"
#include <string.h>
static void le32(uint8_t *p, uint32_t v) {
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}
void ghost_pcap_header(uint8_t out[24], uint32_t snaplen) {
    memset(out, 0, 24);
    le32(out, 0xa1b2c3d4);
    out[4] = 2;
    out[6] = 4;
    le32(out + 16, snaplen);
    le32(out + 20, 101);
}
void ghost_pcap_record(uint8_t out[16], uint64_t ms, uint32_t captured, uint32_t wire) {
    le32(out, ms / 1000);
    le32(out + 4, (ms % 1000) * 1000);
    le32(out + 8, captured);
    le32(out + 12, wire);
}
