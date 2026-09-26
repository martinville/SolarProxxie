#include "protocol.h"
#include <stdatomic.h>
#include <string.h>

/* state:2 | first offset:10 | second offset:10. Atomic words let an upload take
 * effect without pausing the decoder or exposing a half-written offset pair. */
static atomic_uint_least32_t uploaded[GHOST_PACKET_PROFILE_COUNT][GHOST_FIELDS_MAX];
void ghost_packet_mappings_apply(
    const ghost_field_t fields[GHOST_FIELDS_MAX], size_t field_count,
    const ghost_packet_mapping_t mappings[GHOST_PACKET_PROFILE_COUNT][GHOST_FIELDS_MAX]) {
    ghost_field_count = 0;
    memset(ghost_fields, 0, sizeof(ghost_fields));
    if (field_count > GHOST_FIELDS_MAX)
        field_count = GHOST_FIELDS_MAX;
    memcpy(ghost_fields, fields, field_count * sizeof(*fields));
    for (unsigned p = 0; p < GHOST_PACKET_PROFILE_COUNT; p++)
        for (size_t i = 0; i < GHOST_FIELDS_MAX; i++) {
            const ghost_packet_mapping_t *m = &mappings[p][i];
            uint32_t packed = (m->state & 3U) | ((uint32_t)(m->position[0] & 1023U) << 2) |
                              ((uint32_t)(m->position[1] & 1023U) << 12);
            atomic_store(&uploaded[p][i], packed);
        }
    ghost_field_count = field_count;
}

static bool value(const ghost_values_t *values, const char *id, double *out) {
    for (size_t i = 0; i < ghost_field_count; i++)
        if (!strcmp(ghost_fields[i].id, id) && (values->valid & (UINT64_C(1) << i))) {
            *out = values->value[i];
            return true;
        }
    return false;
}
static bool plausible(const ghost_values_t *values) {
    double v;
    /* These are deliberately broad bounds for the supported single-phase profiles.
     * They reject shifted/non-telemetry records without treating a normal outage,
     * an absent battery, or legitimate negative battery current as corruption. */
    const char *voltages[] = {"grid_voltage", "load_voltage"};
    for (size_t i = 0; i < sizeof(voltages) / sizeof(voltages[0]); i++)
        if (value(values, voltages[i], &v) && (v < 0 || v > 600))
            return false;
    if (value(values, "battery_soc", &v) && (v < 0 || v > 100))
        return false;
    const char *frequencies[] = {"grid_frequency", "load_frequency"};
    for (size_t i = 0; i < sizeof(frequencies) / sizeof(frequencies[0]); i++)
        if (value(values, frequencies[i], &v) && (v < 0 || v > 100))
            return false;
    const char *temperatures[] = {"inverter_temperature_dc", "inverter_temperature_ac"};
    for (size_t i = 0; i < sizeof(temperatures) / sizeof(temperatures[0]); i++)
        if (value(values, temperatures[i], &v) && v > 250)
            return false;

    unsigned suspicious = 0;
    for (size_t i = 0; i < sizeof(frequencies) / sizeof(frequencies[0]); i++)
        if (value(values, frequencies[i], &v) && v != 0 && (v < 35 || v > 70))
            suspicious++;
    const char *all_temperatures[] = {"battery_temperature", "inverter_temperature_dc",
                                      "inverter_temperature_ac"};
    for (size_t i = 0; i < sizeof(all_temperatures) / sizeof(all_temperatures[0]); i++)
        if (value(values, all_temperatures[i], &v) && v != -100 && (v < -60 || v > 200))
            suspicious++;
    if (value(values, "grid_connected", &v) && (v < 0 || v > 10))
        suspicious++;
    return suspicious < 2;
}

/* Exact sizes and offsets are reference-backed. No claim of a verified envelope CRC. */
bool ghost_inteless_decode_profile(const uint8_t *p, size_t n, unsigned profile,
                                   ghost_values_t *out) {
    if (!p || !out || profile >= GHOST_PACKET_PROFILE_COUNT || n < 43 || n > GHOST_FRAME_MAX ||
        p[0] != 0xa5)
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
        uint32_t packed = atomic_load(&uploaded[profile][i]);
        unsigned state = packed & 3U;
        if (state != GHOST_PACKET_MAPPING_OFFSET)
            continue;
        uint16_t pos[2] = {(packed >> 2) & 1023U, (packed >> 12) & 1023U};
        if (!f->words || pos[0] < 43 || (size_t)pos[0] + 2 > n ||
            (f->words == 2 && (size_t)pos[1] + 2 > n))
            continue;
        uint16_t low = ghost_be16(p + pos[0]);
        /* A zero temperature word decodes to the protocol bias sentinel -100 C.
         * It means unavailable, not a physical measurement. */
        if (!strcmp(f->device_class, "temperature") && low == 0)
            continue;
        out->value[i] = ghost_number(f, low, f->words == 2 ? ghost_be16(p + pos[1]) : 0);
        out->valid |= UINT64_C(1) << i;
    }
    ghost_derive_values(out);
    return plausible(out);
}
bool ghost_inteless_decode(const uint8_t *p, size_t n, ghost_values_t *out) {
    unsigned profile = n == 292 ? 0 : n == 302 ? 1 : n == 306 ? 2 : GHOST_PACKET_PROFILE_COUNT;
    return ghost_inteless_decode_profile(p, n, profile, out);
}
