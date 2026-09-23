#include "config/config.h"
#include "config/config_v1.h"
#include "mqtt/discovery.h"
#include "nvs.h"
#include "protocol/packet_view.h"
#include "protocol/pcap.h"
#include "protocol/dongle_identity.h"
#include "protocol/cloud_emulator.h"
#include "protocol/telemetry_store.h"
#include "security/security.h"
#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern int test_fail_commit;
static unsigned frames;
static void emit(const uint8_t *p, size_t n, void *arg) {
    ghost_values_t v;
    assert(ghost_inteless_decode(p, n, &v));
    frames++;
}
static void sample(uint8_t *p, size_t n) {
    memset(p, 0, n);
    p[0] = 0xa5;
    memcpy(p + 11, "TEST000001", 10);
    p[37] = 24;
    p[38] = 2;
    p[39] = 29;
    p[40] = 12;
    p[41] = 30;
}
static void cloud_emulator_tests(void) {
    const uint8_t query[] = {0x12, 0x34, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0,
                             5,    'u',  'k', 'i', 'o', 't', 7, 's', 'u', 'n', 's', 'y',
                             'n',  'k',  3,   'n', 'e', 't', 0, 0, 1, 0, 1};
    size_t end = 0;
    ghost_cloud_role_t role = GHOST_CLOUD_ROLE_UNKNOWN;
    assert(ghost_cloud_dns_name(query, sizeof(query), &end, &role) && end == sizeof(query));
    assert(role == GHOST_CLOUD_ROLE_TELEMETRY);
    uint8_t bad[sizeof(query)];
    memcpy(bad, query, sizeof(bad));
    bad[13] = 'x';
    assert(!ghost_cloud_dns_name(bad, sizeof(bad), &end, &role));

    uint8_t request[306] = {0xa5, 0x06, 0x01, 0x0b, 2, 0xb0, 0, 0, 8, 0, 112};
    uint8_t response[GHOST_CLOUD_FRAME_MAX];
    uint8_t clock[] = {0x26, 0x09, 0x23, 0x12, 0x34, 0x56, 0};
    size_t n = ghost_cloud_response(request, 123, response, sizeof(response), clock,
                                    GHOST_CLOUD_ROLE_TELEMETRY);
    assert(n == 148 && response[3] == 0x0b && response[8] == 8 && response[9] == 0 &&
           response[10] == 137 && !memcmp(response + 11, "1.4", 3));
    memset(request, 0, sizeof(request));
    memcpy(request, (uint8_t[]){0xa5, 0x06, 0x01, 0x01, 0, 0xb0, 0, 0, 4, 0, 32}, 11);
    n = ghost_cloud_response(request, 43, response, sizeof(response), clock,
                             GHOST_CLOUD_ROLE_TELEMETRY);
    assert(n == 19 && response[8] == 4 && !memcmp(response + 11, clock, sizeof(clock)) &&
           response[17] == 0 && response[18] == 0x1d);
    request[3] = 0x07;
    request[9] = request[10] = 0;
    n = ghost_cloud_response(request, 11, response, sizeof(response), clock,
                             GHOST_CLOUD_ROLE_REDIRECT);
    assert(n == 34 && response[4] == 2 && response[8] == 4 &&
           !memcmp(response + 11, "ukiot.sunsynk.net:51100", 23));
    request[3] = 0x09;
    request[9] = 1;
    request[10] = 39;
    n = ghost_cloud_response(request, 306, response, sizeof(response), clock,
                             GHOST_CLOUD_ROLE_TELEMETRY);
    assert(n == 11 && response[3] == 0x09 && response[8] == 4);
    assert(!ghost_cloud_response(request, 305, response, sizeof(response), clock,
                                 GHOST_CLOUD_ROLE_TELEMETRY));
    request[3] = 0x04;
    request[9] = request[10] = 0;
    n = ghost_cloud_response(request, 11, response, sizeof(response), clock,
                             GHOST_CLOUD_ROLE_TELEMETRY);
    assert(n == 11 && response[0] == 0xa5 && response[2] == 0xa1 && response[3] == 0x04 &&
           response[4] == 0x01 && response[8] == 4 && response[9] == 0 && response[10] == 0);
    puts("PASS offline cloud emulator: DNS allowlist and captured protocol replies");
}
static void identity_tests(void) {
    uint8_t message[GHOST_IDENTITY_MESSAGE_LENGTH] = {0xa5, 0x06, 0x01, 0x01};
    message[10] = 0x20;
    memcpy(message + 11, "DONGLE123456", 12);
    memcpy(message + 27, "KEY12345", 8);
    ghost_dongle_identity_t identity = {0};
    assert(ghost_identity_parse(message, sizeof(message), &identity));
    assert(!strcmp(identity.serial, "DONGLE123456"));
    assert(!strcmp(identity.register_key, "KEY12345"));
    assert(!ghost_identity_parse(message, sizeof(message) - 1, &identity));
    message[35] = 1;
    assert(!ghost_identity_parse(message, sizeof(message), &identity));
    message[35] = 0;
    ghost_identity_stream_t stream = {0};
    assert(!ghost_identity_feed(&stream, 100, message, 18, &identity));
    assert(ghost_identity_feed(&stream, 118, message + 18, sizeof(message) - 18, &identity));
    assert(!strcmp(identity.register_key, "KEY12345"));
    assert(!ghost_identity_feed(&stream, 100, message, sizeof(message), &identity));
    memset(&stream, 0, sizeof(stream));
    uint8_t coalesced[2 + GHOST_IDENTITY_MESSAGE_LENGTH + 3] = {0};
    memcpy(coalesced + 2, message, sizeof(message));
    assert(ghost_identity_feed(&stream, 500, coalesced, sizeof(coalesced), &identity));
    memset(&stream, 0, sizeof(stream));
    assert(!ghost_identity_feed(&stream, 10, message, 18, &identity));
    assert(!ghost_identity_feed(&stream, 40, message + 18, sizeof(message) - 18, &identity));
    puts("PASS dongle identity: strict packet, split TCP, duplicate, gap and coalescing");
}
static size_t field(const char *id) {
    for (size_t i = 0; i < ghost_field_count; i++)
        if (!strcmp(id, ghost_fields[i].id))
            return i;
    abort();
}
static void protocol_tests(void) {
    uint8_t p[604];
    ghost_values_t v;
    sample(p, 292);
    size_t soc = field("battery_soc"), temp = field("battery_temperature"),
           grid = field("grid_voltage");
    p[244] = 0;
    p[245] = 54;
    p[240] = 4;
    p[241] = 186;
    p[176] = 9;
    p[177] = 29;
    assert(ghost_inteless_decode(p, 292, &v));
    assert(v.value[soc] == 54);
    assert(fabs(v.value[temp] - 21) < 1e-9);
    assert(fabs(v.value[grid] - 233.3) < 1e-9);
    for (size_t n = 0; n < 292; n++)
        assert(!ghost_inteless_decode(p, n, &v));
    p[38] = 13;
    assert(!ghost_inteless_decode(p, 292, &v));
    p[38] = 2;
    p[37] = 23;
    assert(!ghost_inteless_decode(p, 292, &v));
    p[37] = 24;
    ghost_stream_t stream = {.frame_length = 292};
    frames = 0;
    ghost_stream_feed(&stream, 100, p, 140, emit, NULL);
    assert(!frames);
    ghost_stream_feed(&stream, 240, p + 140, 152, emit, NULL);
    assert(frames == 1);
    ghost_stream_feed(&stream, 100, p, 292, emit, NULL);
    assert(frames == 1);
    sample(p, 302);
    memset(&stream, 0, sizeof(stream));
    stream.frame_length = 302;
    frames = 0;
    ghost_stream_feed(&stream, 100, p, 292, emit, NULL);
    assert(!frames);
    ghost_stream_feed(&stream, 392, p + 292, 10, emit, NULL);
    assert(frames == 1);
    memcpy(p + 302, p, 302);
    memset(&stream, 0, sizeof(stream));
    stream.frame_length = 302;
    frames = 0;
    ghost_stream_feed(&stream, 1, p, 604, emit, NULL);
    assert(frames == 2);
    ghost_field_t f = {.type = GHOST_I16, .scale = .1, .offset = -10};
    assert(ghost_number(&f, 55536, 0) == -1010);
    f.type = GHOST_U16;
    assert(ghost_number(&f, 65535, 0) == 6543.5);
    f.type = GHOST_I32;
    f.scale = 1;
    f.offset = 0;
    assert(ghost_number(&f, 0xffff, 0xffff) == -1);
    f.type = GHOST_U32;
    assert(ghost_number(&f, 0xffff, 0xffff) == 4294967295.0);
    f.type = GHOST_BITS;
    f.mask = 3;
    assert(ghost_number(&f, 0xffff, 0) == 3);
    uint8_t modbus[] = {1, 3, 2, 0, 54, 0, 0};
    uint16_t crc = ghost_crc16(modbus, 5);
    modbus[5] = crc;
    modbus[6] = crc >> 8;
    assert(ghost_modbus_response(modbus, 7, 184, &v));
    assert(v.value[soc] == 54);
    modbus[5] ^= 1;
    assert(!ghost_modbus_response(modbus, 7, 184, &v));
    /* Deterministic malformed-input stress, independent lengths and random bytes. */
    uint32_t random = 7;
    for (unsigned t = 0; t < 50000; t++) {
        for (unsigned k = 0; k < sizeof(p); k++) {
            random = random * 1664525 + 1013904223;
            p[k] = random >> 24;
        }
        size_t n = random % sizeof(p);
        ghost_inteless_decode(p, n, &v);
        ghost_modbus_response(p, n, 65500, &v);
    }
    puts("PASS protocol: truncation, dates, signed/scaled/32-bit/bitfield, CRC, TCP splitting, "
         "duplicates, 50000 malformed inputs");
}
static void config_tests(void) {
    uint32_t ip;
    assert(ghost_ipv4("192.168.50.1", &ip) && ip == 0xc0a83201);
    const char *bad[] = {"",         "1.2.3",  "256.1.2.3",          "1.2.3.4x",
                         "-1.2.3.4", "1..2.3", "1.2.3.999999999999", "1.2.3.4.5"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(*bad); i++)
        assert(!ghost_ipv4(bad[i], &ip));
    assert(ghost_network_valid("192.168.1.5", "255.255.255.0", "192.168.1.1"));
    assert(!ghost_network_valid("192.168.1.255", "255.255.255.0", "192.168.1.1"));
    assert(!ghost_network_valid("192.168.1.2", "255.0.255.0", "192.168.1.1"));
    bool valid;
    assert(ghost_config_init(&valid) == ESP_OK && !valid);
    ghost_config_t c;
    ghost_config_get(&c);
    char err[160];
    const char *setup =
        "{\"sta_ssid\":\"TestWifi\",\"sta_password\":\"test-password\",\"ap_password\":\"test-ap-"
        "password\",\"admin\":\"admin\",\"admin_password\":\"test-admin-password\"}";
    cJSON *j = cJSON_Parse(setup);
    assert(ghost_config_from_json(&c, j, true, err, sizeof(err)));
    cJSON_Delete(j);
    assert(ghost_config_save(&c) == ESP_OK);
    assert(ghost_config_init(&valid) == ESP_OK && valid);
    const char *critical[] = {
        "sta_ssid", "sta_password", "dhcp",        "ip",    "mask",    "gateway", "dns1",
        "dns2",     "ap_ssid",      "ap_password", "ap_ip", "ap_mask", "admin",   "admin_password"};
    for (unsigned i = 0; i < sizeof(critical) / sizeof(*critical); i++) {
        j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, critical[i], "attack");
        assert(!ghost_config_from_json(&c, j, false, err, sizeof(err)));
        cJSON_Delete(j);
    }
    j = cJSON_Parse("{\"version\":999}");
    assert(!ghost_config_from_json(&c, j, false, err, sizeof(err)));
    cJSON_Delete(j);
    assert(cJSON_Parse("[[[[[[[[[[[[[0]]]]]]]]]]]]]") == NULL);
    ghost_config_t migrated;
    assert(ghost_config_migrate(GHOST_CONFIG_VERSION, &c, sizeof(c), &migrated) == ESP_OK);
    assert(ghost_config_migrate(99, &c, sizeof(c), &migrated) == ESP_ERR_INVALID_VERSION);
    assert(ghost_config_migrate(GHOST_CONFIG_VERSION, &c, sizeof(c) - 1, &migrated) ==
           ESP_ERR_INVALID_VERSION);
    j = ghost_config_json(&c, false);
    char *out = cJSON_PrintUnformatted(j);
    assert(!strstr(out, "test-password") && !strstr(out, "test-admin-password") &&
           !strstr(out, "test-ap-password"));
    assert(!cJSON_HasObjectItem(j, "hash") && !cJSON_HasObjectItem(j, "salt"));
    assert(ghost_config_from_json(&c, j, false, err, sizeof(err)));
    free(out);
    cJSON_Delete(j);
    strcpy(c.mqtt_password, "keep-me");
    j = cJSON_Parse("{\"mqtt_password\":\"********\"}");
    assert(ghost_config_from_json(&c, j, false, err, sizeof(err)));
    assert(!strcmp(c.mqtt_password, "keep-me"));
    cJSON_Delete(j);
    char sta_password[sizeof(c.sta_password)], ap_password[sizeof(c.ap_password)];
    uint8_t salt[sizeof(c.salt)], hash[sizeof(c.hash)];
    memcpy(sta_password, c.sta_password, sizeof(sta_password));
    memcpy(ap_password, c.ap_password, sizeof(ap_password));
    memcpy(salt, c.salt, sizeof(salt));
    memcpy(hash, c.hash, sizeof(hash));
    j = cJSON_Parse("{\"sta_password\":\"********\",\"ap_password\":\"********\"," 
                    "\"admin_password\":\"********\"}");
    assert(ghost_config_from_json(&c, j, true, err, sizeof(err)));
    assert(!memcmp(c.sta_password, sta_password, sizeof(sta_password)) &&
           !memcmp(c.ap_password, ap_password, sizeof(ap_password)) &&
           !memcmp(c.salt, salt, sizeof(salt)) && !memcmp(c.hash, hash, sizeof(hash)));
    cJSON_Delete(j);
    j = cJSON_Parse("{\"salt\":\"forged\"}");
    assert(!ghost_config_from_json(&c, j, true, err, sizeof(err)));
    cJSON_Delete(j);
    c.dhcp = false;
    strcpy(c.ip, "192.168.50.2");
    strcpy(c.mask, "255.255.255.0");
    strcpy(c.gateway, "192.168.50.254");
    strcpy(c.dns1, "192.168.50.254");
    assert(!ghost_config_validate(&c, err, sizeof(err)));
    c.dhcp = true;
    test_fail_commit = 1;
    c.dongle_cloud[0] = false;
    assert(ghost_config_save(&c) != ESP_OK);
    ghost_config_get(&c);
    assert(c.dongle_cloud[0]);
    test_fail_commit = 0;
    memset(c.sta_ssid, 'X', sizeof(c.sta_ssid));
    assert(!ghost_config_validate(&c, err, sizeof(err)));
    puts("PASS config: IPv4/subnets, critical-field restrictions, persistence, failed saves, "
         "version rejection, secret-free export, masked-secret preservation");
}
static void security_tests(void) {
    uint8_t salt[16] = {0}, hash[32], legacy[32];
    assert(ghost_password_hash_iterations("test-password", salt,
                                          GHOST_LEGACY_KDF_ITERATIONS, legacy));
    const uint8_t expected[32] = {0x84, 0xa8, 0x45, 0x2b, 0x8e, 0xc8, 0x03, 0x98, 0x5e, 0xaa, 0x34,
                                  0x33, 0x3f, 0xd7, 0x3f, 0x82, 0x0a, 0xfe, 0x3b, 0x26, 0xc7, 0x8c,
                                  0xaf, 0x82, 0xfb, 0xb6, 0x83, 0xab, 0xba, 0x62, 0x42, 0x06};
    assert(!memcmp(legacy, expected, 32));
    assert(ghost_password_verify_iterations("test-password", salt, legacy,
                                            GHOST_LEGACY_KDF_ITERATIONS));
    assert(ghost_password_hash("test-password", salt, hash));
    assert(ghost_password_verify("test-password", salt, hash));
    assert(!ghost_password_verify("wrong-password", salt, hash));
    salt[0] = 1;
    assert(!ghost_password_verify("test-password", salt, hash));
    puts("PASS security: legacy PBKDF2 compatibility, fast verification and salt binding");
}
static void mqtt_capture_tests(void) {
    ghost_config_t c;
    ghost_config_get(&c);
    strcpy(c.dongles[0].ip, "192.168.50.2");
    size_t i = field("battery_soc");
    assert(ghost_discovery_field_configured(&c, 0, i));
    c.entities[i].enabled = false;
    assert(!ghost_discovery_field_configured(&c, 0, i));
    c.entities[i].enabled = true;
    assert(!ghost_discovery_field_configured(&c, GHOST_DONGLES_MAX, i));
    cJSON *j = ghost_discovery_json(&c, i, "ghost_0123456789ab", 0, "1.0.0");
    assert(j);
    assert(!strcmp(cJSON_GetObjectItem(j, "unique_id")->valuestring,
                   "ghost_0123456789ab_1_battery_soc"));
    assert(cJSON_GetArraySize(cJSON_GetObjectItem(j, "availability")) == 2);
    cJSON_Delete(j);
    strcpy(c.entities[i].ha_name, "Garage Inverter Battery");
    j = ghost_discovery_json(&c, i, "ghost_0123456789ab", 0, "1.0.0");
    assert(!strcmp(cJSON_GetObjectItem(j, "unique_id")->valuestring,
                   "ghost_0123456789ab_1_battery_soc"));
    cJSON_Delete(j);
    char topic[160];
    assert(ghost_state_topic(topic, sizeof(topic), "solarproxxie", "inverter/battery_soc"));
    assert(!strcmp(topic, "solarproxxie/inverter/battery_soc"));
    assert(!ghost_state_topic(topic, 4, "solarproxxie", "battery_soc"));
    assert(!ghost_state_topic(topic, sizeof(topic), "bad/#", "x"));
    uint8_t h[24], r[16];
    ghost_pcap_header(h, 640);
    assert(h[0] == 0xd4 && h[20] == 101 && h[16] == 0x80 && h[17] == 2);
    ghost_pcap_record(r, 1234, 292, 332);
    assert(r[0] == 1 && r[8] == 0x24 && r[9] == 1 && r[12] == 0x4c);
    puts("PASS MQTT discovery/topic stability and standard PCAP headers");
}
static void packet_view_tests(void) {
    uint8_t p[100] = {0};
    p[0] = 0x45;
    p[3] = 60;
    p[9] = 6;
    p[32] = 0x50;
    ghost_packet_view_t v = ghost_packet_view(p, 60);
    assert(v.transport && v.offset == 40 && v.length == 20);
    v = ghost_packet_view(p, 48);
    assert(v.transport && v.length == 8);
    v = ghost_packet_view(p, 80);
    assert(v.length == 20); /* Ignore bytes beyond IP total length. */
    p[6] = 0x20;
    v = ghost_packet_view(p, 60);
    assert(v.fragmented && !v.transport);
    p[6] = 0;
    p[7] = 1;
    assert(!ghost_packet_view(p, 60).transport);
    p[7] = 0;
    p[32] = 0x40;
    assert(!ghost_packet_view(p, 60).transport);
    p[32] = 0xf0;
    assert(!ghost_packet_view(p, 60).transport);
    p[9] = 17;
    p[25] = 12;
    v = ghost_packet_view(p, 60);
    assert(v.transport && v.offset == 28 && v.length == 4);
    p[25] = 7;
    assert(!ghost_packet_view(p, 60).transport);
    p[25] = 50;
    assert(!ghost_packet_view(p, 60).transport);
    p[25] = 12;
    p[3] = 10;
    assert(!ghost_packet_view(p, 60).ipv4);
    p[3] = 60;
    for (size_t n = 0; n < 28; n++)
        assert(!ghost_packet_view(p, n).transport);
    assert(!ghost_packet_view(NULL, 100).ipv4);
    puts("PASS packet inspector: TCP/UDP offsets, truncation, fragments, malformed headers and "
         "padding");
}
static void multi_dongle_tests(void) {
    ghost_config_t c;
    ghost_config_get(&c);
    char err[160];
    strcpy(c.dongles[0].ip, "192.168.50.2");
    strcpy(c.dongles[1].ip, "192.168.50.3");
    assert(ghost_config_validate(&c, err, sizeof(err)));
    strcpy(c.dongles[1].name, "INVERTER1");
    assert(!ghost_config_validate(&c, err, sizeof(err)));
    strcpy(c.dongles[1].name, "INVERTER 2");
    assert(!ghost_config_validate(&c, err, sizeof(err)));
    strcpy(c.dongles[1].name, "INVERTER2");
    strcpy(c.dongles[1].ip, "192.168.50.2");
    assert(!ghost_config_validate(&c, err, sizeof(err)));
    strcpy(c.dongles[1].ip, "192.168.1.2");
    assert(!ghost_config_validate(&c, err, sizeof(err)));
    strcpy(c.dongles[1].ip, "192.168.50.3");
    c.dongle_profiles[1] = GHOST_PROFILE_INTELESS_SP_CAPTURED_306;
    c.dongle_layouts[1] = 306;
    assert(ghost_config_validate(&c, err, sizeof(err)));
    c.dongle_profiles[1] = GHOST_PROFILE_INVALID;
    assert(!ghost_config_validate(&c, err, sizeof(err)));
    c.dongle_profiles[1] = GHOST_PROFILE_INTELESS_SP_CAPTURED_306;
    c.dongle_layouts[1] = 306;
    size_t i = field("battery_soc");
    char topic[192];
    strcpy(c.entities[i].suffix, "sunsynk/battery_soc");
    assert(ghost_mapped_topic(topic, sizeof(topic), &c, 0, i) &&
           !strcmp(topic, "solarproxxie/INVERTER1/battery_soc"));
    assert(ghost_mapped_topic(topic, sizeof(topic), &c, 1, i) &&
           !strcmp(topic, "solarproxxie/INVERTER2/battery_soc"));
    assert(!ghost_mapped_topic(topic, 8, &c, 1, i));
    assert(!ghost_mapped_topic(topic, sizeof(topic), &c, 2, i));
    cJSON *j = ghost_discovery_json(&c, i, "ghost_0123456789ab", 1, "1.0.0");
    assert(j);
    assert(!strcmp(cJSON_GetObjectItem(j, "unique_id")->valuestring,
                   "ghost_0123456789ab_2_battery_soc"));
    cJSON *device = cJSON_GetObjectItem(j, "device");
    assert(!strcmp(cJSON_GetObjectItem(device, "name")->valuestring, "INVERTER2"));
    cJSON_Delete(j);
    strcpy(c.dongles[1].name, "GARAGE");
    j = ghost_discovery_json(&c, i, "ghost_0123456789ab", 1, "1.0.0");
    assert(!strcmp(cJSON_GetObjectItem(j, "unique_id")->valuestring,
                   "ghost_0123456789ab_2_battery_soc"));
    cJSON_Delete(j);
    j = ghost_config_header_json(&c, false);
    assert(cJSON_GetArraySize(cJSON_GetObjectItem(j, "dongles")) == GHOST_DONGLES_MAX);
    assert(cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(j, "dongles"), 1), "layout")
               ->valueint == 306);
    assert(!strcmp(cJSON_GetObjectItem(
                       cJSON_GetArrayItem(cJSON_GetObjectItem(j, "dongles"), 1), "profile")
                       ->valuestring,
                   "inteless_sp_captured_306"));
    assert(cJSON_IsTrue(cJSON_GetObjectItem(
        cJSON_GetArrayItem(cJSON_GetObjectItem(j, "dongles"), 1), "cloud_forward")));
    assert(ghost_config_from_json(&c, j, false, err, sizeof(err)));
    assert(c.dongle_profiles[0] == GHOST_PROFILE_INTELESS_SP_LEGACY &&
           c.dongle_profiles[1] == GHOST_PROFILE_INTELESS_SP_CAPTURED_306 &&
           c.dongle_layouts[1] == 306 && c.dongle_cloud[1]);
    cJSON_Delete(j);
    c.version = 2;
    c.layout = 302;
    ghost_config_t migrated;
    assert(ghost_config_migrate(2, &c, offsetof(ghost_config_t, dongles[4]), &migrated) ==
           ESP_OK);
    assert(migrated.version == GHOST_CONFIG_VERSION && migrated.dongle_layouts[0] == 302 &&
           migrated.dongle_profiles[0] == GHOST_PROFILE_INTELESS_SP_NEWER &&
           migrated.dongle_layouts[1] == 302 && !strcmp(migrated.dongles[1].name, "GARAGE") &&
           !migrated.dongles[4].ip[0] && migrated.dongle_cloud[0] && migrated.dongle_cloud[7]);
    c.version = 3;
    c.cloud = false;
    assert(ghost_config_migrate(3, &c, offsetof(ghost_config_t, dongle_cloud), &migrated) ==
           ESP_OK);
    assert(!migrated.dongle_cloud[0] && !migrated.dongle_cloud[7] && migrated.cloud);
    c.version = 4;
    c.dongle_layouts[0] = 306;
    c.dongle_cloud[0] = false;
    assert(ghost_config_migrate(4, &c, offsetof(ghost_config_t, dongle_profiles), &migrated) ==
           ESP_OK);
    assert(migrated.dongle_profiles[0] == GHOST_PROFILE_INTELESS_SP_CAPTURED_306 &&
           migrated.dongle_layouts[0] == 306 && !migrated.dongle_cloud[0]);
    c.dongle_profiles[0] = GHOST_PROFILE_INTELESS_SP_LEGACY;
    c.dongle_layouts[0] = 292;
    c.version = GHOST_CONFIG_VERSION;
    c.layout = 292;
    ghost_config_v1_t legacy;
    memcpy(&legacy, &c, sizeof(legacy));
    legacy.version = 1;
    assert(ghost_config_migrate(1, &legacy, sizeof(legacy), &migrated) == ESP_OK);
    assert(migrated.version == GHOST_CONFIG_VERSION && !strcmp(migrated.ap_password, c.ap_password));
    assert(!memcmp(migrated.hash, c.hash, sizeof(c.hash)));
    assert(!migrated.dongles[0].ip[0] && !strcmp(migrated.dongles[0].name, "INVERTER1"));
    nvs_handle_t storage;
    nvs_open("ghost", NVS_READWRITE, &storage);
    nvs_set_blob(storage, "config", &legacy, sizeof(legacy));
    bool valid;
    assert(ghost_config_init(&valid) == ESP_OK && valid);
    ghost_config_get(&migrated);
    assert(!strcmp(migrated.sta_ssid, c.sta_ssid));
    assert(ghost_config_save(&c) == ESP_OK);
    assert(ghost_config_init(&valid) == ESP_OK && valid);
    ghost_config_get(&migrated);
    assert(!strcmp(migrated.dongles[1].name, "GARAGE"));
    ghost_store_t store = {0};
    ghost_values_t v = {.valid = 1};
    strcpy(v.serial, "SERIAL0001");
    v.value[0] = 10;
    assert(ghost_store_update(&store, 1, &v, 100));
    strcpy(v.serial, "SERIAL0002");
    v.value[0] = 20;
    assert(ghost_store_update(&store, 2, &v, 200));
    ghost_source_t source;
    ghost_store_get(&store, 1, &source);
    assert(source.values.value[0] == 10 && source.updated == 100);
    ghost_store_get(&store, 2, &source);
    assert(source.values.value[0] == 20 && source.updated == 200);
    strcpy(v.serial, "SERIAL0003");
    assert(!ghost_store_update(&store, 1, &v, 300));
    ghost_store_get(&store, 1, &source);
    assert(source.conflict && source.updated == 100 && source.values.value[0] == 10);
    ghost_store_get(&store, 0, &source);
    assert(!source.updated);
    ghost_store_get(&store, 3, &source);
    assert(!source.values.valid);
    memset(&store, 0, sizeof(store));
    for (unsigned d = 1; d <= GHOST_DONGLES_MAX; d++) {
        snprintf(v.serial, sizeof(v.serial), "INV%07u", d);
        v.value[0] = d * 10;
        assert(ghost_store_update(&store, d, &v, d * 1000));
    }
    for (unsigned d = 1; d <= GHOST_DONGLES_MAX; d++) {
        ghost_store_get(&store, d, &source);
        assert(source.values.value[0] == d * 10 && source.frames == 1);
    }
    ghost_store_get(&store, 1, &source);
    assert(!ghost_source_fresh(&source, 31000, 30));
    ghost_store_get(&store, GHOST_DONGLES_MAX, &source);
    assert(ghost_source_fresh(&source, 31000, 30));
    source.conflict = true;
    assert(!ghost_source_fresh(&source, 31000, 30));
    strcpy(c.dongles[1].ip, "192.168.050.3");
    assert(!ghost_config_validate(&c, err, sizeof(err)));
    strcpy(c.dongles[1].ip, "192.168.50.3");
    strcpy(c.entities[i].suffix, "battery_soc");
    assert(ghost_mapped_topic(topic, sizeof(topic), &c, 0, i) &&
           !strcmp(topic, "solarproxxie/INVERTER1/battery_soc"));
    strcpy(c.ap_ssid, "ESPGhostNode");
    strcpy(c.base, "espghostnode");
    strcpy(c.client, "espghostnode");
    assert(ghost_config_migrate(GHOST_CONFIG_VERSION, &c, sizeof(c), &migrated) == ESP_OK);
    assert(!strcmp(migrated.ap_ssid, "SolarProxxie") && !strcmp(migrated.base, "solarproxxie") &&
           !strcmp(migrated.client, "solarproxxie"));
    strcpy(c.ap_ssid, "BOB4");
    strcpy(c.base, "custom_solar");
    strcpy(c.client, "custom_client");
    assert(ghost_config_migrate(GHOST_CONFIG_VERSION, &c, sizeof(c), &migrated) == ESP_OK);
    assert(!strcmp(migrated.ap_ssid, "BOB4") && !strcmp(migrated.base, "custom_solar") &&
           !strcmp(migrated.client, "custom_client"));
    assert(!strcmp(migrated.ap_password, c.ap_password) &&
           !memcmp(migrated.hash, c.hash, sizeof(c.hash)));
    puts("PASS multi-dongle: isolated telemetry, serial conflict, naming, topics, discovery IDs, "
         "v1 NVS migration and mapping persistence");
}
static void load_split_tests(void) {
    size_t home = field("home_load_power"), ups = field("ups_power");
    size_t ct = field("grid_power_ct"), line = field("grid_power_l1");
    ghost_values_t v = {0};
    v.valid = (UINT64_C(1) << ct) | (UINT64_C(1) << line);
    v.value[ct] = 350;
    v.value[line] = 332;
    ghost_derive_values(&v);
    assert(v.value[home] == 18 && (v.valid & (UINT64_C(1) << home)));
    v.value[ct] = -100;
    v.value[line] = -118;
    ghost_derive_values(&v);
    assert(v.value[home] == 18);
    v.value[ct] = 100;
    v.value[line] = 118;
    ghost_derive_values(&v);
    assert(v.value[home] == 0);
    v.valid &= ~(UINT64_C(1) << ct);
    ghost_derive_values(&v);
    assert(!(v.valid & (UINT64_C(1) << home)));
    uint8_t packet[302] = {0};
    packet[0] = 0xa5;
    memcpy(packet + 11, "TEST000001", 10);
    packet[37] = 26;
    packet[38] = 9;
    packet[39] = 17;
    packet[242] = 2;
    packet[243] = 95;
    assert(ghost_inteless_decode(packet, 302, &v));
    assert(v.value[ups] == 607 && (v.valid & (UINT64_C(1) << ups)));
    assert(ghost_inteless_decode(packet, 292, &v));
    assert(!(v.valid & (UINT64_C(1) << ups)));
    ghost_config_t c, m;
    ghost_config_get(&c);
    memset(&c.entities[42], 0, (ghost_field_count - 42) * sizeof(c.entities[0]));
    assert(ghost_config_migrate(c.version, &c, sizeof(c), &m) == ESP_OK);
    assert(m.entities[home].enabled && m.entities[ups].enabled);
    assert(!strcmp(m.entities[ups].suffix, "ups_power"));
    m.entities[ups].enabled = false;
    strcpy(m.entities[ups].name, "Custom UPS");
    assert(ghost_config_migrate(m.version, &m, sizeof(m), &c) == ESP_OK);
    assert(!c.entities[ups].enabled && !strcmp(c.entities[ups].name, "Custom UPS"));
    strcpy(c.dongles[0].ip, "192.168.50.2");
    char topic[192];
    assert(ghost_mapped_topic(topic, sizeof(topic), &c, 0, home));
    assert(!strcmp(topic, "solarproxxie/INVERTER1/home_load_power"));
    puts(
        "PASS load split: signed flows, clamping, missing inputs, UPS profile, migration and MQTT");
}
static void captured_temperature_tests(void) {
    uint8_t p[612];
    sample(p, 306);
    ghost_values_t v;
    p[114] = 5;
    p[115] = 230; /* 1510 -> 51 C */
    p[116] = 5;
    p[117] = 150; /* 1430 -> 43 C */
    p[248] = 4;
    p[249] = 199; /* 1223 -> 22.3 C */
    p[298] = 21;
    p[299] = 22; /* BMS layout must not interpret 5398 as amps. */
    assert(ghost_inteless_decode(p, 306, &v));
    assert(fabs(v.value[field("inverter_temperature_dc")] - 51) < 1e-9);
    assert(fabs(v.value[field("inverter_temperature_ac")] - 43) < 1e-9);
    assert(fabs(v.value[field("battery_temperature")] - 22.3) < 1e-9);
    for (size_t i = 0; i < ghost_field_count; i++)
        if (!strncmp(ghost_fields[i].id, "bms_", 4))
            assert(!(v.valid & (UINT64_C(1) << i)));
    p[248] = 3;
    p[249] = 132; /* Real -10 C is preserved, not clamped. */
    assert(ghost_inteless_decode(p, 306, &v));
    assert(fabs(v.value[field("battery_temperature")] + 10) < 1e-9);
    ghost_stream_t st = {.frame_length = 306};
    frames = 0;
    ghost_stream_feed(&st, 100, p, 302, emit, NULL);
    assert(frames == 0);
    ghost_stream_feed(&st, 402, p + 302, 4, emit, NULL);
    assert(frames == 1);
    ghost_stream_feed(&st, 100, p, 306, emit, NULL);
    assert(frames == 1);
    memcpy(p + 306, p, 306);
    memset(&st, 0, sizeof(st));
    st.frame_length = 306;
    frames = 0;
    ghost_stream_feed(&st, 1, p, 612, emit, NULL);
    assert(frames == 2);
    ghost_config_t c;
    ghost_config_defaults(&c);
    c.layout = 306;
    strcpy(c.dongles[0].ip, "192.168.50.2");
    cJSON *j =
        ghost_discovery_json(&c, field("battery_temperature"), "ghost_0123456789ab", 0, "1.0.0");
    assert(j);
    assert(!strcmp(cJSON_GetObjectItem(j, "unit_of_measurement")->valuestring, "\xc2\xb0"
                                                                               "C"));
    assert(!strcmp(cJSON_GetObjectItem(j, "device_class")->valuestring, "temperature"));
    cJSON_Delete(j);
    puts("PASS 306 profile: Celsius, legitimate negatives, unknown BMS suppressed, stream "
         "boundaries");
}
static void captured_energy_tests(void) {
    const char *ids[] = {"battery_charge_daily", "grid_import_daily", "load_energy_daily",
                         "pv_energy_daily",      "pv_energy_monthly", "load_energy_monthly"};
    const unsigned offsets[] = {74, 86, 102, 150, 64, 66};
    const unsigned raw[] = {4, 83, 133, 55, 575, 3213};
    uint8_t packet[306];
    ghost_values_t values;
    sample(packet, sizeof(packet));
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        packet[offsets[i]] = raw[i] >> 8;
        packet[offsets[i] + 1] = raw[i];
    }
    assert(ghost_inteless_decode(packet, 306, &values));
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        size_t index = field(ids[i]);
        assert(values.valid & (UINT64_C(1) << index));
        assert(fabs(values.value[index] - raw[i] * 0.1) < 1e-9);
    }
    assert(ghost_inteless_decode(packet, 302, &values));
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++)
        assert(!(values.valid & (UINT64_C(1) << field(ids[i]))));
    sample(packet, 292);
    assert(ghost_inteless_decode(packet, 292, &values));
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++)
        assert(!(values.valid & (UINT64_C(1) << field(ids[i]))));
    ghost_config_t config;
    ghost_config_defaults(&config);
    strcpy(config.dongles[0].ip, "192.168.50.2");
    size_t index = field("pv_energy_daily");
    assert(config.entities[index].enabled);
    char topic[192];
    assert(ghost_mapped_topic(topic, sizeof(topic), &config, 0, index));
    assert(!strcmp(topic, "solarproxxie/INVERTER1/pv_energy_daily"));
    cJSON *discovery = ghost_discovery_json(&config, index, "ghost_0123456789ab", 0, "1.0.0");
    assert(discovery);
    assert(!strcmp(cJSON_GetObjectItem(discovery, "state_class")->valuestring, "total_increasing"));
    cJSON_Delete(discovery);
    puts("PASS 306 energy counters: scaling, layout isolation, MQTT topic and discovery");
}
int main(void) {
    cloud_emulator_tests();
    identity_tests();
    captured_energy_tests();
    captured_temperature_tests();
    config_tests();
    load_split_tests();
    packet_view_tests();
    protocol_tests();
    security_tests();
    mqtt_capture_tests();
    multi_dongle_tests();
    puts("All host tests passed.");
    return 0;
}
