#include "app.h"
#include "esp_app_desc.h"
#include "esp_mac.h"
#include "freertos/queue.h"
#include "mqtt/discovery.h"
#include "mqtt_client.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
TaskHandle_t ghost_mqtt_task;
static esp_mqtt_client_handle_t client;
static QueueHandle_t acknowledgements;
static atomic_bool connected, reload_requested, discovery_requested, force_requested;
static atomic_uint sent, failures, last_entities, last_success, last_failed;
static atomic_ullong last_publish, last_connection;
static char device_id[24];
static atomic_bool test_busy, reconnect_command;
static atomic_uint test_result;
static char birth_topic[96];
static void event(void *a, esp_event_base_t base, int32_t id, void *data) {
    esp_mqtt_event_handle_t e = data;
    if (id == MQTT_EVENT_CONNECTED) {
        atomic_store(&connected, true);
        atomic_store(&discovery_requested, true);
        atomic_store(&force_requested, true);
        atomic_store(&last_connection, ghost_millis());
        esp_mqtt_client_subscribe(e->client, birth_topic, 0);
    }
    if (id == MQTT_EVENT_DATA && e->topic_len == (int)strlen(birth_topic) &&
        !memcmp(e->topic, birth_topic, e->topic_len) && e->data_len == 6 &&
        !memcmp(e->data, "online", 6)) {
        atomic_store(&discovery_requested, true);
        atomic_store(&force_requested, true);
    }
    if (id == MQTT_EVENT_DISCONNECTED || id == MQTT_EVENT_ERROR)
        atomic_store(&connected, false);
    if (id == MQTT_EVENT_PUBLISHED)
        xQueueSend(acknowledgements, &e->msg_id, 0);
}
static bool publish(const char *topic, const char *value, bool retain) {
    if (!client || !atomic_load(&connected))
        return false;
    int id = esp_mqtt_client_enqueue(client, topic, value, 0, 1, retain, true);
    if (id < 0) {
        atomic_fetch_add(&failures, 1);
        return false;
    }
    uint64_t deadline = ghost_millis() + 2000;
    int ack;
    while (atomic_load(&connected) && ghost_millis() < deadline) {
        if (xQueueReceive(acknowledgements, &ack, pdMS_TO_TICKS(100)) == pdTRUE && ack == id) {
            atomic_fetch_add(&sent, 1);
            return true;
        }
    }
    atomic_fetch_add(&failures, 1);
    return false;
}
static void discovery_topic(char *out, size_t n, const ghost_config_t *c, unsigned slot, size_t i) {
    snprintf(out, n, "%s/sensor/%s_%u/%s/config", c->discovery_prefix, device_id, slot + 1,
             ghost_fields[i].id);
}
static bool discovery_field(const ghost_config_t *c, unsigned slot, size_t i, bool remove) {
    char topic[192];
    discovery_topic(topic, sizeof(topic), c, slot, i);
    if (remove || !c->discovery || !c->entities[i].enabled)
        return publish(topic, "", true);
    cJSON *j = ghost_discovery_json(c, i, device_id, slot, esp_app_get_description()->version);
    if (!j)
        return false;
    char *json = cJSON_PrintUnformatted(j);
    bool ok = json && publish(topic, json, true);
    free(json);
    cJSON_Delete(j);
    return ok;
}
static void mqtt_task(void *arg) {
    ghost_config_t *c = malloc(sizeof(*c));
    if (!c) {
        vTaskDelete(NULL);
        return;
    }
    bool need_config = true;
    typedef struct {
        bool available;
        uint64_t last;
        ghost_values_t previous;
    } publication_t;
    publication_t *states = calloc(GHOST_DONGLES_MAX, sizeof(*states));
    if (!states) {
        free(c);
        vTaskDelete(NULL);
        return;
    }
    for (;;) {
        if (atomic_exchange(&reload_requested, false))
            need_config = true;
        if (need_config) {
            if (client) {
                if (atomic_load(&connected)) {
                    for (unsigned d = 0; d < GHOST_DONGLES_MAX; d++) {
                        if (!c->dongles[d].ip[0])
                            continue;
                        char old_avail[128];
                        snprintf(old_avail, sizeof(old_avail), "%s/%s_%u/inverter_availability",
                                 c->base, device_id, d + 1);
                        publish(old_avail, "offline", true);
                        for (size_t i = 0; i < ghost_field_count; i++) {
                            if (!discovery_field(c, d, i, true))
                                break;
                            char old_state[192];
                            if (ghost_mapped_topic(old_state, sizeof(old_state), c, d, i))
                                publish(old_state, "", true);
                        }
                    }
                    char topic[128];
                    snprintf(topic, sizeof(topic), "%s/%s/availability", c->base, device_id);
                    publish(topic, "offline", true);
                }
                esp_mqtt_client_stop(client);
                esp_mqtt_client_destroy(client);
                client = NULL;
                atomic_store(&connected, false);
                xQueueReset(acknowledgements);
            }
            ghost_config_get(c);
            snprintf(birth_topic, sizeof(birth_topic), "%s/status", c->discovery_prefix);
            need_config = false;
            memset(states, 0, GHOST_DONGLES_MAX * sizeof(*states));
            if (c->mqtt_enabled) {
                char will[128];
                snprintf(will, sizeof(will), "%s/%s/availability", c->base, device_id);
                esp_mqtt_client_config_t cfg = {.broker.address.hostname = c->broker,
                                                .broker.address.port = c->port,
                                                .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
                                                .credentials.client_id = c->client,
                                                .credentials.username =
                                                    c->mqtt_user[0] ? c->mqtt_user : NULL,
                                                .credentials.authentication.password =
                                                    c->mqtt_password[0] ? c->mqtt_password : NULL,
                                                .session.keepalive = c->keepalive,
                                                .session.last_will.topic = will,
                                                .session.last_will.msg = "offline",
                                                .session.last_will.qos = 1,
                                                .session.last_will.retain = 1,
                                                .network.reconnect_timeout_ms = 10000,
                                                .network.timeout_ms = 3000,
                                                .task.priority = 3,
                                                .task.stack_size = 4096,
                                                .buffer.size = 2048,
                                                .outbox.limit = 8192};
                client = esp_mqtt_client_init(&cfg);
                if (client) {
                    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, event, NULL);
                    esp_mqtt_client_start(client);
                }
            }
        }
        if (atomic_exchange(&reconnect_command, false) && client)
            esp_mqtt_client_reconnect(client);
        if (!atomic_load(&connected)) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        bool discover = atomic_exchange(&discovery_requested, false);
        bool force = atomic_exchange(&force_requested, false);
        if (discover) {
            char topic[192];
            snprintf(topic, sizeof(topic), "%s/%s/availability", c->base, device_id);
            if (!publish(topic, "online", true))
                atomic_store(&discovery_requested, true);
            /* Remove the old single-inverter discovery namespace on upgrade. */
            for (size_t i = 0; i < ghost_field_count; i++) {
                snprintf(topic, sizeof(topic), "%s/sensor/%s/%s/config", c->discovery_prefix,
                         device_id, ghost_fields[i].id);
                if (!publish(topic, "", true)) {
                    atomic_store(&discovery_requested, true);
                    break;
                }
            }
        }
        for (unsigned d = 0; d < GHOST_DONGLES_MAX && atomic_load(&connected); d++) {
            publication_t *state = &states[d];
            ghost_source_t source;
            ghost_telemetry_get(c->dongles[d].ip, &source);
            ghost_values_t values = source.values;
            /* A valid bit alone must never turn NaN or infinity into an MQTT state. */
            for (size_t i = 0; i < ghost_field_count; i++)
                if ((values.valid & (UINT64_C(1) << i)) && !isfinite(values.value[i]))
                    values.valid &= ~(UINT64_C(1) << i);
            ghost_values_t *v = &values;
            uint64_t now = ghost_millis();
            bool fresh = c->dongles[d].ip[0] && ghost_source_fresh(&source, now, c->stale_seconds);
            if (discover) {
                for (size_t i = 0; i < ghost_field_count; i++) {
                    /* Discovery describes configured entities. Telemetry freshness belongs to the
                       availability topic and must not delete an HA device while an inverter is
                       offline or waiting for its first packet. */
                    bool remove = !ghost_discovery_field_configured(c, d, i);
                    if (!discovery_field(c, d, i, remove)) {
                        atomic_store(&discovery_requested, true);
                        break;
                    }
                    if (remove && c->dongles[d].ip[0]) {
                        char topic[192];
                        if (ghost_mapped_topic(topic, sizeof(topic), c, d, i) &&
                            !publish(topic, "", true))
                            atomic_store(&discovery_requested, true);
                    }
                }
            }
            if (discover || fresh != state->available) {
                char topic[128];
                snprintf(topic, sizeof(topic), "%s/%s_%u/inverter_availability", c->base, device_id,
                         d + 1);
                if (publish(topic, fresh ? "online" : "offline", true))
                    state->available = fresh;
                else
                    atomic_store(&discovery_requested, true);
            }
            if (!c->dongles[d].ip[0])
                continue;
            bool changed = v->valid != state->previous.valid;
            for (size_t i = 0; i < ghost_field_count; i++)
                if ((v->valid & (UINT64_C(1) << i)) &&
                    fabs(v->value[i] - state->previous.value[i]) >=
                        fabs(ghost_fields[i].scale) * .5)
                    changed = true;
            fresh = c->dongles[d].ip[0] &&
                    ghost_source_fresh(&source, ghost_millis(), c->stale_seconds);
            if (fresh && (force || now - state->last >= (uint64_t)c->max_interval * 1000 ||
                          ((c->on_change ? changed : true) &&
                           now - state->last >= (uint64_t)c->min_interval * 1000))) {
                unsigned success = 0, failed = 0;
                /* Keep discovery stable if a value disappears. Clear only its retained state so
                   Home Assistant reports unknown rather than retaining an old measurement. */
                uint64_t lost = state->previous.valid & ~v->valid;
                for (size_t i = 0; i < ghost_field_count; i++)
                    if (c->entities[i].enabled && (lost & (UINT64_C(1) << i))) {
                        char topic[192];
                        if (ghost_mapped_topic(topic, sizeof(topic), c, d, i) &&
                            publish(topic, "", true))
                            success++;
                        else
                            failed++;
                    }
                for (size_t i = 0; i < ghost_field_count; i++)
                    if (c->entities[i].enabled && (v->valid & (UINT64_C(1) << i))) {
                        if (!ghost_source_fresh(&source, ghost_millis(), c->stale_seconds)) {
                            failed++;
                            break;
                        }
                        char topic[192], value[40];
                        snprintf(value, sizeof(value), "%.6g", v->value[i]);
                        if (ghost_mapped_topic(topic, sizeof(topic), c, d, i) &&
                            publish(topic, value, c->retain))
                            success++;
                        else
                            failed++;
                    }
                atomic_store(&last_entities, success + failed);
                atomic_store(&last_success, success);
                atomic_store(&last_failed, failed);
                atomic_store(&last_publish, ghost_millis());
                state->last = ghost_millis();
                if (!failed)
                    state->previous = *v;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
void ghost_mqtt_start(void) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(device_id, sizeof(device_id), "ghost_%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);
    acknowledgements = xQueueCreate(16, sizeof(int));
    if (acknowledgements)
        xTaskCreatePinnedToCore(mqtt_task, "telemetry_mqtt", 6144, NULL, 3, &ghost_mqtt_task, 1);
}
void ghost_mqtt_reload(void) {
    atomic_store(&reload_requested, true);
}
void ghost_mqtt_command(const char *cmd) {
    if (!strcmp(cmd, "discovery"))
        atomic_store(&discovery_requested, true);
    else if (!strcmp(cmd, "publish"))
        atomic_store(&force_requested, true);
    else if (!strcmp(cmd, "test"))
        atomic_store(&reconnect_command, true);
}
static void test_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (id == MQTT_EVENT_CONNECTED)
        atomic_store(&test_result, 2);
    else if (id == MQTT_EVENT_ERROR)
        atomic_store(&test_result, 3);
}
static void test_task(void *arg) {
    ghost_config_t *c = arg;
    char id[80];
    snprintf(id, sizeof(id), "%s-probe", c->client);
    esp_mqtt_client_config_t cfg = {.broker.address.hostname = c->broker,
                                    .broker.address.port = c->port,
                                    .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
                                    .credentials.client_id = id,
                                    .credentials.username = c->mqtt_user[0] ? c->mqtt_user : NULL,
                                    .credentials.authentication.password =
                                        c->mqtt_password[0] ? c->mqtt_password : NULL,
                                    .network.disable_auto_reconnect = true,
                                    .network.timeout_ms = 3000,
                                    .task.stack_size = 4096,
                                    .task.priority = 2};
    esp_mqtt_client_handle_t probe = esp_mqtt_client_init(&cfg);
    if (probe) {
        esp_mqtt_client_register_event(probe, ESP_EVENT_ANY_ID, test_event, NULL);
        esp_mqtt_client_start(probe);
        uint64_t end = ghost_millis() + 10000;
        while (atomic_load(&test_result) == 1 && ghost_millis() < end)
            vTaskDelay(pdMS_TO_TICKS(100));
        esp_mqtt_client_stop(probe);
        esp_mqtt_client_destroy(probe);
    }
    if (atomic_load(&test_result) == 1)
        atomic_store(&test_result, 4);
    memset(c, 0, sizeof(*c));
    free(c);
    atomic_store(&test_busy, false);
    vTaskDelete(NULL);
}
esp_err_t ghost_mqtt_test(const ghost_config_t *c) {
    if (!c->broker[0] || atomic_exchange(&test_busy, true))
        return ESP_FAIL;
    ghost_config_t *copy = malloc(sizeof(*copy));
    if (!copy) {
        atomic_store(&test_busy, false);
        return ESP_ERR_NO_MEM;
    }
    *copy = *c;
    atomic_store(&test_result, 1);
    if (xTaskCreate(test_task, "mqtt_probe", 3072, copy, 2, NULL) != pdPASS) {
        free(copy);
        atomic_store(&test_busy, false);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
cJSON *ghost_mqtt_status(void) {
    cJSON *j = cJSON_CreateObject();
    cJSON_AddBoolToObject(j, "connected", atomic_load(&connected));
    cJSON_AddStringToObject(j, "device_id", device_id);
    static const char *results[] = {"not tested", "testing", "connected", "failed", "timed out"};
    cJSON_AddStringToObject(j, "test_result", results[atomic_load(&test_result)]);
#define N(k, v) cJSON_AddNumberToObject(j, k, atomic_load(&v))
    N("messages_sent", sent);
    N("failures", failures);
    N("last_publish_ms", last_publish);
    N("last_connection_ms", last_connection);
    N("entities", last_entities);
    N("succeeded", last_success);
    N("failed", last_failed);
    cJSON_AddStringToObject(j, "measurement",
                            "QoS 1 broker acknowledgements; not HA processing time");
    return j;
#undef N
}
