#include "discovery.h"
#include <stdio.h>
#include <string.h>
bool ghost_state_topic(char *out, size_t capacity, const char *base, const char *suffix) {
    if (!out || !capacity || !ghost_topic_valid(base) || !ghost_topic_valid(suffix))
        return false;
    int n = snprintf(out, capacity, "%s/%s", base, suffix);
    return n >= 0 && (size_t)n < capacity;
}
bool ghost_mapped_topic(char *out, size_t capacity, const ghost_config_t *c, unsigned slot,
                        size_t field) {
    if (!c || slot >= GHOST_DONGLES_MAX || field >= ghost_field_count || !c->dongles[slot].ip[0] ||
        !ghost_dongle_name_valid(c->dongles[slot].name) || !ghost_topic_valid(c->base) ||
        !ghost_topic_valid(ghost_field_suffix(c->entities[field].suffix)))
        return false;
    int n = snprintf(out, capacity, "%s/%s/%s", c->base, c->dongles[slot].name,
                     ghost_field_suffix(c->entities[field].suffix));
    return n >= 0 && (size_t)n < capacity;
}
bool ghost_discovery_field_configured(const ghost_config_t *c, unsigned slot, size_t field) {
    return c && slot < GHOST_DONGLES_MAX && field < ghost_field_count &&
           c->dongles[slot].ip[0] && c->entities[field].enabled;
}
cJSON *ghost_discovery_json(const ghost_config_t *c, size_t i, const char *device_id, unsigned slot,
                            const char *version) {
    if (!c || i >= ghost_field_count || !device_id || strlen(device_id) > 23)
        return NULL;
    char state[192], avail[128], fresh[128], uid[100], mapped_id[40];
    const ghost_field_t *f = &ghost_fields[i];
    const ghost_entity_t *e = &c->entities[i];
    if (!ghost_mapped_topic(state, sizeof(state), c, slot, i))
        return NULL;
    snprintf(mapped_id, sizeof(mapped_id), "%s_%u", device_id, slot + 1);
    snprintf(avail, sizeof(avail), "%s/%s/availability", c->base, device_id);
    snprintf(fresh, sizeof(fresh), "%s/%s/inverter_availability", c->base, mapped_id);
    snprintf(uid, sizeof(uid), "%s_%s", mapped_id, f->id);
    cJSON *j = cJSON_CreateObject();
    if (!j)
        return NULL;
    cJSON_AddStringToObject(j, "name", e->ha_name);
    cJSON_AddStringToObject(j, "unique_id", uid);
    cJSON_AddStringToObject(j, "state_topic", state);
    cJSON *a = cJSON_AddArrayToObject(j, "availability"), *x = cJSON_CreateObject();
    cJSON_AddStringToObject(x, "topic", avail);
    cJSON_AddItemToArray(a, x);
    x = cJSON_CreateObject();
    cJSON_AddStringToObject(x, "topic", fresh);
    cJSON_AddItemToArray(a, x);
    cJSON_AddStringToObject(j, "availability_mode", "all");
    if (e->unit[0])
        cJSON_AddStringToObject(j, "unit_of_measurement", e->unit);
    if (!strcmp(e->unit, f->unit)) {
        if (f->device_class[0])
            cJSON_AddStringToObject(j, "device_class", f->device_class);
        if (f->state_class[0])
            cJSON_AddStringToObject(j, "state_class", f->state_class);
    }
    cJSON *d = cJSON_AddObjectToObject(j, "device");
    a = cJSON_AddArrayToObject(d, "identifiers");
    cJSON_AddItemToArray(a, cJSON_CreateString(mapped_id));
    cJSON_AddStringToObject(d, "manufacturer", "SolarProxxie");
    cJSON_AddStringToObject(d, "model", "SolarProxxie Inverter Gateway");
    cJSON_AddStringToObject(d, "name", c->dongles[slot].name);
    cJSON_AddStringToObject(d, "sw_version", version);
    return j;
}
