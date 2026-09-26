#include "config.h"
#include "config_v1.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
/* Frozen version 2 suffix: four IP/name records followed no extra fields. */
typedef struct {
    ghost_config_v1_t base;
    uint32_t mapping_reserved;
    ghost_dongle_t dongles[4];
} ghost_config_v2_t;
typedef struct {
    ghost_config_v1_t base;
    uint32_t mapping_reserved;
    ghost_dongle_t dongles[GHOST_DONGLES_MAX];
    uint16_t dongle_layouts[GHOST_DONGLES_MAX];
} ghost_config_v3_t;
typedef struct {
    ghost_config_v1_t base;
    uint32_t mapping_reserved;
    ghost_dongle_t dongles[GHOST_DONGLES_MAX];
    uint16_t dongle_layouts[GHOST_DONGLES_MAX];
    bool dongle_cloud[GHOST_DONGLES_MAX];
} ghost_config_v4_t;
typedef struct {
    ghost_config_v1_t base;
    uint32_t mapping_reserved;
    ghost_dongle_t dongles[GHOST_DONGLES_MAX];
    uint16_t dongle_layouts[GHOST_DONGLES_MAX];
    bool dongle_cloud[GHOST_DONGLES_MAX];
    uint8_t dongle_profiles[GHOST_DONGLES_MAX];
} ghost_config_v5_t;
typedef struct {
    ghost_config_v5_t base;
    bool imperial_units;
} ghost_config_v6_t;
typedef struct {
    ghost_config_v5_t base;
    bool imperial_units;
    ghost_packet_mapping_t packet_mappings[3][GHOST_FIELDS_MAX];
} ghost_config_v7_t;
typedef struct {
    ghost_config_v7_t base;
    uint32_t field_count;
    ghost_field_t fields[GHOST_FIELDS_MAX];
} ghost_config_v8_t;
_Static_assert(offsetof(ghost_config_t, mapping_reserved) == sizeof(ghost_config_v1_t),
               "Version 1 prefix must retain its exact on-flash layout");
_Static_assert(offsetof(ghost_config_t, dongles[4]) == sizeof(ghost_config_v2_t),
               "Version 2 prefix must retain its exact on-flash layout");
_Static_assert(offsetof(ghost_config_t, dongle_cloud) == sizeof(ghost_config_v3_t),
               "Version 3 prefix must retain its exact on-flash layout");
_Static_assert(offsetof(ghost_config_t, dongle_profiles) == sizeof(ghost_config_v4_t),
               "Version 4 prefix must retain its exact on-flash layout");
_Static_assert(offsetof(ghost_config_t, imperial_units) == sizeof(ghost_config_v5_t),
               "Version 5 prefix must retain its exact on-flash layout");
_Static_assert(offsetof(ghost_config_t, packet_mappings) + 2 == sizeof(ghost_config_v6_t),
               "Version 6 record must differ only by its former tail padding");
esp_err_t ghost_config_migrate(uint32_t version, const void *record, size_t size,
                               ghost_config_t *out) {
    if (!record || !out)
        return ESP_ERR_INVALID_ARG;
    if (version == 1 && size == sizeof(ghost_config_v1_t)) {
        ghost_config_defaults(out);
        memcpy(out, record, size);
        out->version = GHOST_CONFIG_VERSION;
    } else if (version == 2 && size == sizeof(ghost_config_v2_t)) {
        ghost_config_defaults(out);
        memcpy(out, record, size);
        out->version = GHOST_CONFIG_VERSION;
        for (unsigned i = 0; i < 4; i++)
            out->dongle_layouts[i] = out->layout;
        for (unsigned i = 4; i < GHOST_DONGLES_MAX; i++)
            out->dongle_layouts[i] = out->layout;
    } else if (version == 3 && size == sizeof(ghost_config_v3_t)) {
        ghost_config_defaults(out);
        memcpy(out, record, size);
        out->version = GHOST_CONFIG_VERSION;
        for (unsigned i = 0; i < GHOST_DONGLES_MAX; i++)
            out->dongle_cloud[i] = out->cloud;
        out->cloud = true;
    } else if (version == 4 && size == sizeof(ghost_config_v4_t)) {
        ghost_config_defaults(out);
        memcpy(out, record, size);
        out->version = GHOST_CONFIG_VERSION;
    } else if (version == 5 && size == sizeof(ghost_config_v5_t)) {
        ghost_config_defaults(out);
        memcpy(out, record, size);
        out->version = GHOST_CONFIG_VERSION;
    } else if (version == 6 && size == sizeof(ghost_config_v6_t)) {
        ghost_config_defaults(out);
        /* V6 ended with two alignment bytes. Do not copy those bytes into the
         * first uploaded mapping in the extended V7 structure. */
        memcpy(out, record, offsetof(ghost_config_t, packet_mappings));
        out->version = GHOST_CONFIG_VERSION;
    } else if (version == 7 && size == sizeof(ghost_config_v7_t)) {
        ghost_config_defaults(out);
        /* Preserve operational settings, but replace the former firmware-owned
         * override table with the current shipped JSON files. */
        memcpy(out, record, offsetof(ghost_config_t, packet_mappings));
        out->version = GHOST_CONFIG_VERSION;
    } else if (version == 8 && size == sizeof(ghost_config_v8_t)) {
        const ghost_config_v8_t *old = record;
        ghost_config_defaults(out);
        memcpy(out, record, offsetof(ghost_config_t, packet_mappings));
        memcpy(out->packet_mappings, old->base.packet_mappings,
               sizeof(old->base.packet_mappings));
        out->field_count = old->field_count;
        memcpy(out->fields, old->fields, sizeof(old->fields));
        out->version = GHOST_CONFIG_VERSION;
    } else if (version == GHOST_CONFIG_VERSION && size == sizeof(*out)) {
        memcpy(out, record, size);
    } else {
        return ESP_ERR_INVALID_VERSION;
    }
    if (version == 1)
        for (unsigned i = 0; i < GHOST_DONGLES_MAX; i++)
            out->dongle_layouts[i] = out->layout;
    if (version == 1 || version == 2)
        for (unsigned i = 0; i < GHOST_DONGLES_MAX; i++)
            out->dongle_cloud[i] = out->cloud;
    if (version < 4)
        out->cloud = true;
    if (version < 5)
        for (unsigned i = 0; i < GHOST_DONGLES_MAX; i++)
            out->dongle_profiles[i] = ghost_decoder_profile_from_layout(out->dongle_layouts[i]);
    char error[100];
    if (!ghost_config_validate(out, error, sizeof(error)))
        return ESP_ERR_INVALID_ARG;
    /* Legacy default names only. Custom SSIDs, broker namespaces and credentials stay intact. */
    if (!strcmp(out->ap_ssid, "ESPGhostNode"))
        strcpy(out->ap_ssid, "SolarProxxie");
    if (!strcmp(out->base, "espghostnode"))
        strcpy(out->base, "solarproxxie");
    if (!strcmp(out->client, "espghostnode"))
        strcpy(out->client, "solarproxxie");
    return ESP_OK;
}
