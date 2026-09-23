#pragma once
#include "config/config.h"
cJSON *ghost_discovery_json(const ghost_config_t *c, size_t i, const char *device_id, unsigned slot,
                            const char *version);
bool ghost_state_topic(char *out, size_t capacity, const char *base, const char *suffix);
bool ghost_mapped_topic(char *out, size_t capacity, const ghost_config_t *c, unsigned slot,
                        size_t field);
bool ghost_discovery_field_configured(const ghost_config_t *c, unsigned slot, size_t field);
