#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GHOST_CLOUD_FRAME_MAX 768

typedef enum {
    GHOST_CLOUD_ROLE_UNKNOWN,
    GHOST_CLOUD_ROLE_TELEMETRY,
    GHOST_CLOUD_ROLE_REDIRECT,
} ghost_cloud_role_t;

bool ghost_cloud_dns_name(const uint8_t *question, size_t length, size_t *question_end,
                          ghost_cloud_role_t *role);
size_t ghost_cloud_response(const uint8_t *request, size_t length, uint8_t *response,
                            size_t capacity, const uint8_t clock[6], ghost_cloud_role_t role);
