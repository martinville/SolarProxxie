#include "config/config.h"
#include "protocol/protocol.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ghost_config_t config;
static unsigned mapping_slot;

static void quote(const char *text) {
    putchar('"');
    for (; *text; text++) {
        if (*text == '"' || *text == '\\')
            putchar('\\');
        putchar(*text);
    }
    putchar('"');
}

static bool load_mapping(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END))
        return false;
    long size = ftell(file);
    if (size <= 0 || size > 24000 || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return false;
    }
    char *text = malloc((size_t)size + 1);
    if (!text || fread(text, 1, (size_t)size, file) != (size_t)size) {
        free(text);
        fclose(file);
        return false;
    }
    fclose(file);
    text[size] = 0;
    cJSON *json = cJSON_Parse(text);
    free(text);
    char error[160] = "Expected one self-contained packetoffsetNNN.json file";
    const cJSON *number = json ? cJSON_GetObjectItemCaseSensitive(json, "packet_offset") : NULL;
    bool valid = cJSON_IsNumber(number) && number->valueint >= 1 &&
                 number->valueint <= GHOST_PACKET_PROFILE_COUNT;
    if (valid) {
        mapping_slot = (unsigned)number->valueint - 1;
        valid = ghost_packet_offsets_from_json(&config, mapping_slot, json, error, sizeof(error));
    }
    if (!valid)
        fprintf(stderr, "%s\n", json ? error : "Invalid mapping JSON");
    cJSON_Delete(json);
    if (valid)
        ghost_packet_mappings_apply(config.fields, config.field_count, config.packet_mappings);
    return valid;
}

int main(int argc, char **argv) {
    int start = -1;
    const char *mapping = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--modbus-start") && i + 1 < argc)
            start = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mapping") && i + 1 < argc)
            mapping = argv[++i];
        else
            return 2;
    }
    ghost_config_defaults(&config);
    if (!mapping || !load_mapping(mapping)) {
        fputs("A valid --mapping file is required\n", stderr);
        return 2;
    }
    char line[4096];
    while (fgets(line, sizeof(line), stdin)) {
        uint8_t bytes[1024];
        size_t count = 0;
        int high = -1;
        bool bad = false;
        for (char *p = line; *p; p++) {
            if (isspace((unsigned char)*p))
                continue;
            int x = *p >= '0' && *p <= '9'   ? *p - '0'
                    : *p >= 'a' && *p <= 'f' ? *p - 'a' + 10
                    : *p >= 'A' && *p <= 'F' ? *p - 'A' + 10
                                             : -1;
            if (x < 0) {
                bad = true;
                break;
            }
            if (high < 0)
                high = x;
            else {
                if (count == sizeof(bytes)) {
                    bad = true;
                    break;
                }
                bytes[count++] = (uint8_t)((high << 4) | x);
                high = -1;
            }
        }
        ghost_values_t values;
        bool valid =
            !bad && high < 0 &&
            (start >= 0 ? ghost_modbus_response(bytes, count, (uint16_t)start, &values)
                        : ghost_inteless_decode_profile(bytes, count, mapping_slot, &values));
        if (!valid) {
            puts("{\"decoded\":false}");
            continue;
        }
        printf("{\"decoded\":true,\"serial\":");
        quote(values.serial);
        printf(",\"values\":{");
        bool first = true;
        for (size_t i = 0; i < ghost_field_count; i++)
            if (values.valid & (UINT64_C(1) << i)) {
                if (!first)
                    putchar(',');
                first = false;
                quote(ghost_fields[i].id);
                printf(":{\"value\":%.12g,\"unit\":", values.value[i]);
                quote(config.entities[i].unit);
                printf(",\"register\":%d}", ghost_fields[i].reg[0]);
            }
        puts("}}");
    }
    return 0;
}
