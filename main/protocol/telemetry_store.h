#pragma once
#include "protocol.h"
#define GHOST_TELEMETRY_SLOTS 8
typedef struct {
    uint32_t ip;
    uint64_t updated;
    uint64_t field_updated[GHOST_FIELDS_MAX];
    unsigned frames;
    bool conflict;
    ghost_values_t values;
} ghost_source_t;
typedef struct {
    ghost_source_t sources[GHOST_TELEMETRY_SLOTS];
} ghost_store_t;
/* Caller serializes access. IP is an opaque, consistently ordered key. */
bool ghost_store_update(ghost_store_t *store, uint32_t ip, const ghost_values_t *values,
                        uint64_t now);
void ghost_store_get(const ghost_store_t *store, uint32_t ip, ghost_source_t *out);
static inline bool ghost_source_fresh(const ghost_source_t *source, uint64_t now,
                                      uint16_t stale_seconds) {
    return source->ip && source->updated && !source->conflict && now >= source->updated &&
           now - source->updated < (uint64_t)stale_seconds * 1000;
}
