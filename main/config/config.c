#include "config.h"
#ifdef ESP_PLATFORM
#include "esp_log.h"
#endif
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "security/security.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static ghost_config_t current;
static SemaphoreHandle_t lock;
static nvs_handle_t storage;
const char *ghost_decoder_profile_id(ghost_decoder_profile_t profile) {
    static const char *ids[GHOST_PACKET_PROFILE_COUNT] = {
        "packetoffset001", "packetoffset002", "packetoffset003", "packetoffset004",
        "packetoffset005", "packetoffset006", "packetoffset007", "packetoffset008"};
    return profile >= 1 && profile <= GHOST_PACKET_PROFILE_COUNT ? ids[profile - 1] : "";
}
ghost_decoder_profile_t ghost_decoder_profile_parse(const char *id) {
    if (!id)
        return GHOST_PROFILE_INVALID;
    if (!strcmp(id, "inteless_sp_legacy"))
        return 1;
    if (!strcmp(id, "inteless_sp_newer"))
        return 2;
    if (!strcmp(id, "inteless_sp_captured_306"))
        return 3;
    for (ghost_decoder_profile_t p = 1; p <= GHOST_PACKET_PROFILE_COUNT; p++)
        if (!strcmp(id, ghost_decoder_profile_id(p)))
            return p;
    return GHOST_PROFILE_INVALID;
}
ghost_decoder_profile_t ghost_decoder_profile_from_layout(unsigned layout) {
    return layout == 292   ? GHOST_PROFILE_INTELESS_SP_LEGACY
           : layout == 302 ? GHOST_PROFILE_INTELESS_SP_NEWER
           : layout == 306 ? GHOST_PROFILE_INTELESS_SP_CAPTURED_306
                           : GHOST_PROFILE_INVALID;
}
unsigned ghost_decoder_profile_layout(ghost_decoder_profile_t profile) {
    return profile == GHOST_PROFILE_INTELESS_SP_LEGACY         ? 292
           : profile == GHOST_PROFILE_INTELESS_SP_NEWER        ? 302
           : profile == GHOST_PROFILE_INTELESS_SP_CAPTURED_306 ? 306
                                                               : 0;
}
static bool temperature_field(const ghost_config_t *c, size_t field) {
    return c && field < c->field_count && !strcmp(c->fields[field].device_class, "temperature");
}
double ghost_display_value(const ghost_config_t *c, size_t field, double value) {
    return c && c->imperial_units && temperature_field(c, field) ? value * 9.0 / 5.0 + 32.0 : value;
}
const char *ghost_display_unit(const ghost_config_t *c, size_t field) {
    if (!c || field >= c->field_count)
        return "";
    if (temperature_field(c, field))
        return c->imperial_units ? "\xc2\xb0"
                                   "F"
                                 : "\xc2\xb0"
                                   "C";
    return c->entities[field].unit;
}
void ghost_config_defaults(ghost_config_t *c) {
    memset(c, 0, sizeof(*c));
    c->version = GHOST_CONFIG_VERSION;
    for (unsigned d = 0; d < GHOST_DONGLES_MAX; d++) {
        snprintf(c->dongles[d].name, sizeof(c->dongles[d].name), "INVERTER%u", d + 1);
        c->dongle_layouts[d] = 292;
        c->dongle_profiles[d] = GHOST_PROFILE_INTELESS_SP_LEGACY;
        c->dongle_cloud[d] = true;
    }
    static const uint16_t shipped_layouts[] = {292, 302, 306};
    static const char *shipped_names[] = {"Legacy 292-byte packet", "Newer 302-byte packet",
                                          "Captured 306-byte packet"};
    for (unsigned p = 0; p < 3; p++) {
        c->packet_profile_active[p] = true;
        c->packet_profile_layouts[p] = shipped_layouts[p];
        snprintf(c->packet_profile_names[p], sizeof(c->packet_profile_names[p]), "%s",
                 shipped_names[p]);
    }
    c->dhcp = true;
    strcpy(c->ap_ssid, "SolarProxxie");
    strcpy(c->ap_ip, "192.168.50.1");
    strcpy(c->ap_mask, "255.255.255.0");
    strcpy(c->mask, "255.255.255.0");
    strcpy(c->base, "solarproxxie");
    strcpy(c->discovery_prefix, "homeassistant");
    strcpy(c->client, "solarproxxie");
    c->cloud = true;
    c->retain = true;
    c->discovery = true;
    c->on_change = true;
    c->port = 1883;
    c->keepalive = 60;
    c->min_interval = 5;
    c->max_interval = 60;
    c->stale_seconds = 900;
    c->layout = 292;
    c->log_level = 3;
    c->probe_enabled = true;
    strcpy(c->probe_ip, "1.1.1.1");
#ifdef ESP_PLATFORM
    char mapping_error[160];
    if (!ghost_packet_mapping_defaults(c, mapping_error, sizeof(mapping_error)))
        ESP_LOGE("config", "Shipped packet mapping files are invalid: %s", mapping_error);
#endif
}
esp_err_t ghost_config_init(bool *valid) {
    *valid = false;
    lock = xSemaphoreCreateMutex();
    if (!lock)
        return ESP_ERR_NO_MEM;
    esp_err_t r = nvs_flash_init();
    /* Never erase automatically on version/no-space errors. Physical reset owns erasure. */
    if (r != ESP_OK)
        return r;
    if ((r = nvs_open("ghost", NVS_READWRITE, &storage)) != ESP_OK)
        return r;
    ghost_config_defaults(&current);
    size_t n = 0;
    r = nvs_get_blob(storage, "config", NULL, &n);
    if (r == ESP_ERR_NVS_NOT_FOUND)
        return ESP_OK;
    if (r != ESP_OK || n < sizeof(uint32_t) || n > sizeof(current))
        return ESP_ERR_INVALID_VERSION;
    ghost_config_t *candidate = calloc(1, sizeof(*candidate));
    if (!candidate)
        return ESP_ERR_NO_MEM;
    r = nvs_get_blob(storage, "config", candidate, &n);
    if (r == ESP_OK && ghost_config_migrate(candidate->version, candidate, n, &current) == ESP_OK) {
        *valid = true;
    } else
        r = ESP_ERR_INVALID_VERSION;
    memset(candidate, 0, sizeof(*candidate));
    free(candidate);
    return r;
}
void ghost_config_get(ghost_config_t *c) {
    xSemaphoreTake(lock, portMAX_DELAY);
    *c = current;
    xSemaphoreGive(lock);
}
const ghost_config_t *ghost_config_lock(void) {
    xSemaphoreTake(lock, portMAX_DELAY);
    return &current;
}
void ghost_config_unlock(void) {
    xSemaphoreGive(lock);
}
void ghost_config_probe(bool *enabled, char ip[16]) {
    xSemaphoreTake(lock, portMAX_DELAY);
    *enabled = current.probe_enabled;
    memcpy(ip, current.probe_ip, 16);
    xSemaphoreGive(lock);
}
esp_err_t ghost_config_save(const ghost_config_t *c) {
    char err[100];
    if (!ghost_config_validate(c, err, sizeof(err)))
        return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(lock, portMAX_DELAY);
    esp_err_t r = nvs_set_blob(storage, "config", c, sizeof(*c));
    if (r == ESP_OK)
        r = nvs_commit(storage);
    if (r == ESP_OK)
        current = *c;
    xSemaphoreGive(lock);
    return r;
}
esp_err_t ghost_config_reset(void) {
    return nvs_flash_erase();
}
static bool strfield(const cJSON *j, const char *key, char *out, size_t cap, char *err,
                     unsigned ec) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    if (!v)
        return true;
    if (!cJSON_IsString(v) || strlen(v->valuestring) >= cap) {
        snprintf(err, ec, "Invalid or oversized %s", key);
        return false;
    }
    strcpy(out, v->valuestring);
    return true;
}
bool ghost_config_from_json(ghost_config_t *c, const cJSON *j, bool setup, char *err,
                            unsigned cap) {
    if (!cJSON_IsObject(j)) {
        snprintf(err, cap, "Expected JSON object");
        return false;
    }
    const cJSON *key, *other;
    cJSON_ArrayForEach(key, j) {
        for (other = key->next; other; other = other->next)
            if (!strcmp(key->string, other->string)) {
                snprintf(err, cap, "Duplicate JSON key: %s", key->string);
                return false;
            }
    }
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, "version");
    if (v && (!cJSON_IsNumber(v) || v->valuedouble != GHOST_CONFIG_VERSION)) {
        snprintf(err, cap, "Unsupported configuration version; migration required");
        return false;
    }
    if (cJSON_HasObjectItem(j, "salt") || cJSON_HasObjectItem(j, "hash")) {
        snprintf(err, cap, "Password hash fields cannot be configured");
        return false;
    }
    const char *critical[] = {
        "sta_ssid", "sta_password", "dhcp",        "ip",    "mask",    "gateway", "dns1",
        "dns2",     "ap_ssid",      "ap_password", "ap_ip", "ap_mask", "admin",   "admin_password"};
    if (!setup)
        for (size_t i = 0; i < sizeof(critical) / sizeof(critical[0]); i++)
            if (cJSON_HasObjectItem(j, critical[i])) {
                snprintf(err, cap, "Network and administrator changes require physical Setup Mode");
                return false;
            }
#define STR(k)                                                                                     \
    if (!strfield(j, #k, c->k, sizeof(c->k), err, cap))                                            \
    return false
#define BOOL(k)                                                                                    \
    do {                                                                                           \
        v = cJSON_GetObjectItemCaseSensitive(j, #k);                                               \
        if (v) {                                                                                   \
            if (!cJSON_IsBool(v)) {                                                                \
                snprintf(err, cap, "Invalid " #k);                                                 \
                return false;                                                                      \
            }                                                                                      \
            c->k = cJSON_IsTrue(v);                                                                \
        }                                                                                          \
    } while (0)
#define NUM(k)                                                                                     \
    do {                                                                                           \
        v = cJSON_GetObjectItemCaseSensitive(j, #k);                                               \
        if (v) {                                                                                   \
            if (!cJSON_IsNumber(v) || v->valuedouble < 0 || v->valuedouble > 65535 ||              \
                v->valuedouble != (int)v->valuedouble) {                                           \
                snprintf(err, cap, "Invalid " #k);                                                 \
                return false;                                                                      \
            }                                                                                      \
            c->k = v->valueint;                                                                    \
        }                                                                                          \
    } while (0)
    if (setup) {
        STR(sta_ssid);
        v = cJSON_GetObjectItemCaseSensitive(j, "sta_password");
        if (v && (!cJSON_IsString(v) || (strcmp(v->valuestring, "********") &&
                                         !strfield(j, "sta_password", c->sta_password,
                                                   sizeof(c->sta_password), err, cap))))
            return false;
        BOOL(dhcp);
        STR(ip);
        STR(mask);
        STR(gateway);
        STR(dns1);
        STR(dns2);
        STR(ap_ssid);
        v = cJSON_GetObjectItemCaseSensitive(j, "ap_password");
        if (v && (!cJSON_IsString(v) ||
                  (strcmp(v->valuestring, "********") &&
                   !strfield(j, "ap_password", c->ap_password, sizeof(c->ap_password), err, cap))))
            return false;
        STR(ap_ip);
        STR(ap_mask);
        STR(admin);
        v = cJSON_GetObjectItemCaseSensitive(j, "admin_password");
        if (v && (!cJSON_IsString(v) ||
                  (strcmp(v->valuestring, "********") &&
                   (strlen(v->valuestring) < 12 || strlen(v->valuestring) > 128)))) {
            snprintf(err, cap, "Administrator password must be 12–128 bytes");
            return false;
        }
        if (cJSON_IsString(v) && strcmp(v->valuestring, "********")) {
            esp_fill_random(c->salt, sizeof(c->salt));
            if (!ghost_password_hash(v->valuestring, c->salt, c->hash)) {
                snprintf(err, cap, "Password hashing failed");
                return false;
            }
        }
    }
    BOOL(debug);
    BOOL(mqtt_enabled);
    BOOL(retain);
    BOOL(discovery);
    BOOL(on_change);
    BOOL(imperial_units);
    BOOL(probe_enabled);
    STR(probe_ip);
    STR(broker);
    STR(mqtt_user);
    STR(base);
    STR(client);
    STR(discovery_prefix);
    v = cJSON_GetObjectItemCaseSensitive(j, "mqtt_password");
    if (v && (!cJSON_IsString(v) || strcmp(v->valuestring, "********"))) {
        STR(mqtt_password);
    }
    NUM(port);
    NUM(keepalive);
    NUM(min_interval);
    NUM(max_interval);
    NUM(stale_seconds);
    NUM(layout);
    v = cJSON_GetObjectItemCaseSensitive(j, "log_level");
    if (v) {
        if (!cJSON_IsNumber(v) || v->valuedouble < 1 || v->valuedouble > 5 ||
            v->valuedouble != v->valueint) {
            snprintf(err, cap, "Invalid log level");
            return false;
        }
        c->log_level = v->valueint;
    }
    const cJSON *maps = cJSON_GetObjectItemCaseSensitive(j, "dongles");
    if (maps) {
        if (!cJSON_IsArray(maps) || cJSON_GetArraySize(maps) > GHOST_DONGLES_MAX) {
            snprintf(err, cap, "Too many inverter mappings");
            return false;
        }
        for (unsigned d = 0; d < (unsigned)cJSON_GetArraySize(maps); d++) {
            const cJSON *map = cJSON_GetArrayItem(maps, d);
            if (!cJSON_IsObject(map) ||
                !cJSON_IsString(cJSON_GetObjectItemCaseSensitive(map, "ip")) ||
                !cJSON_IsString(cJSON_GetObjectItemCaseSensitive(map, "name")) ||
                !strfield(map, "ip", c->dongles[d].ip, sizeof(c->dongles[d].ip), err, cap) ||
                !strfield(map, "name", c->dongles[d].name, sizeof(c->dongles[d].name), err, cap)) {
                snprintf(err, cap, "Each dongle requires an IP string and a name");
                return false;
            }
            const cJSON *profile = cJSON_GetObjectItemCaseSensitive(map, "profile");
            if (profile) {
                if (!cJSON_IsString(profile) ||
                    !(c->dongle_profiles[d] = ghost_decoder_profile_parse(profile->valuestring))) {
                    snprintf(err, cap, "Choose a supported decoder profile for each inverter");
                    return false;
                }
                unsigned selected = c->dongle_profiles[d] - 1;
                c->dongle_layouts[d] =
                    c->packet_profile_active[selected] ? c->packet_profile_layouts[selected] : 0;
            }
            const cJSON *layout = cJSON_GetObjectItemCaseSensitive(map, "layout");
            if (layout && !profile) {
                if (!cJSON_IsNumber(layout) || layout->valuedouble != layout->valueint) {
                    snprintf(err, cap, "Choose an active packet-offset setup for each inverter");
                    return false;
                }
                unsigned selected = GHOST_PACKET_PROFILE_COUNT;
                for (unsigned p = 0; p < GHOST_PACKET_PROFILE_COUNT; p++)
                    if (c->packet_profile_active[p] &&
                        c->packet_profile_layouts[p] == layout->valueint) {
                        selected = p;
                        break;
                    }
                if (selected == GHOST_PACKET_PROFILE_COUNT) {
                    snprintf(err, cap, "No active packet-offset setup has that packet length");
                    return false;
                }
                c->dongle_layouts[d] = layout->valueint;
                c->dongle_profiles[d] = selected + 1;
            }
            const cJSON *cloud = cJSON_GetObjectItemCaseSensitive(map, "cloud_forward");
            if (cloud) {
                if (!cJSON_IsBool(cloud)) {
                    snprintf(err, cap, "Invalid cloud forwarding setting");
                    return false;
                }
                c->dongle_cloud[d] = cJSON_IsTrue(cloud);
            }
        }
        for (unsigned d = cJSON_GetArraySize(maps); d < GHOST_DONGLES_MAX; d++) {
            c->dongles[d].ip[0] = 0;
            c->dongle_profiles[d] = 1;
            c->dongle_layouts[d] = c->packet_profile_layouts[0];
            c->dongle_cloud[d] = true;
        }
    }
    const cJSON *array = cJSON_GetObjectItemCaseSensitive(j, "entities");
    if (array) {
        if (!cJSON_IsArray(array) || cJSON_GetArraySize(array) > (int)c->field_count) {
            snprintf(err, cap, "Invalid entities");
            return false;
        }
        const cJSON *e;
        uint64_t seen = 0;
        cJSON_ArrayForEach(e, array) {
            const cJSON *id = cJSON_GetObjectItemCaseSensitive(e, "id");
            size_t i;
            for (i = 0; i < c->field_count; i++)
                if (cJSON_IsString(id) && !strcmp(id->valuestring, c->fields[i].id))
                    break;
            if (i == c->field_count || (seen & (UINT64_C(1) << i))) {
                snprintf(err, cap, "Unknown or duplicate datapoint");
                return false;
            }
            seen |= UINT64_C(1) << i;
            ghost_entity_t *d = &c->entities[i];
            if (!strfield(e, "name", d->name, sizeof(d->name), err, cap) ||
                !strfield(e, "ha_name", d->ha_name, sizeof(d->ha_name), err, cap) ||
                !strfield(e, "suffix", d->suffix, sizeof(d->suffix), err, cap) ||
                !strfield(e, "unit", d->unit, sizeof(d->unit), err, cap))
                return false;
            v = cJSON_GetObjectItemCaseSensitive(e, "enabled");
            if (v) {
                if (!cJSON_IsBool(v))
                    return false;
                d->enabled = cJSON_IsTrue(v);
            }
        }
    }
    return ghost_config_validate(c, err, cap);
#undef STR
#undef BOOL
#undef NUM
}
cJSON *ghost_config_header_json(const ghost_config_t *c, bool setup) {
    cJSON *j = cJSON_CreateObject();
    if (!j)
        return NULL;
#define S(k) cJSON_AddStringToObject(j, #k, c->k)
#define B(k) cJSON_AddBoolToObject(j, #k, c->k)
#define N(k) cJSON_AddNumberToObject(j, #k, c->k)
    N(version);
    cJSON *maps = cJSON_AddArrayToObject(j, "dongles");
    for (unsigned d = 0; d < GHOST_DONGLES_MAX; d++) {
        cJSON *map = cJSON_CreateObject();
        cJSON_AddStringToObject(map, "ip", c->dongles[d].ip);
        cJSON_AddStringToObject(map, "name", c->dongles[d].name);
        cJSON_AddStringToObject(map, "profile", ghost_decoder_profile_id(c->dongle_profiles[d]));
        cJSON_AddNumberToObject(map, "layout", c->dongle_layouts[d]);
        cJSON_AddBoolToObject(map, "cloud_forward", c->dongle_cloud[d]);
        cJSON_AddItemToArray(maps, map);
    }
    cJSON *profiles = cJSON_AddArrayToObject(j, "packet_profiles");
    for (unsigned p = 0; p < GHOST_PACKET_PROFILE_COUNT; p++) {
        cJSON *profile = cJSON_CreateObject();
        cJSON_AddNumberToObject(profile, "slot", p + 1);
        cJSON_AddBoolToObject(profile, "active", c->packet_profile_active[p]);
        cJSON_AddStringToObject(profile, "id", ghost_decoder_profile_id(p + 1));
        cJSON_AddStringToObject(profile, "name", c->packet_profile_names[p]);
        cJSON_AddNumberToObject(profile, "layout", c->packet_profile_layouts[p]);
        cJSON_AddItemToArray(profiles, profile);
    }
    B(debug);
    B(mqtt_enabled);
    B(retain);
    B(discovery);
    B(on_change);
    B(imperial_units);
    S(broker);
    S(mqtt_user);
    S(base);
    S(client);
    S(discovery_prefix);
    N(port);
    N(keepalive);
    N(min_interval);
    N(max_interval);
    N(stale_seconds);
    N(layout);
    N(log_level);
    B(probe_enabled);
    S(probe_ip);
    cJSON_AddBoolToObject(j, "mqtt_password_saved", c->mqtt_password[0] != 0);
    if (setup) {
        S(sta_ssid);
        B(dhcp);
        S(ip);
        S(mask);
        S(gateway);
        S(dns1);
        S(dns2);
        S(ap_ssid);
        S(ap_ip);
        S(ap_mask);
        S(admin);
        cJSON_AddBoolToObject(j, "sta_password_saved", c->sta_password[0] != 0);
        cJSON_AddBoolToObject(j, "ap_password_saved", c->ap_password[0] != 0);
    }
    return j;
#undef S
#undef B
#undef N
}
cJSON *ghost_entity_json(const ghost_config_t *c, size_t i) {
    if (i >= c->field_count)
        return NULL;
    const ghost_entity_t *e = &c->entities[i];
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", c->fields[i].id);
    cJSON_AddStringToObject(o, "name", e->name);
    cJSON_AddStringToObject(o, "ha_name", e->ha_name);
    cJSON_AddStringToObject(o, "suffix", e->suffix);
    cJSON_AddStringToObject(o, "unit", e->unit);
    cJSON_AddBoolToObject(o, "enabled", e->enabled);
    return o;
}
cJSON *ghost_config_json(const ghost_config_t *c, bool setup) {
    cJSON *j = ghost_config_header_json(c, setup);
    if (!j)
        return NULL;
    cJSON *a = cJSON_AddArrayToObject(j, "entities");
    for (size_t i = 0; i < c->field_count; i++)
        cJSON_AddItemToArray(a, ghost_entity_json(c, i));
    return j;
}
