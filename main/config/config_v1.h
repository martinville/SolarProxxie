#pragma once
#include "config.h"
/* Frozen on-flash version 1 layout; do not edit. */
typedef struct {
    uint32_t version;
    char sta_ssid[33], sta_password[65];
    bool dhcp;
    char ip[16], mask[16], gateway[16], dns1[16], dns2[16];
    char ap_ssid[33], ap_password[65], ap_ip[16], ap_mask[16];
    char admin[33];
    uint8_t salt[16], hash[32];
    bool cloud, debug, mqtt_enabled, retain, discovery, on_change;
    char broker[128], mqtt_user[65], mqtt_password[129], base[64], client[64], discovery_prefix[64];
    uint16_t port, keepalive, min_interval, max_interval, stale_seconds, layout;
    uint8_t log_level;
    bool probe_enabled;
    char probe_ip[16];
    ghost_entity_t entities[GHOST_FIELDS_MAX];
} ghost_config_v1_t;
