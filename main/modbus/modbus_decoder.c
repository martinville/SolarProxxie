#include "protocol/protocol.h"
#include <string.h>
uint16_t ghost_be16(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | p[1];
}
void ghost_derive_values(ghost_values_t *values) {
    for (size_t i = 0; i < ghost_field_count; i++) {
        const char *next = ghost_fields[i].sum_of;
        if (!next || !*next)
            continue;
        values->valid &= ~(UINT64_C(1) << i);
        double sum = 0;
        bool all = true;
        while (*next) {
            bool subtract = *next == '-';
            if (subtract) next++;
            const char *end = strchr(next, ' ');
            size_t n = end ? (size_t)(end - next) : strlen(next);
            size_t j;
            for (j = 0; j < i; j++)
                if (strlen(ghost_fields[j].id) == n && !memcmp(ghost_fields[j].id, next, n))
                    break;
            if (j == i || !(values->valid & (UINT64_C(1) << j))) {
                all = false;
                break;
            }
            sum += subtract ? -values->value[j] : values->value[j];
            if (!end)
                break;
            next = end + 1;
        }
        if (all) {
            if (!strcmp(ghost_fields[i].id, "home_load_power") && sum < 0)
                sum = 0;
            values->value[i] = sum * ghost_fields[i].scale + ghost_fields[i].offset;
            values->valid |= UINT64_C(1) << i;
        }
    }
}
uint16_t ghost_crc16(const uint8_t *p, size_t n) {
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (unsigned b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xa001 : 0);
    }
    return crc;
}
double ghost_number(const ghost_field_t *f, uint16_t low, uint16_t high) {
    uint32_t raw = low | ((uint32_t)high << 16);
    double value;
    switch (f->type) {
    case GHOST_I16:
        value = (low & 0x8000) ? (double)low - 65536 : low;
        break;
    case GHOST_I32:
        value = (raw & 0x80000000) ? (double)raw - 4294967296.0 : raw;
        break;
    case GHOST_U32:
        value = raw;
        break;
    case GHOST_BITS:
        value = raw & f->mask;
        break;
    default:
        value = low;
        break;
    }
    return value * f->scale + f->offset;
}
bool ghost_modbus_response(const uint8_t *p, size_t n, uint16_t start, ghost_values_t *out) {
    if (!p || !out || n < 7 || n > 255 || p[0] == 0 || p[0] > 247 || (p[1] != 3 && p[1] != 4) ||
        !p[2] || (p[2] & 1) || n != (size_t)p[2] + 5 || ghost_crc16(p, n) != 0 ||
        (uint32_t)start + p[2] / 2 > 65536)
        return false;
    memset(out, 0, sizeof(*out));
    for (size_t i = 0; i < ghost_field_count; i++) {
        const ghost_field_t *f = &ghost_fields[i];
        uint16_t words[2] = {0};
        bool found = f->words > 0;
        for (unsigned j = 0; j < f->words; j++) {
            int r = f->reg[j] - start;
            if (f->reg[j] < 0 || r < 0 || r >= p[2] / 2) {
                found = false;
                break;
            }
            words[j] = ghost_be16(p + 3 + 2 * r);
        }
        if (found) {
            out->value[i] = ghost_number(f, words[0], words[1]);
            out->valid |= UINT64_C(1) << i;
        }
    }
    ghost_derive_values(out);
    return true;
}
