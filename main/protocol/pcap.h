#pragma once
#include <stdint.h>
void ghost_pcap_header(uint8_t out[24], uint32_t snaplen);
void ghost_pcap_record(uint8_t out[16], uint64_t milliseconds, uint32_t captured, uint32_t wire);
