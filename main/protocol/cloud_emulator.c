#include "cloud_emulator.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool leap(unsigned year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

bool ghost_cloud_fallback_clock(uint8_t out[6], const char *build_date, const char *build_time,
                                uint64_t uptime_seconds, ghost_cloud_role_t role) {
    if (!out || !build_date || !build_time || strlen(build_date) < 3)
        return false;
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char abbreviation[4] = {build_date[0], build_date[1], build_date[2], 0};
    const char *month = strstr(months, abbreviation);
    unsigned day, year, hour, minute, second;
    if (!month || (size_t)(month - months) % 3 ||
        sscanf(build_date + 3, "%u %u", &day, &year) != 2 ||
        sscanf(build_time, "%u:%u:%u", &hour, &minute, &second) != 3 || day < 1 || day > 31 ||
        year < 2024 || year > 2099 || hour > 23 || minute > 59 || second > 59)
        return false;
    unsigned mon = (unsigned)(month - months) / 3 + 1;
    uint64_t seconds = hour * 3600 + minute * 60 + second + uptime_seconds;
    if (role == GHOST_CLOUD_ROLE_TELEMETRY)
        seconds += 2 * 60 * 60;
    day += (unsigned)(seconds / 86400);
    seconds %= 86400;
    for (;;) {
        static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        unsigned in_month = days[mon - 1] + (mon == 2 && leap(year));
        if (day <= in_month)
            break;
        day -= in_month;
        if (++mon == 13) {
            mon = 1;
            year++;
        }
    }
    out[0] = year % 100;
    out[1] = mon;
    out[2] = day;
    out[3] = seconds / 3600;
    out[4] = seconds / 60 % 60;
    out[5] = seconds % 60;
    return true;
}

static bool name_is(const char *name, size_t length, const char *expected) {
    size_t wanted = strlen(expected);
    if (length != wanted)
        return false;
    for (size_t i = 0; i < length; i++)
        if (tolower((unsigned char)name[i]) != expected[i])
            return false;
    return true;
}

bool ghost_cloud_dns_name(const uint8_t *question, size_t length, size_t *question_end,
                          ghost_cloud_role_t *role) {
    if (!question || length < 17 || !question_end)
        return false;
    char name[64];
    size_t source = 12, used = 0;
    unsigned labels = 0;
    while (source < length && question[source]) {
        size_t label = question[source++];
        if (!label || label > 63 || label > length - source || ++labels > 8)
            return false;
        if (used) {
            if (used == sizeof(name))
                return false;
            name[used++] = '.';
        }
        if (label > sizeof(name) - used)
            return false;
        memcpy(name + used, question + source, label);
        used += label;
        source += label;
    }
    if (source + 5 > length || question[source] != 0)
        return false;
    source++;
    if (question[source] != 0 || question[source + 1] != 1 || question[source + 2] != 0 ||
        question[source + 3] != 1)
        return false;
    *question_end = source + 4;
    if (name_is(name, used, "iot.e-linter.com")) {
        if (role)
            *role = GHOST_CLOUD_ROLE_REDIRECT;
        return true;
    }
    if (name_is(name, used, "ukiot.sunsynk.net") || name_is(name, used, "pv.e-linter.com")) {
        if (role)
            *role = GHOST_CLOUD_ROLE_TELEMETRY;
        return true;
    }
    return false;
}

static const uint8_t poll_map[] = {
    0x31, 0x2e, 0x34, 0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0x14, 0,    0,    0x03, 0,    0x0d, 0x01, 0x01, 0,
    0x03, 0,    0x0f, 0x01, 0,    0,    0x03, 0,    0x10, 0x02, 0x01, 0,    0x03,
    0,    0x14, 0x02, 0x01, 0,    0x03, 0,    0x19, 0x0e, 0x01, 0,    0x03, 0,
    0x28, 0x01, 0x01, 0,    0x03, 0,    0x2b, 0x10, 0x03, 0,    0x03, 0,    0x2e,
    0x02, 0x02, 0,    0x03, 0,    0x30, 0x04, 0,    0,    0x03, 0,    0x36, 0x01,
    0,    0,    0x03, 0,    0x3b, 0x42, 0,    0,    0x03, 0,    0x96, 0x32, 0x01,
    0,    0x03, 0,    0xc8, 0x64, 0,    0,    0x03, 0,    0xcc, 0x01, 0,    0x01,
    0x03, 0,    0xf4, 0x01, 0x01, 0,    0x03, 0x01, 0x2c, 0x10, 0,    0,    0x03,
    0x01, 0x38, 0x08, 0x01, 0,    0x03, 0x01, 0x40, 0x64, 0x01, 0,    0x03, 0x01,
    0xb1, 0x37, 0,    0x01, 0x03, 0x01, 0xeb, 0x01,
};

size_t ghost_cloud_response(const uint8_t *request, size_t length, uint8_t *response,
                            size_t capacity, const uint8_t clock[6], ghost_cloud_role_t role) {
    if (!request || length < 11 || !response || capacity < 11 || request[0] != 0xa5 ||
        request[1] != 0x06 || request[2] != 0x01 || ((size_t)request[9] << 8 | request[10]) + 11 != length)
        return 0;
    uint8_t type = request[3];
    if (type == 0x07 && role == GHOST_CLOUD_ROLE_REDIRECT) {
        static const char target[] = "ukiot.sunsynk.net:51100";
        if (capacity < 11 + sizeof(target) - 1)
            return 0;
        uint8_t header[] = {0xa5, 0x06, 0xa1, 0x07, 0x02, 0, 0, 0, request[8], 0,
                            sizeof(target) - 1};
        memcpy(response, header, sizeof(header));
        memcpy(response + sizeof(header), target, sizeof(target) - 1);
        return sizeof(header) + sizeof(target) - 1;
    }
    if (type == 0x0b) {
        if (capacity < 11 + sizeof(poll_map))
            return 0;
        uint8_t header[] = {0xa5, 0x06, 0xa1, 0x0b, 0x01, 0, 0, 0, request[8], 0,
                            sizeof(poll_map)};
        memcpy(response, header, sizeof(header));
        memcpy(response + sizeof(header), poll_map, sizeof(poll_map));
        return sizeof(header) + sizeof(poll_map);
    }
    if (type == 0x01) {
        if (!clock || capacity < 19)
            return 0;
        uint8_t header[] = {0xa5, 0x06, 0xa1, 0x01, 0x01, 0, 0, 0, request[8], 0, 8};
        memcpy(response, header, sizeof(header));
        memcpy(response + sizeof(header), clock, 6);
        response[17] = 0;
        response[18] = 0x1d;
        return 19;
    }
    if (type != 0x02 && type != 0x04 && type != 0x07 && type != 0x09 && type != 0x0a)
        return 0;
    uint8_t status = type == 0x02 && request[4] == 0x02 ? 0x03 : 0x01;
    uint8_t ack[] = {0xa5, 0x06, 0xa1, type, status, 0, 0, 0, request[8], 0, 0};
    memcpy(response, ack, sizeof(ack));
    return sizeof(ack);
}
