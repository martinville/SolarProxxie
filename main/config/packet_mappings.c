#include "config.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
extern const uint8_t packet_offset_001_start[] asm("_binary_packetoffset001_json_start");
extern const uint8_t packet_offset_001_end[] asm("_binary_packetoffset001_json_end");
extern const uint8_t packet_offset_002_start[] asm("_binary_packetoffset002_json_start");
extern const uint8_t packet_offset_002_end[] asm("_binary_packetoffset002_json_end");
extern const uint8_t packet_offset_003_start[] asm("_binary_packetoffset003_json_start");
extern const uint8_t packet_offset_003_end[] asm("_binary_packetoffset003_json_end");
#endif

static bool unique_keys(const cJSON *v) {
    if (cJSON_IsObject(v)) {
        const cJSON *i;
        cJSON_ArrayForEach(i, v) {
            for (const cJSON *o = i->next; o; o = o->next)
                if (!strcmp(i->string, o->string))
                    return false;
            if (!unique_keys(i))
                return false;
        }
    } else if (cJSON_IsArray(v)) {
        const cJSON *i;
        cJSON_ArrayForEach(i, v) if (!unique_keys(i)) return false;
    }
    return true;
}

static int field_index(const ghost_config_t *c, const char *id) {
    if (id)
        for (size_t i = 0; i < c->field_count; i++)
            if (!strcmp(id, c->fields[i].id))
                return (int)i;
    return -1;
}

static const char *type_name(ghost_type_t type) {
    static const char *names[] = {"u16", "i16", "u32", "i32", "bits", "enum"};
    return type <= GHOST_ENUM ? names[type] : "u16";
}

static bool parse_type(const char *name, ghost_type_t *type, uint8_t *words) {
    static const char *names[] = {"u16", "i16", "u32", "i32", "bits", "enum"};
    if (!name)
        return false;
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (!strcmp(name, names[i])) {
            *type = (ghost_type_t)i;
            *words = i == GHOST_U32 || i == GHOST_I32 ? 2 : 1;
            return true;
        }
    return false;
}

static bool string_value(const cJSON *o, const char *key, char *out, size_t cap, bool required) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!v)
        return !required;
    if (!cJSON_IsString(v) || (!v->valuestring[0] && strcmp(key, "unit")) ||
        strlen(v->valuestring) >= cap)
        return false;
    strcpy(out, v->valuestring);
    return true;
}

static bool number_value(const cJSON *v, double *out) {
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble))
        return false;
    *out = v->valuedouble;
    return true;
}

static bool byte_offset(const cJSON *v, unsigned layout, uint16_t *out) {
    if (!cJSON_IsNumber(v) || v->valuedouble != v->valueint || v->valueint < 43 ||
        v->valueint + 2 > (int)layout)
        return false;
    *out = (uint16_t)v->valueint;
    return true;
}

static bool registers(const cJSON *item, ghost_field_t *field) {
    field->reg[0] = field->reg[1] = -1;
    const cJSON *one = cJSON_GetObjectItemCaseSensitive(item, "register");
    const cJSON *two = cJSON_GetObjectItemCaseSensitive(item, "registers");
    if (!one && !two)
        return true;
    if (field->words == 1 && cJSON_IsNumber(one) && !two && one->valuedouble == one->valueint &&
        one->valueint >= 0 && one->valueint <= 32767) {
        field->reg[0] = (int16_t)one->valueint;
        return true;
    }
    if (field->words == 2 && !one && cJSON_IsArray(two) && cJSON_GetArraySize(two) == 2) {
        const cJSON *a = cJSON_GetArrayItem(two, 0), *b = cJSON_GetArrayItem(two, 1);
        if (cJSON_IsNumber(a) && cJSON_IsNumber(b) && a->valuedouble == a->valueint &&
            b->valuedouble == b->valueint && a->valueint >= 0 && a->valueint <= 32767 &&
            b->valueint >= 0 && b->valueint <= 32767) {
            field->reg[0] = (int16_t)a->valueint;
            field->reg[1] = (int16_t)b->valueint;
            return true;
        }
    }
    return false;
}

static bool formula(const ghost_config_t *c, size_t index, const char *text, ghost_field_t *field) {
    const char *next = text;
    while (next && *next) {
        bool subtract = *next == '-';
        if (subtract)
            next++;
        const char *end = strchr(next, ' ');
        size_t length = end ? (size_t)(end - next) : strlen(next), source;
        if (!length || field->sum_count == sizeof(field->sum_fields))
            return false;
        for (source = 0; source < index; source++)
            if (strlen(c->fields[source].id) == length &&
                !memcmp(c->fields[source].id, next, length))
                break;
        if (source == index)
            return false;
        field->sum_fields[field->sum_count] = (int8_t)source;
        if (subtract)
            field->sum_subtract |= 1U << field->sum_count;
        field->sum_count++;
        next = end ? end + 1 : NULL;
    }
    return true;
}

cJSON *ghost_data_point_definition_json(const ghost_config_t *c, size_t i) {
    if (!c || i >= c->field_count)
        return NULL;
    const ghost_field_t *f = &c->fields[i];
    const ghost_entity_t *e = &c->entities[i];
    cJSON *j = cJSON_CreateObject();
    if (!j)
        return NULL;
    cJSON_AddStringToObject(j, "id", f->id);
    cJSON_AddStringToObject(j, "name", e->name);
    cJSON_AddStringToObject(j, "unit", e->unit);
    cJSON_AddStringToObject(j, "type", type_name(f->type));
    cJSON_AddNumberToObject(j, "scale", f->scale);
    cJSON_AddNumberToObject(j, "add", f->offset);
    if (f->has_minimum)
        cJSON_AddNumberToObject(j, "minimum", f->minimum);
    if (f->device_class[0])
        cJSON_AddStringToObject(j, "device_class", f->device_class);
    if (f->state_class[0])
        cJSON_AddStringToObject(j, "state_class", f->state_class);
    if (f->type == GHOST_BITS)
        cJSON_AddNumberToObject(j, "mask", f->mask);
    if (f->reg[0] >= 0) {
        if (f->words == 2) {
            cJSON *a = cJSON_AddArrayToObject(j, "registers");
            cJSON_AddItemToArray(a, cJSON_CreateNumber(f->reg[0]));
            cJSON_AddItemToArray(a, cJSON_CreateNumber(f->reg[1]));
        } else
            cJSON_AddNumberToObject(j, "register", f->reg[0]);
    }
    if (f->sum_count) {
        char text[128] = {0};
        size_t used = 0;
        for (unsigned n = 0; n < f->sum_count; n++) {
            int written = snprintf(text + used, sizeof(text) - used, "%s%s%s", n ? " " : "",
                                   f->sum_subtract & (1U << n) ? "-" : "",
                                   c->fields[(uint8_t)f->sum_fields[n]].id);
            if (written < 0 || (size_t)written >= sizeof(text) - used)
                break;
            used += (size_t)written;
        }
        cJSON_AddStringToObject(j, "sum_of", text);
    }
    return j;
}

typedef struct {
    const cJSON *array;
    const char *line;
    unsigned index, count;
} point_source_t;

static bool point_source(const cJSON *root, point_source_t *source) {
    const cJSON *array = cJSON_GetObjectItemCaseSensitive(root, "data_points");
    const cJSON *lines = cJSON_GetObjectItemCaseSensitive(root, "data_points_jsonl");
    memset(source, 0, sizeof(*source));
    if (cJSON_IsArray(array) && !lines) {
        source->array = array;
        source->count = (unsigned)cJSON_GetArraySize(array);
        return source->count <= GHOST_FIELDS_MAX;
    }
    if (!array && cJSON_IsString(lines) && lines->valuestring[0]) {
        source->line = lines->valuestring;
        source->count = 1;
        for (const char *p = source->line; *p; p++)
            if (*p == '\n')
                source->count++;
        return source->count <= GHOST_FIELDS_MAX;
    }
    return false;
}

static cJSON *next_point(point_source_t *source, bool *owned) {
    *owned = false;
    if (source->index >= source->count)
        return NULL;
    if (source->array)
        return (cJSON *)cJSON_GetArrayItem(source->array, (int)source->index++);
    const char *start = source->line;
    const char *end = strchr(start, '\n');
    size_t length = end ? (size_t)(end - start) : strlen(start);
    char *text = malloc(length + 1);
    if (!text)
        return NULL;
    memcpy(text, start, length);
    text[length] = 0;
    cJSON *item = cJSON_Parse(text);
    free(text);
    source->line = end ? end + 1 : start + length;
    source->index++;
    *owned = true;
    return item;
}

static bool parse_points_at(ghost_config_t *out, const cJSON *root, size_t start, char *err,
                            unsigned cap) {
    point_source_t source;
    if (!point_source(root, &source) || start + source.count > GHOST_FIELDS_MAX) {
        snprintf(err, cap, "Invalid data_points in packet-offset file");
        return false;
    }
    out->field_count = start + source.count;
    for (size_t i = start; i < out->field_count; i++) {
        bool owned = false;
        cJSON *item = next_point(&source, &owned);
        ghost_field_t *f = &out->fields[i];
        ghost_entity_t *e = &out->entities[i];
        char type[8] = {0}, sum[128] = {0};
        if (!cJSON_IsObject(item) || !unique_keys(item) ||
            !string_value(item, "id", f->id, sizeof(f->id), true) ||
            !string_value(item, "name", e->name, sizeof(e->name), true) ||
            !string_value(item, "unit", e->unit, sizeof(e->unit), true) ||
            !string_value(item, "type", type, sizeof(type), true) ||
            !parse_type(type, &f->type, &f->words) ||
            !string_value(item, "device_class", f->device_class, sizeof(f->device_class), false) ||
            !string_value(item, "state_class", f->state_class, sizeof(f->state_class), false) ||
            !string_value(item, "sum_of", sum, sizeof(sum), false)) {
            snprintf(err, cap, "Invalid or oversized data point at index %u", (unsigned)i);
            if (owned)
                cJSON_Delete(item);
            return false;
        }
        for (size_t old = 0; old < i; old++)
            if (!strcmp(f->id, out->fields[old].id)) {
                snprintf(err, cap, "Duplicate data point id: %s", f->id);
                if (owned)
                    cJSON_Delete(item);
                return false;
            }
        const cJSON *scale = cJSON_GetObjectItemCaseSensitive(item, "scale");
        const cJSON *add = cJSON_GetObjectItemCaseSensitive(item, "add");
        const cJSON *minimum = cJSON_GetObjectItemCaseSensitive(item, "minimum");
        f->scale = 1;
        if ((scale && !number_value(scale, &f->scale)) || (add && !number_value(add, &f->offset)) ||
            f->scale == 0) {
            snprintf(err, cap, "Invalid scale/add for %s", f->id);
            if (owned)
                cJSON_Delete(item);
            return false;
        }
        if (minimum && !(f->has_minimum = number_value(minimum, &f->minimum))) {
            snprintf(err, cap, "Invalid minimum for %s", f->id);
            if (owned)
                cJSON_Delete(item);
            return false;
        }
        f->mask = UINT32_MAX;
        const cJSON *mask = cJSON_GetObjectItemCaseSensitive(item, "mask");
        if (mask &&
            (f->type != GHOST_BITS || !cJSON_IsNumber(mask) || mask->valuedouble < 0 ||
             mask->valuedouble > UINT32_MAX || mask->valuedouble != floor(mask->valuedouble))) {
            snprintf(err, cap, "Invalid bit mask for %s", f->id);
            if (owned)
                cJSON_Delete(item);
            return false;
        } else if (mask)
            f->mask = (uint32_t)mask->valuedouble;
        if (sum[0]) {
            if (!formula(out, i, sum, f)) {
                snprintf(err, cap, "Invalid derived formula for %s", f->id);
                if (owned)
                    cJSON_Delete(item);
                return false;
            }
            f->words = 0;
        }
        if (!registers(item, f)) {
            snprintf(err, cap, "Invalid register definition for %s", f->id);
            if (owned)
                cJSON_Delete(item);
            return false;
        }
        e->enabled = true;
        memcpy(e->suffix, f->id, sizeof(f->id));
        e->suffix[sizeof(f->id) - 1] = 0;
        snprintf(e->ha_name, sizeof(e->ha_name), "%s", e->name);
        if (owned)
            cJSON_Delete(item);
    }
    return true;
}

static bool parse_points(ghost_config_t *out, const cJSON *root, char *err, unsigned cap) {
    return parse_points_at(out, root, 0, err, cap);
}

cJSON *ghost_packet_offsets_json(const ghost_config_t *c, unsigned slot) {
    if (!c || slot >= GHOST_PACKET_PROFILE_COUNT || !c->packet_profile_active[slot])
        return NULL;
    cJSON *root = cJSON_CreateObject();
    if (!root)
        return NULL;
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddNumberToObject(root, "packet_offset", slot + 1);
    cJSON_AddStringToObject(root, "name", c->packet_profile_names[slot]);
    cJSON_AddNumberToObject(root, "layout", c->packet_profile_layouts[slot]);
    cJSON *points = cJSON_AddArrayToObject(root, "data_points");
    for (size_t i = 0; points && i < c->field_count; i++) {
        cJSON *point = ghost_data_point_definition_json(c, i);
        if (!point) {
            cJSON_Delete(root);
            return NULL;
        }
        const ghost_packet_mapping_t *m = &c->packet_mappings[slot][i];
        if (m->state == GHOST_PACKET_MAPPING_OFFSET && c->fields[i].words == 1)
            cJSON_AddNumberToObject(point, "offset", m->position[0]);
        else if (m->state == GHOST_PACKET_MAPPING_OFFSET && c->fields[i].words == 2) {
            cJSON *a = cJSON_AddArrayToObject(point, "offsets");
            cJSON_AddItemToArray(a, cJSON_CreateNumber(m->position[0]));
            cJSON_AddItemToArray(a, cJSON_CreateNumber(m->position[1]));
        }
        cJSON_AddItemToArray(points, point);
    }
    if (!points) {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

bool ghost_packet_offsets_from_json(ghost_config_t *c, unsigned slot, const cJSON *root, char *err,
                                    unsigned cap) {
    if (!c || slot >= GHOST_PACKET_PROFILE_COUNT || !cJSON_IsObject(root) || !unique_keys(root)) {
        snprintf(err, cap, "Expected a packet-offset JSON object with unique keys");
        return false;
    }
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON *number = cJSON_GetObjectItemCaseSensitive(root, "packet_offset");
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "name");
    const cJSON *length = cJSON_GetObjectItemCaseSensitive(root, "layout");
    point_source_t source;
    if (!cJSON_IsNumber(version) || version->valuedouble != 1 || !cJSON_IsNumber(number) ||
        number->valuedouble != number->valueint || number->valueint != (int)slot + 1 ||
        !cJSON_IsString(name) || !name->valuestring[0] || strlen(name->valuestring) >= 32) {
        snprintf(err, cap, "File number and name must match packetoffset%03u.json", slot + 1);
        return false;
    }
    if (!cJSON_IsNumber(length) || length->valuedouble != length->valueint ||
        length->valueint < 43 || length->valueint > GHOST_FRAME_MAX ||
        !point_source(root, &source)) {
        snprintf(err, cap, "Invalid packet length or data_points in packetoffset%03u.json",
                 slot + 1);
        return false;
    }
    static ghost_packet_mapping_t saved[GHOST_PACKET_PROFILE_COUNT][GHOST_FIELDS_MAX];
    memcpy(saved, c->packet_mappings, sizeof(c->packet_mappings));
    int8_t old_index[GHOST_FIELDS_MAX];
    uint8_t old_words[GHOST_FIELDS_MAX];
    memset(old_index, -1, sizeof(old_index));
    memset(old_words, 0, sizeof(old_words));
    unsigned point = 0;
    point_source(root, &source);
    cJSON *item;
    bool owned = false;
    while ((item = next_point(&source, &owned))) {
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
        int old = cJSON_IsString(id) ? field_index(c, id->valuestring) : -1;
        const cJSON *type = cJSON_GetObjectItemCaseSensitive(item, "type");
        const cJSON *sum = cJSON_GetObjectItemCaseSensitive(item, "sum_of");
        const cJSON *one = cJSON_GetObjectItemCaseSensitive(item, "offset");
        const cJSON *two = cJSON_GetObjectItemCaseSensitive(item, "offsets");
        ghost_type_t parsed_type;
        uint8_t words = 0;
        bool offset_valid = true;
        if (cJSON_IsString(type) && parse_type(type->valuestring, &parsed_type, &words) &&
            !(cJSON_IsString(sum) && sum->valuestring[0]) && (one || two)) {
            uint16_t positions[2];
            offset_valid =
                words == 1
                    ? !two && byte_offset(one, length->valueint, &positions[0])
                    : !one && cJSON_IsArray(two) && cJSON_GetArraySize(two) == 2 &&
                          byte_offset(cJSON_GetArrayItem(two, 0), length->valueint,
                                      &positions[0]) &&
                          byte_offset(cJSON_GetArrayItem(two, 1), length->valueint,
                                      &positions[1]);
        }
        if (!offset_valid) {
            snprintf(err, cap, "Invalid byte offset for %s",
                     cJSON_IsString(id) ? id->valuestring : "data point");
            if (owned)
                cJSON_Delete(item);
            return false;
        }
        if (point < GHOST_FIELDS_MAX && old >= 0) {
            old_index[point] = (int8_t)old;
            old_words[point] = c->fields[old].words;
        }
        point++;
        if (owned)
            cJSON_Delete(item);
    }
    if (point != source.count) {
        snprintf(err, cap, "Invalid data point JSON");
        return false;
    }
    memset(c->fields, 0, sizeof(c->fields));
    memset(c->entities, 0, sizeof(c->entities));
    c->field_count = 0;
    if (!parse_points(c, root, err, cap)) {
        return false;
    }
    memset(c->packet_mappings, 0, sizeof(c->packet_mappings));
    for (size_t i = 0; i < c->field_count; i++) {
        int old = old_index[i];
        if (old >= 0 && old_words[i] == c->fields[i].words) {
            for (unsigned p = 0; p < GHOST_PACKET_PROFILE_COUNT; p++)
                c->packet_mappings[p][i] = saved[p][old];
        }
    }
    memset(c->packet_mappings[slot], 0, sizeof(c->packet_mappings[slot]));
    uint64_t seen = 0;
    point_source(root, &source);
    while ((item = next_point(&source, &owned))) {
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
        int i = cJSON_IsString(id) ? field_index(c, id->valuestring) : -1;
        if (!cJSON_IsObject(item) || i < 0 || (seen & (UINT64_C(1) << i))) {
            snprintf(err, cap, "Unknown or duplicate data point");
            if (owned)
                cJSON_Delete(item);
            return false;
        }
        seen |= UINT64_C(1) << i;
        if (!c->fields[i].words) {
            if (owned)
                cJSON_Delete(item);
            continue;
        }
        const cJSON *one = cJSON_GetObjectItemCaseSensitive(item, "offset");
        const cJSON *two = cJSON_GetObjectItemCaseSensitive(item, "offsets");
        if (!one && !two) {
            if (owned)
                cJSON_Delete(item);
            continue;
        }
        ghost_packet_mapping_t *m = &c->packet_mappings[slot][i];
        m->state = GHOST_PACKET_MAPPING_OFFSET;
        bool valid =
            c->fields[i].words == 1
                ? !two && byte_offset(one, length->valueint, &m->position[0])
                : !one && cJSON_IsArray(two) && cJSON_GetArraySize(two) == 2 &&
                      byte_offset(cJSON_GetArrayItem(two, 0), length->valueint, &m->position[0]) &&
                      byte_offset(cJSON_GetArrayItem(two, 1), length->valueint, &m->position[1]);
        if (!valid) {
            snprintf(err, cap, "Invalid byte offset for %s", id->valuestring);
            if (owned)
                cJSON_Delete(item);
            return false;
        }
        if (owned)
            cJSON_Delete(item);
    }
    c->packet_profile_active[slot] = true;
    c->packet_profile_layouts[slot] = (uint16_t)length->valueint;
    snprintf(c->packet_profile_names[slot], sizeof(c->packet_profile_names[slot]), "%s",
             name->valuestring);
    return true;
}

typedef struct {
    ghost_config_t *config;
    unsigned slot, expected, received, layout;
    ghost_packet_mapping_t mappings[GHOST_PACKET_PROFILE_COUNT][GHOST_FIELDS_MAX];
    char ids[GHOST_FIELDS_MAX][40];
    uint8_t words[GHOST_FIELDS_MAX];
} packet_upload_t;
static packet_upload_t *upload;

void ghost_packet_upload_cancel(void) {
    if (upload) {
        if (upload->config) {
            memset(upload->config, 0, sizeof(*upload->config));
            free(upload->config);
        }
        memset(upload, 0, sizeof(*upload));
        free(upload);
        upload = NULL;
    }
}

bool ghost_packet_upload_begin(unsigned slot, const cJSON *metadata, char *err, unsigned cap) {
    ghost_packet_upload_cancel();
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(metadata, "version");
    const cJSON *number = cJSON_GetObjectItemCaseSensitive(metadata, "packet_offset");
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(metadata, "name");
    const cJSON *layout = cJSON_GetObjectItemCaseSensitive(metadata, "layout");
    const cJSON *count = cJSON_GetObjectItemCaseSensitive(metadata, "point_count");
    if (slot >= GHOST_PACKET_PROFILE_COUNT || !cJSON_IsObject(metadata) ||
        !unique_keys(metadata) || !cJSON_IsNumber(version) || version->valueint != 1 ||
        version->valuedouble != version->valueint || !cJSON_IsNumber(number) ||
        number->valueint != (int)slot + 1 || number->valuedouble != number->valueint ||
        !cJSON_IsString(name) || !name->valuestring[0] || strlen(name->valuestring) >= 32 ||
        !cJSON_IsNumber(layout) || layout->valuedouble != layout->valueint ||
        layout->valueint < 43 || layout->valueint > GHOST_FRAME_MAX || !cJSON_IsNumber(count) ||
        count->valuedouble != count->valueint || count->valueint < 0 ||
        count->valueint > GHOST_FIELDS_MAX) {
        snprintf(err, cap, "Invalid packet-offset upload metadata");
        return false;
    }
    upload = calloc(1, sizeof(*upload));
    if (!upload) {
        snprintf(err, cap, "Memory unavailable while starting chunked setup upload");
        return false;
    }
    upload->config = malloc(sizeof(*upload->config));
    if (!upload->config) {
        snprintf(err, cap, "Memory unavailable while starting chunked setup upload");
        ghost_packet_upload_cancel();
        return false;
    }
    ghost_config_get(upload->config);
    memcpy(upload->mappings, upload->config->packet_mappings, sizeof(upload->mappings));
    for (size_t i = 0; i < upload->config->field_count; i++) {
        snprintf(upload->ids[i], sizeof(upload->ids[i]), "%s", upload->config->fields[i].id);
        upload->words[i] = upload->config->fields[i].words;
    }
    upload->slot = slot;
    upload->expected = (unsigned)count->valueint;
    upload->layout = (unsigned)layout->valueint;
    memset(upload->config->fields, 0, sizeof(upload->config->fields));
    memset(upload->config->entities, 0, sizeof(upload->config->entities));
    memset(upload->config->packet_mappings, 0, sizeof(upload->config->packet_mappings));
    upload->config->field_count = 0;
    upload->config->packet_profile_active[slot] = true;
    upload->config->packet_profile_layouts[slot] = (uint16_t)upload->layout;
    snprintf(upload->config->packet_profile_names[slot],
             sizeof(upload->config->packet_profile_names[slot]), "%s", name->valuestring);
    return true;
}

bool ghost_packet_upload_chunk(const cJSON *chunk, char *err, unsigned cap) {
    point_source_t source;
    if (!upload || !upload->config || !point_source(chunk, &source) || !source.array ||
        upload->received + source.count > upload->expected) {
        snprintf(err, cap, "Invalid or unexpected packet-offset upload chunk");
        ghost_packet_upload_cancel();
        return false;
    }
    unsigned start = upload->received;
    if (!parse_points_at(upload->config, chunk, start, err, cap)) {
        ghost_packet_upload_cancel();
        return false;
    }
    for (unsigned n = 0; n < source.count; n++) {
        unsigned i = start + n;
        ghost_field_t *field = &upload->config->fields[i];
        for (unsigned old = 0; old < GHOST_FIELDS_MAX; old++)
            if (upload->ids[old][0] && upload->words[old] == field->words &&
                !strcmp(upload->ids[old], field->id)) {
                for (unsigned profile = 0; profile < GHOST_PACKET_PROFILE_COUNT; profile++)
                    upload->config->packet_mappings[profile][i] = upload->mappings[profile][old];
                break;
            }
        const cJSON *item = cJSON_GetArrayItem(source.array, (int)n);
        const cJSON *one = cJSON_GetObjectItemCaseSensitive(item, "offset");
        const cJSON *two = cJSON_GetObjectItemCaseSensitive(item, "offsets");
        if (!field->words || (!one && !two))
            continue;
        ghost_packet_mapping_t *mapping = &upload->config->packet_mappings[upload->slot][i];
        bool valid = field->words == 1
                         ? !two && byte_offset(one, upload->layout, &mapping->position[0])
                         : !one && cJSON_IsArray(two) && cJSON_GetArraySize(two) == 2 &&
                               byte_offset(cJSON_GetArrayItem(two, 0), upload->layout,
                                           &mapping->position[0]) &&
                               byte_offset(cJSON_GetArrayItem(two, 1), upload->layout,
                                           &mapping->position[1]);
        if (!valid) {
            snprintf(err, cap, "Invalid byte offset for %s", field->id);
            ghost_packet_upload_cancel();
            return false;
        }
        mapping->state = GHOST_PACKET_MAPPING_OFFSET;
    }
    upload->received += source.count;
    return true;
}

ghost_config_t *ghost_packet_upload_finish(char *err, unsigned cap) {
    if (!upload || !upload->config || upload->received != upload->expected) {
        snprintf(err, cap, "Packet-offset upload is incomplete (%u of %u data points)",
                 upload ? upload->received : 0, upload ? upload->expected : 0);
        ghost_packet_upload_cancel();
        return NULL;
    }
    ghost_config_t *result = upload->config;
    upload->config = NULL;
    ghost_packet_upload_cancel();
    return result;
}

bool ghost_packet_mapping_defaults(ghost_config_t *c, char *err, unsigned cap) {
#ifndef ESP_PLATFORM
    (void)c;
    snprintf(err, cap, "Shipped mapping files are available only in the ESP32 firmware image");
    return false;
#else
    static const uint8_t *starts[] = {packet_offset_001_start, packet_offset_002_start,
                                      packet_offset_003_start};
    static const uint8_t *ends[] = {packet_offset_001_end, packet_offset_002_end,
                                    packet_offset_003_end};
    bool valid = true;
    for (unsigned file = 0; valid && file < 3; file++) {
        cJSON *json =
            cJSON_ParseWithLength((const char *)starts[file], (size_t)(ends[file] - starts[file]));
        if (!json) {
            snprintf(err, cap, "Could not parse shipped packet-offset file");
            valid = false;
        } else
            valid = ghost_packet_offsets_from_json(c, file, json, err, cap);
        cJSON_Delete(json);
    }
    return valid;
#endif
}

const uint8_t *ghost_packet_mapping_shipped(unsigned slot, size_t *length) {
#ifndef ESP_PLATFORM
    (void)slot;
    if (length)
        *length = 0;
    return NULL;
#else
    static const uint8_t *starts[] = {packet_offset_001_start, packet_offset_002_start,
                                      packet_offset_003_start};
    static const uint8_t *ends[] = {packet_offset_001_end, packet_offset_002_end,
                                    packet_offset_003_end};
    if (slot >= 3 || !length)
        return NULL;
    *length = (size_t)(ends[slot] - starts[slot]);
    return starts[slot];
#endif
}
