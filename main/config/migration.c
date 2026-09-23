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
_Static_assert(offsetof(ghost_config_t, mapping_reserved) == sizeof(ghost_config_v1_t),
               "Version 1 prefix must retain its exact on-flash layout");
_Static_assert(offsetof(ghost_config_t, dongles[4]) == sizeof(ghost_config_v2_t),
               "Version 2 prefix must retain its exact on-flash layout");
_Static_assert(offsetof(ghost_config_t, dongle_cloud) == sizeof(ghost_config_v3_t),
               "Version 3 prefix must retain its exact on-flash layout");
_Static_assert(offsetof(ghost_config_t, dongle_profiles) == sizeof(ghost_config_v4_t),
               "Version 4 prefix must retain its exact on-flash layout");
esp_err_t ghost_config_migrate(uint32_t version, const void *record, size_t size,
                               ghost_config_t *out) {
    if (!record || !out)
        return ESP_ERR_INVALID_ARG;
    if (version == 1 && size == sizeof(ghost_config_v1_t)) {
        ghost_config_defaults(out);
        memcpy(out, record, size);
        out->version = GHOST_CONFIG_VERSION;
        for (size_t i = 0; i < ghost_field_count; i++) {
            char legacy_name[80];
            snprintf(legacy_name, sizeof(legacy_name), "Sunsynk %s", ghost_fields[i].name);
            if (!strncmp(out->entities[i].name, legacy_name, sizeof(out->entities[i].name)))
                snprintf(out->entities[i].name, sizeof(out->entities[i].name), "%s",
                         ghost_fields[i].name);
            if (!strncmp(out->entities[i].ha_name, legacy_name, sizeof(out->entities[i].ha_name)))
                snprintf(out->entities[i].ha_name, sizeof(out->entities[i].ha_name), "%s",
                         ghost_fields[i].name);
        }
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
    /* Older records have zero-filled unused slots. Initialize only appended fields. */
    for (size_t i = 42; i < ghost_field_count; i++) {
        ghost_entity_t empty = {0};
        ghost_entity_t *e = &out->entities[i];
        if (memcmp(e, &empty, sizeof(empty))) continue;
        e->enabled = true;
        snprintf(e->suffix, sizeof(e->suffix), "%s", ghost_fields[i].id);
        snprintf(e->name, sizeof(e->name), "%s", ghost_fields[i].name);
        snprintf(e->ha_name, sizeof(e->ha_name), "%s", ghost_fields[i].name);
        snprintf(e->unit, sizeof(e->unit), "%s", ghost_fields[i].unit);
    }
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
