#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GHOST_FIELDS_MAX 64
#define GHOST_FRAME_MAX 512
#define GHOST_PACKET_PROFILE_COUNT 8
typedef enum {
    GHOST_PACKET_MAPPING_UNMAPPED = 0,
    GHOST_PACKET_MAPPING_OFFSET = 1,
    GHOST_PACKET_MAPPING_DISABLED = 2,
} ghost_packet_mapping_state_t;
typedef struct {
    uint8_t state;
    uint16_t position[2];
} ghost_packet_mapping_t;
typedef enum { GHOST_U16, GHOST_I16, GHOST_U32, GHOST_I32, GHOST_BITS, GHOST_ENUM } ghost_type_t;
typedef struct {
    char id[40], device_class[20], state_class[24];
    int16_t reg[2];
    int8_t sum_fields[8];
    uint8_t sum_count, sum_subtract;
    uint8_t words;
    ghost_type_t type;
    double scale, offset;
    double minimum;
    bool has_minimum;
    uint32_t mask;
} ghost_field_t;
extern ghost_field_t ghost_fields[GHOST_FIELDS_MAX];
extern size_t ghost_field_count;
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
bool ghost_inteless_decode_profile(const uint8_t *p, size_t n, unsigned profile,
                                   ghost_values_t *out);
void ghost_packet_mappings_apply(
    const ghost_field_t fields[GHOST_FIELDS_MAX], size_t field_count,
    const ghost_packet_mapping_t mappings[GHOST_PACKET_PROFILE_COUNT][GHOST_FIELDS_MAX]);
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
