#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GHOST_FIELDS_MAX 64
#define GHOST_FRAME_MAX 306
typedef enum { GHOST_U16, GHOST_I16, GHOST_U32, GHOST_I32, GHOST_BITS, GHOST_ENUM } ghost_type_t;
typedef struct {
    const char *id, *name, *unit, *device_class, *state_class;
    int16_t reg[2];
    uint16_t pos292[2], pos302[2];
    uint8_t words;
    ghost_type_t type;
    double scale, offset;
    uint32_t mask;
    const char *evidence;
    const char *sum_of; /* Space-separated earlier IDs; -ID subtracts. NULL for direct fields. */
    uint16_t pos306[2]; /* Optional 306-byte-only offsets; zero falls back to pos302. */
} ghost_field_t;
extern const ghost_field_t ghost_fields[];
extern const size_t ghost_field_count;
typedef struct {
    double value[GHOST_FIELDS_MAX];
    uint64_t valid;
    char serial[11];
} ghost_values_t;
uint16_t ghost_be16(const uint8_t *p);
uint16_t ghost_crc16(const uint8_t *p, size_t n);
double ghost_number(const ghost_field_t *f, uint16_t low, uint16_t high);
void ghost_derive_values(ghost_values_t *values);
bool ghost_inteless_decode(const uint8_t *p, size_t n, ghost_values_t *out);
bool ghost_modbus_response(const uint8_t *p, size_t n, uint16_t start, ghost_values_t *out);
typedef struct {
    uint8_t bytes[2 * GHOST_FRAME_MAX];
    size_t used;
    uint32_t next_seq;
    bool active;
    uint16_t frame_length;
} ghost_stream_t;
typedef void (*ghost_frame_fn)(const uint8_t *, size_t, void *);
void ghost_stream_feed(ghost_stream_t *s, uint32_t seq, const uint8_t *p, size_t n,
                       ghost_frame_fn emit, void *context);
