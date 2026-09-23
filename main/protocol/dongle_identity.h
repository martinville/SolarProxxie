#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GHOST_IDENTITY_MESSAGE_LENGTH 43

typedef struct {
    char serial[13];
    char register_key[9];
} ghost_dongle_identity_t;

typedef struct {
    uint8_t window[GHOST_IDENTITY_MESSAGE_LENGTH];
    size_t used;
    uint32_t next_seq;
    bool active;
} ghost_identity_stream_t;

bool ghost_identity_parse(const uint8_t *message, size_t length, ghost_dongle_identity_t *out);
bool ghost_identity_feed(ghost_identity_stream_t *stream, uint32_t seq, const uint8_t *data,
                         size_t length, ghost_dongle_identity_t *out);
