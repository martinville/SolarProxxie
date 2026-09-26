#include "telemetry_store.h"
#include <string.h>
bool ghost_store_update(ghost_store_t *store, uint32_t ip, const ghost_values_t *values,
                        uint64_t now) {
    if (!ip || !values || !values->valid)
        return false;
    ghost_source_t *target = NULL, *oldest = &store->sources[0];
    for (unsigned i = 0; i < GHOST_TELEMETRY_SLOTS; i++) {
        ghost_source_t *s = &store->sources[i];
        if (s->ip == ip) {
            target = s;
            break;
        }
        if (s->updated < oldest->updated)
            oldest = s;
    }
    if (!target) {
        target = oldest;
        memset(target, 0, sizeof(*target));
        target->ip = ip;
    }
    if (target->updated && strcmp(target->values.serial, values->serial)) {
        target->conflict = true;
        return false;
    }
    if (target->conflict)
        return false;
    target->values = *values;
    target->updated = now;
    for (size_t i = 0; i < GHOST_FIELDS_MAX; i++)
        if (values->valid & (UINT64_C(1) << i))
            target->field_updated[i] = now;
    target->frames++;
    return true;
}
void ghost_store_get(const ghost_store_t *store, uint32_t ip, ghost_source_t *out) {
    memset(out, 0, sizeof(*out));
    if (!ip)
        return;
    for (unsigned i = 0; i < GHOST_TELEMETRY_SLOTS; i++)
        if (store->sources[i].ip == ip) {
            *out = store->sources[i];
            return;
        }
}
