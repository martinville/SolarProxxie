#include "protocol.h"
#include <string.h>

/* Exact sizes and offsets are reference-backed. No claim of a verified envelope CRC. */
bool ghost_inteless_decode(const uint8_t *p, size_t n, ghost_values_t *out) {
    if (!p || !out || (n != 292 && n != 302 && n != 306) || p[0] != 0xa5)
        return false;
    unsigned month = p[38], day = p[39];
    static const unsigned days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    unsigned year = 2000 + p[37];
    if (!month || month > 12 || !day || p[40] > 23 || p[41] > 59 || p[42] > 59)
        return false;
    unsigned maxday =
        days[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    if (day > maxday)
        return false;
    for (unsigned i = 11; i < 21; i++)
        if (p[i] < 32 || p[i] > 126)
            return false;
    memset(out, 0, sizeof(*out));
    memcpy(out->serial, p + 11, 10);
    for (size_t i = 0; i < ghost_field_count; i++) {
        const ghost_field_t *f = &ghost_fields[i];
        /* Captured 306-byte records share main measurements, not the BMS layout.
         * Keep unverified BMS values unavailable instead of reporting false temperatures. */
        if (n == 306 && !strncmp(f->id, "bms_", 4))
            continue;
        const uint16_t *pos = n == 292                   ? f->pos292
                              : n == 306 && f->pos306[0] ? f->pos306
                                                         : f->pos302;
        if (!f->words || pos[0] < 43 || (size_t)pos[0] + 2 > n ||
            (f->words == 2 && (size_t)pos[1] + 2 > n))
            continue;
        out->value[i] =
            ghost_number(f, ghost_be16(p + pos[0]), f->words == 2 ? ghost_be16(p + pos[1]) : 0);
        out->valid |= UINT64_C(1) << i;
    }
    ghost_derive_values(out);
    return true;
}
