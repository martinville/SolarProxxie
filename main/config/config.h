#pragma once
#include "cJSON.h"
#include "esp_err.h"
#include "protocol/protocol.h"
#include <stdbool.h>
#include <stdint.h>
#define GHOST_CONFIG_VERSION 5
#define GHOST_DONGLES_MAX 8

typedef enum {
    GHOST_PROFILE_INVALID = 0,
    GHOST_PROFILE_INTELESS_SP_LEGACY = 1,
    GHOST_PROFILE_INTELESS_SP_NEWER = 2,
    GHOST_PROFILE_INTELESS_SP_CAPTURED_306 = 3,
} ghost_decoder_profile_t;

typedef struct {
    char ip[16], name[25];
} ghost_dongle_t;
typedef struct {
    bool enabled;
    char name[64], suffix[64], ha_name[64], unit[12];
} ghost_entity_t;
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
    uint32_t mapping_reserved; /* Align new fields after the complete v1 record. */
    ghost_dongle_t dongles[GHOST_DONGLES_MAX];
    uint16_t dongle_layouts[GHOST_DONGLES_MAX];
    bool dongle_cloud[GHOST_DONGLES_MAX];
    uint8_t dongle_profiles[GHOST_DONGLES_MAX];
} ghost_config_t;
void ghost_config_defaults(ghost_config_t *c);
esp_err_t ghost_config_init(bool *valid);
void ghost_config_get(ghost_config_t *c);
esp_err_t ghost_config_save(const ghost_config_t *c);
esp_err_t ghost_config_reset(void);
bool ghost_config_validate(const ghost_config_t *c, char *error, unsigned size);
bool ghost_config_from_json(ghost_config_t *c, const cJSON *j, bool physical_setup, char *err,
                            unsigned cap);
cJSON *ghost_config_json(const ghost_config_t *c, bool setup);
cJSON *ghost_config_header_json(const ghost_config_t *c, bool setup);
cJSON *ghost_entity_json(const ghost_config_t *c, size_t i);
void ghost_config_probe(bool *enabled, char ip[16]);
bool ghost_ipv4(const char *s, uint32_t *out);
bool ghost_network_valid(const char *ip, const char *mask, const char *gateway);
bool ghost_topic_valid(const char *s);
esp_err_t ghost_config_migrate(uint32_t version, const void *record, size_t size,
                               ghost_config_t *out);

bool ghost_dongle_name_valid(const char *name);
const char *ghost_field_suffix(const char *suffix);
const char *ghost_decoder_profile_id(ghost_decoder_profile_t profile);
ghost_decoder_profile_t ghost_decoder_profile_parse(const char *id);
ghost_decoder_profile_t ghost_decoder_profile_from_layout(unsigned layout);
unsigned ghost_decoder_profile_layout(ghost_decoder_profile_t profile);
