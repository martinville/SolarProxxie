#include "config.h"
#include <stdio.h>
#include <string.h>
bool ghost_ipv4(const char *s, uint32_t *out) {
    if (!s || !*s)
        return false;
    uint32_t ip = 0;
    for (int part = 0; part < 4; part++) {
        unsigned n = 0, digits = 0;
        while (*s >= '0' && *s <= '9') {
            n = n * 10 + (unsigned)(*s++ - '0');
            if (++digits > 3 || n > 255)
                return false;
        }
        if (!digits || (part < 3 ? *s++ != '.' : *s != 0))
            return false;
        ip = (ip << 8) | n;
    }
    if (out)
        *out = ip;
    return true;
}
static bool unicast(uint32_t n) {
    return (n >> 24) != 0 && (n >> 24) != 127 && (n >> 24) < 224 && (n >> 16) != 0xa9fe;
}
bool ghost_network_valid(const char *ip, const char *mask, const char *gateway) {
    uint32_t a, m, g;
    if (!ghost_ipv4(ip, &a) || !ghost_ipv4(mask, &m) || !ghost_ipv4(gateway, &g))
        return false;
    uint32_t h = ~m;
    return m && h >= 3 && (h & (h + 1)) == 0 && unicast(a) && unicast(g) && (a & h) &&
           (a & h) != h && (g & h) && (g & h) != h && (a & m) == (g & m);
}
bool ghost_topic_valid(const char *s) {
    if (!s || !*s || *s == '/' || s[strlen(s) - 1] == '/')
        return false;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') ||
              *s == '_' || *s == '-' || *s == '/'))
            return false;
    return true;
}
bool ghost_dongle_name_valid(const char *s) {
    if (!s || !*s || strlen(s) > 24 || !((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z')))
        return false;
    for (; *s; s++)
        if (!((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') ||
              *s == '_' || *s == '-'))
            return false;
    return true;
}
const char *ghost_field_suffix(const char *suffix) {
    if (!strncmp(suffix, "sunsynk/", 8) || !strncmp(suffix, "inverter/", 9))
        return strchr(suffix, '/') + 1;
    return suffix;
}
bool ghost_config_validate(const ghost_config_t *c, char *error, unsigned size) {
#define FAIL(s)                                                                                    \
    do {                                                                                           \
        snprintf(error, size, "%s", s);                                                            \
        return false;                                                                              \
    } while (0)
    if (!c || !error || !size)
        return false;
#define TERMINATED(k)                                                                              \
    if (!memchr(c->k, 0, sizeof(c->k)))                                                            \
    FAIL("Unterminated configuration field")
    TERMINATED(sta_ssid);
    TERMINATED(sta_password);
    TERMINATED(ip);
    TERMINATED(mask);
    TERMINATED(gateway);
    TERMINATED(dns1);
    TERMINATED(dns2);
    TERMINATED(ap_ssid);
    TERMINATED(ap_password);
    TERMINATED(ap_ip);
    TERMINATED(ap_mask);
    TERMINATED(admin);
    TERMINATED(broker);
    TERMINATED(mqtt_user);
    TERMINATED(mqtt_password);
    TERMINATED(base);
    TERMINATED(client);
    TERMINATED(discovery_prefix);
    TERMINATED(probe_ip);
    for (size_t i = 0; i < GHOST_FIELDS_MAX; i++) {
        const ghost_entity_t *e = &c->entities[i];
        if (!memchr(e->name, 0, sizeof(e->name)) || !memchr(e->ha_name, 0, sizeof(e->ha_name)) ||
            !memchr(e->suffix, 0, sizeof(e->suffix)) || !memchr(e->unit, 0, sizeof(e->unit)))
            FAIL("Unterminated datapoint field");
    }
#undef TERMINATED
    uint32_t probe;
    if (!ghost_ipv4(c->probe_ip, &probe) || !unicast(probe))
        FAIL("Invalid reachability probe IP");
    if (c->version != GHOST_CONFIG_VERSION)
        FAIL("Unsupported configuration version");
    if (!c->sta_ssid[0] || !c->ap_ssid[0] || strlen(c->ap_password) < 8 ||
        strlen(c->ap_password) > 63)
        FAIL("Wi-Fi SSIDs and an AP password of 8–63 bytes are required");
    if (c->sta_password[0] && (strlen(c->sta_password) < 8 || strlen(c->sta_password) > 63))
        FAIL("Home Wi-Fi password must be empty or 8–63 bytes");
    if (!c->admin[0])
        FAIL("Administrator username is required");
    if (!ghost_network_valid(c->ap_ip, c->ap_mask, c->ap_ip))
        FAIL("Invalid AP subnet");
    uint32_t a, m, st, sm, d;
    ghost_ipv4(c->ap_ip, &a);
    ghost_ipv4(c->ap_mask, &m);
    if ((~m) < 63 || (~m) > 65535 || (a & ~m) != 1)
        FAIL("AP subnet must be /16 through /26 with gateway at host 1");
    for (unsigned i = 0; i < GHOST_DONGLES_MAX; i++) {
        const ghost_dongle_t *map = &c->dongles[i];
        uint32_t mapped;
        if (!memchr(map->ip, 0, sizeof(map->ip)) || !memchr(map->name, 0, sizeof(map->name)) ||
            !ghost_dongle_name_valid(map->name))
            FAIL("Dongle names: 1-24 letters, digits, underscore or hyphen; start with a letter");
        unsigned profile_layout =
            ghost_decoder_profile_layout((ghost_decoder_profile_t)c->dongle_profiles[i]);
        if (!profile_layout || c->dongle_layouts[i] != profile_layout)
            FAIL("Select a supported decoder profile for each inverter");
        if (!map->ip[0])
            continue;
        if (!ghost_ipv4(map->ip, &mapped) || !unicast(mapped) || mapped == a ||
            (mapped & m) != (a & m) || !(mapped & ~m) || (mapped & ~m) == ~m)
            FAIL("Dongle IP must be a client address on the SolarProxxie AP subnet");
        char canonical[16];
        snprintf(canonical, sizeof(canonical), "%u.%u.%u.%u", (unsigned)(mapped >> 24),
                 (unsigned)((mapped >> 16) & 255), (unsigned)((mapped >> 8) & 255),
                 (unsigned)(mapped & 255));
        if (strcmp(map->ip, canonical))
            FAIL("Dongle IP addresses must not contain leading zeroes");
        for (unsigned k = 0; k < i; k++) {
            uint32_t other;
            if (c->dongles[k].ip[0] && ghost_ipv4(c->dongles[k].ip, &other) &&
                (mapped == other || !strcmp(map->name, c->dongles[k].name)))
                FAIL("Active dongle IPs and names must be unique");
        }
    }
    if (!c->dhcp) {
        if (!ghost_network_valid(c->ip, c->mask, c->gateway) || !strcmp(c->ip, c->gateway))
            FAIL("Invalid static IP, mask or gateway");
        ghost_ipv4(c->ip, &st);
        ghost_ipv4(c->mask, &sm);
        uint32_t common = m & sm;
        if ((st & common) == (a & common))
            FAIL("STA and AP subnets overlap");
        if (!ghost_ipv4(c->dns1, &d) || !unicast(d) || !strcmp(c->dns1, c->ap_ip))
            FAIL("Invalid primary DNS");
        if (c->dns2[0] && (!ghost_ipv4(c->dns2, &d) || !unicast(d) || !strcmp(c->dns2, c->ap_ip)))
            FAIL("Invalid secondary DNS");
    }
    if (c->layout != 292 && c->layout != 302 && c->layout != 306)
        FAIL("Select a 292, 302 or 306 byte Inteless layout");
    if (!c->port || c->keepalive < 15 || c->keepalive > 3600 || c->min_interval < 1 ||
        c->max_interval < c->min_interval || c->stale_seconds < 30 || c->log_level > 5)
        FAIL("Invalid operational intervals or log level");
    if (!ghost_topic_valid(c->base) || !ghost_topic_valid(c->client) || strchr(c->client, '/') ||
        !ghost_topic_valid(c->discovery_prefix))
        FAIL("Invalid MQTT topic or client ID");
    if (c->mqtt_enabled && !c->broker[0])
        FAIL("MQTT broker is required");
    for (const char *p = c->broker; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
              *p == '.' || *p == '-'))
            FAIL("Broker must be a hostname or IPv4 address, without a URL scheme");
    for (size_t i = 0; i < ghost_field_count; i++) {
        if (!ghost_topic_valid(c->entities[i].suffix) ||
            !ghost_topic_valid(ghost_field_suffix(c->entities[i].suffix)))
            FAIL("Invalid datapoint topic suffix");
        for (size_t j = 0; j < i; j++)
            if (!strcmp(ghost_field_suffix(c->entities[i].suffix),
                        ghost_field_suffix(c->entities[j].suffix)))
                FAIL("Duplicate datapoint topic suffix");
    }
    return true;
#undef FAIL
}
