#include "app.h"
#include "esp_app_desc.h"
#include "esp_mac.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "mqtt/discovery.h"
#include "mqtt_client.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
TaskHandle_t ghost_mqtt_task;
static esp_mqtt_client_handle_t client;
static QueueHandle_t acknowledgements;
static atomic_bool connected, reload_requested, discovery_requested, force_requested,
    telemetry_pending;
static atomic_uint sent, failures, last_entities, last_success, last_failed;
static atomic_uint telemetry_notifications, state_source_frames, state_publish_trigger;
static atomic_ullong last_publish, last_connection;
static atomic_uint broker_event, broker_error_type, broker_connect_code, state_publish_result,
    state_message_id, state_payload_bytes;
static atomic_int broker_socket_errno, broker_tls_error;
static atomic_ullong broker_event_ms, state_source_ms, state_attempt_ms, state_ack_ms,
    last_telemetry_ms;
static SemaphoreHandle_t status_lock;
static char state_topic[192], state_payload[4096];
static char device_id[24];
static atomic_bool test_busy, reconnect_command;
static atomic_uint test_result;
static char birth_topic[96];
static char active_base[64];
typedef enum {
    PUBLISH_NOT_ATTEMPTED,
    PUBLISH_REQUESTED,
    PUBLISH_SENDING,
    PUBLISH_ACKNOWLEDGED,
    PUBLISH_NOT_CONNECTED,
    PUBLISH_ENQUEUE_FAILED,
    PUBLISH_ACK_TIMEOUT,
    PUBLISH_PAYLOAD_FAILED,
    PUBLISH_NO_FRESH_DATA
} publish_result_t;
typedef enum {
    TRIGGER_NONE,
    TRIGGER_TELEMETRY,
    TRIGGER_MANUAL,
    TRIGGER_RETRY,
    TRIGGER_INTERVAL
} publish_trigger_t;
static void wake_mqtt(void) {
    if (ghost_mqtt_task)
        xTaskNotifyGive(ghost_mqtt_task);
}
static bool priority_pending(void) {
    return atomic_load(&telemetry_pending) || atomic_load(&force_requested);
}
static void event(void *a, esp_event_base_t base, int32_t id, void *data) {
    esp_mqtt_event_handle_t e = data;
    if (id == MQTT_EVENT_CONNECTED) {
        atomic_store(&broker_event, MQTT_EVENT_CONNECTED);
        atomic_store(&broker_event_ms, ghost_millis());
        atomic_store(&broker_error_type, 0);
        atomic_store(&broker_connect_code, 0);
        atomic_store(&broker_socket_errno, 0);
        atomic_store(&broker_tls_error, 0);
        atomic_store(&connected, true);
        atomic_store(&discovery_requested, true);
        atomic_store(&force_requested, true);
        wake_mqtt();
        atomic_store(&last_connection, ghost_millis());
        esp_mqtt_client_subscribe(e->client, birth_topic, 0);
    }
    if (id == MQTT_EVENT_DATA && e->topic_len == (int)strlen(birth_topic) &&
        !memcmp(e->topic, birth_topic, e->topic_len) && e->data_len == 6 &&
        !memcmp(e->data, "online", 6)) {
        atomic_store(&discovery_requested, true);
        atomic_store(&force_requested, true);
        wake_mqtt();
    }
    if (id == MQTT_EVENT_DISCONNECTED || id == MQTT_EVENT_ERROR) {
        atomic_store(&connected, false);
        atomic_store(&broker_event, id);
        atomic_store(&broker_event_ms, ghost_millis());
    }
    if (id == MQTT_EVENT_ERROR && e->error_handle) {
        atomic_store(&broker_error_type, e->error_handle->error_type);
        atomic_store(&broker_connect_code, e->error_handle->connect_return_code);
        atomic_store(&broker_socket_errno, e->error_handle->esp_transport_sock_errno);
        atomic_store(&broker_tls_error, e->error_handle->esp_tls_last_esp_err);
    }
    if (id == MQTT_EVENT_PUBLISHED)
        xQueueSend(acknowledgements, &e->msg_id, 0);
}
static publish_result_t publish_result(const char *topic, const char *value, bool retain,
                                       int *message_id) {
    if (message_id)
        *message_id = 0;
    if (!client || !atomic_load(&connected))
        return PUBLISH_NOT_CONNECTED;
    int id = esp_mqtt_client_enqueue(client, topic, value, 0, 1, retain, true);
    if (message_id)
        *message_id = id;
    if (id < 0) {
        atomic_fetch_add(&failures, 1);
        return PUBLISH_ENQUEUE_FAILED;
    }
    uint64_t deadline = ghost_millis() + 2000;
    int ack;
    while (atomic_load(&connected) && ghost_millis() < deadline) {
        if (xQueueReceive(acknowledgements, &ack, pdMS_TO_TICKS(100)) == pdTRUE && ack == id) {
            atomic_fetch_add(&sent, 1);
            return PUBLISH_ACKNOWLEDGED;
        }
    }
    atomic_fetch_add(&failures, 1);
    return PUBLISH_ACK_TIMEOUT;
}
static bool publish(const char *topic, const char *value, bool retain) {
    return publish_result(topic, value, retain, NULL) == PUBLISH_ACKNOWLEDGED;
}
/* MQTT owns the config lock while preparing a message, but releases it during
 * the potentially long broker acknowledgement wait so settings can still save. */
static publish_result_t publish_result_with_config_unlocked(const char *topic,
                                                            const char *value, bool retain,
                                                            int *message_id) {
    ghost_config_unlock();
    publish_result_t result = publish_result(topic, value, retain, message_id);
    ghost_config_lock();
    return result;
}
static bool publish_with_config_unlocked(const char *topic, const char *value, bool retain) {
    return publish_result_with_config_unlocked(topic, value, retain, NULL) == PUBLISH_ACKNOWLEDGED;
}
static void discovery_topic(char *out, size_t n, const ghost_config_t *c, unsigned slot, size_t i) {
    snprintf(out, n, "%s/sensor/%s_%u/%.39s/config", c->discovery_prefix, device_id, slot + 1,
             ghost_fields[i].id);
}
static bool discovery_field(const ghost_config_t *c, unsigned slot, size_t i, bool remove) {
    char topic[192];
    discovery_topic(topic, sizeof(topic), c, slot, i);
    if (remove || !c->discovery || !c->entities[i].enabled)
        return publish_with_config_unlocked(topic, "", true);
    cJSON *j = ghost_discovery_json(c, i, device_id, slot, esp_app_get_description()->version);
    if (!j)
        return false;
    char *json = cJSON_PrintUnformatted(j);
    bool ok = json && publish_with_config_unlocked(topic, json, true);
    free(json);
    cJSON_Delete(j);
    return ok;
}
static void mqtt_task(void *arg) {
    bool need_config = true;
    typedef struct {
        bool available;
        uint64_t last;
        uint64_t attempted_source;
        uint64_t published_source;
    } publication_t;
    publication_t *states = calloc(GHOST_DONGLES_MAX, sizeof(*states));
    if (!states) {
        vTaskDelete(NULL);
        return;
    }
    for (;;) {
        if (atomic_exchange(&reload_requested, false))
            need_config = true;
        if (need_config) {
            if (client) {
                if (atomic_load(&connected)) {
                    /* Do not hold reconnection behind deletion of every retained entity. The
                       stable discovery topics are overwritten/removed after reconnect. */
                    char topic[128];
                    snprintf(topic, sizeof(topic), "%s/%s/availability", active_base, device_id);
                    publish(topic, "offline", true);
                }
                esp_mqtt_client_stop(client);
                esp_mqtt_client_destroy(client);
                client = NULL;
                atomic_store(&connected, false);
                xQueueReset(acknowledgements);
            }
            const ghost_config_t *c = ghost_config_lock();
            snprintf(active_base, sizeof(active_base), "%s", c->base);
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
                                                .buffer.size = 4096,
                                                .outbox.limit = 8192};
                client = esp_mqtt_client_init(&cfg);
                if (client) {
                    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, event, NULL);
                    esp_mqtt_client_start(client);
                }
            }
            ghost_config_unlock();
        }
        if (atomic_exchange(&reconnect_command, false) && client)
            esp_mqtt_client_reconnect(client);
        if (!atomic_load(&connected)) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
            continue;
        }
        const ghost_config_t *c = ghost_config_lock();
        bool discover = atomic_exchange(&discovery_requested, false);
        bool force = atomic_exchange(&force_requested, false);
        atomic_exchange(&telemetry_pending, false);
        bool attempted_any = false;
        uint64_t newest_source = 0;
        for (unsigned d = 0; d < GHOST_DONGLES_MAX && atomic_load(&connected); d++) {
            publication_t *state = &states[d];
            ghost_source_t source;
            ghost_telemetry_get(c->dongles[d].ip, &source);
            if (source.updated > newest_source)
                newest_source = source.updated;
            ghost_values_t values = source.values;
            /* A valid bit alone must never turn NaN or infinity into an MQTT state. */
            for (size_t i = 0; i < ghost_field_count; i++)
                if ((values.valid & (UINT64_C(1) << i)) && !isfinite(values.value[i]))
                    values.valid &= ~(UINT64_C(1) << i);
            ghost_values_t *v = &values;
            uint64_t now = ghost_millis();
            bool fresh = c->dongles[d].ip[0] && ghost_source_fresh(&source, now, c->stale_seconds);
            if (!c->dongles[d].ip[0])
                continue;
            fresh = c->dongles[d].ip[0] &&
                    ghost_source_fresh(&source, ghost_millis(), c->stale_seconds);
            bool pending = source.updated && source.updated != state->published_source;
            bool first_attempt = pending && source.updated != state->attempted_source;
            bool retry = pending && !first_attempt &&
                         now - state->last >= (uint64_t)c->min_interval * 1000;
            bool interval = state->last &&
                            now - state->last >= (uint64_t)c->max_interval * 1000;
            if (fresh && (force || first_attempt || retry || interval)) {
                attempted_any = true;
                publish_trigger_t trigger = force          ? TRIGGER_MANUAL
                                            : first_attempt ? TRIGGER_TELEMETRY
                                            : retry         ? TRIGGER_RETRY
                                                            : TRIGGER_INTERVAL;
                unsigned entities = 0;
                for (size_t i = 0; i < ghost_field_count; i++)
                    if (c->entities[i].enabled)
                        entities++;
                char topic[192];
                char *payload = ghost_snapshot_json(c, v);
                bool ready = payload && ghost_source_fresh(&source, ghost_millis(), c->stale_seconds) &&
                             ghost_mapped_topic(topic, sizeof(topic), c, d, 0);
                int message_id = 0;
                publish_result_t result = PUBLISH_PAYLOAD_FAILED;
                atomic_store(&state_source_ms, source.updated);
                atomic_store(&state_source_frames, source.frames);
                atomic_store(&state_attempt_ms, ghost_millis());
                atomic_store(&state_ack_ms, 0);
                atomic_store(&state_publish_trigger, trigger);
                if (ready) {
                    if (status_lock && xSemaphoreTake(status_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
                        snprintf(state_topic, sizeof(state_topic), "%s", topic);
                        snprintf(state_payload, sizeof(state_payload), "%s", payload);
                        xSemaphoreGive(status_lock);
                    }
                    atomic_store(&state_payload_bytes, strlen(payload));
                    atomic_store(&state_publish_result, PUBLISH_SENDING);
                    result = publish_result_with_config_unlocked(topic, payload, c->retain,
                                                                 &message_id);
                    atomic_store(&state_message_id, message_id > 0 ? (unsigned)message_id : 0);
                    if (result == PUBLISH_ACKNOWLEDGED)
                        atomic_store(&state_ack_ms, ghost_millis());
                }
                atomic_store(&state_publish_result, result);
                bool ok = result == PUBLISH_ACKNOWLEDGED;
                free(payload);
                atomic_store(&last_entities, entities);
                atomic_store(&last_success, ok ? entities : 0);
                atomic_store(&last_failed, ok ? 0 : entities);
                atomic_store(&last_publish, ghost_millis());
                state->last = ghost_millis();
                state->attempted_source = source.updated;
                if (ok)
                    state->published_source = source.updated;
            }
            /* State is deliberately published before availability and discovery. */
            if (fresh != state->available && !priority_pending()) {
                char topic[128];
                snprintf(topic, sizeof(topic), "%s/%s_%u/inverter_availability", c->base, device_id,
                         d + 1);
                if (publish_with_config_unlocked(topic, fresh ? "online" : "offline", true))
                    state->available = fresh;
                else
                    atomic_store(&discovery_requested, true);
            }
        }
        if (force && !attempted_any) {
            atomic_store(&state_source_ms, newest_source);
            atomic_store(&state_attempt_ms, ghost_millis());
            atomic_store(&state_ack_ms, 0);
            atomic_store(&state_message_id, 0);
            atomic_store(&state_payload_bytes, 0);
            atomic_store(&state_publish_trigger, TRIGGER_MANUAL);
            atomic_store(&state_publish_result, PUBLISH_NO_FRESH_DATA);
            if (status_lock && xSemaphoreTake(status_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
                state_topic[0] = 0;
                state_payload[0] = 0;
                xSemaphoreGive(status_lock);
            }
        }
        if (discover && atomic_load(&connected)) {
            bool failed = false;
            char topic[192];
            snprintf(topic, sizeof(topic), "%s/%s/availability", c->base, device_id);
            if (priority_pending() || !publish_with_config_unlocked(topic, "online", true))
                failed = true;
            /* Discovery is background work. A new dataset/manual request interrupts it. */
            for (size_t i = 0; i < ghost_field_count && !failed; i++) {
                if (priority_pending()) {
                    failed = true;
                    break;
                }
                snprintf(topic, sizeof(topic), "%s/sensor/%s/%.39s/config", c->discovery_prefix,
                         device_id, ghost_fields[i].id);
                if (!publish_with_config_unlocked(topic, "", true))
                    failed = true;
            }
            for (unsigned d = 0; d < GHOST_DONGLES_MAX && !failed; d++)
                for (size_t i = 0; i < ghost_field_count; i++) {
                    if (priority_pending()) {
                        failed = true;
                        break;
                    }
                    /* Freshness belongs to availability; configured entities remain discoverable
                       while their inverter is offline or waiting for its first packet. */
                    bool remove = !ghost_discovery_field_configured(c, d, i);
                    if (!discovery_field(c, d, i, remove)) {
                        failed = true;
                        break;
                    }
                }
            if (failed)
                atomic_store(&discovery_requested, true);
        }
        ghost_config_unlock();
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
    }
}
void ghost_mqtt_start(void) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(device_id, sizeof(device_id), "ghost_%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);
    atomic_store(&broker_event, MQTT_EVENT_BEFORE_CONNECT);
    acknowledgements = xQueueCreate(16, sizeof(int));
    status_lock = xSemaphoreCreateMutex();
    if (acknowledgements && status_lock)
        xTaskCreatePinnedToCore(mqtt_task, "telemetry_mqtt", 6144, NULL, 3, &ghost_mqtt_task, 1);
}
void ghost_mqtt_reload(void) {
    atomic_store(&reload_requested, true);
    wake_mqtt();
}
void ghost_mqtt_notify_telemetry(void) {
    atomic_store(&telemetry_pending, true);
    atomic_fetch_add(&telemetry_notifications, 1);
    atomic_store(&last_telemetry_ms, ghost_millis());
    wake_mqtt();
}
void ghost_mqtt_command(const char *cmd) {
    if (!strcmp(cmd, "discovery"))
        atomic_store(&discovery_requested, true);
    else if (!strcmp(cmd, "publish")) {
        atomic_store(&force_requested, true);
        atomic_store(&state_attempt_ms, ghost_millis());
        atomic_store(&state_ack_ms, 0);
        atomic_store(&state_message_id, 0);
        atomic_store(&state_publish_trigger, TRIGGER_MANUAL);
        atomic_store(&state_publish_result,
                     atomic_load(&connected) ? PUBLISH_REQUESTED : PUBLISH_NOT_CONNECTED);
    } else if (!strcmp(cmd, "test"))
        atomic_store(&reconnect_command, true);
    wake_mqtt();
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
    cJSON_AddBoolToObject(j, "offline_mode", atomic_load(&ghost_offline_active));
    cJSON_AddBoolToObject(j, "dongle_cloud_forwarding", atomic_load(&ghost_cloud_enabled));
    cJSON_AddStringToObject(j, "device_id", device_id);
    cJSON_AddNumberToObject(j, "now_ms", ghost_millis());
    static const char *results[] = {"not tested", "testing", "connected", "failed", "timed out"};
    cJSON_AddStringToObject(j, "test_result", results[atomic_load(&test_result)]);
#define N(k, v) cJSON_AddNumberToObject(j, k, atomic_load(&v))
    N("messages_sent", sent);
    N("failures", failures);
    N("last_publish_ms", last_publish);
    N("last_connection_ms", last_connection);
    N("telemetry_notifications", telemetry_notifications);
    N("last_telemetry_ms", last_telemetry_ms);
    N("entities", last_entities);
    N("succeeded", last_success);
    N("failed", last_failed);
    static const char *publish_results[] = {
        "not attempted",          "manual publish requested", "sending; waiting for PUBACK",
        "acknowledged by broker", "not connected",            "enqueue failed",
        "PUBACK timed out",       "could not build JSON payload",
        "no fresh mapped dongle data"};
    static const char *publish_triggers[] = {"none", "new dongle dataset", "manual button",
                                             "retry after failure", "maximum interval refresh"};
    unsigned result = atomic_load(&state_publish_result);
    cJSON *state = cJSON_AddObjectToObject(j, "last_state_publish");
    cJSON_AddStringToObject(state, "result",
                            result < sizeof(publish_results) / sizeof(publish_results[0])
                                ? publish_results[result]
                                : "unknown");
    cJSON_AddNumberToObject(state, "attempt_ms", atomic_load(&state_attempt_ms));
    cJSON_AddNumberToObject(state, "source_updated_ms", atomic_load(&state_source_ms));
    cJSON_AddNumberToObject(state, "ack_ms", atomic_load(&state_ack_ms));
    cJSON_AddNumberToObject(state, "message_id", atomic_load(&state_message_id));
    cJSON_AddNumberToObject(state, "payload_bytes", atomic_load(&state_payload_bytes));
    cJSON_AddNumberToObject(state, "source_frame", atomic_load(&state_source_frames));
    unsigned trigger = atomic_load(&state_publish_trigger);
    cJSON_AddStringToObject(state, "trigger",
                            trigger < sizeof(publish_triggers) / sizeof(publish_triggers[0])
                                ? publish_triggers[trigger]
                                : "unknown");
    if (status_lock && xSemaphoreTake(status_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        cJSON_AddStringToObject(state, "topic", state_topic);
        cJSON_AddStringToObject(state, "payload", state_payload);
        xSemaphoreGive(status_lock);
    } else {
        cJSON_AddStringToObject(state, "topic", "status busy");
        cJSON_AddStringToObject(state, "payload", "");
    }
    static const char *broker_events[] = {"error", "connected", "disconnected", "subscribed",
                                         "unsubscribed", "published", "data", "before connect",
                                         "deleted", "user event"};
    int event_id = (int)atomic_load(&broker_event);
    cJSON *broker = cJSON_AddObjectToObject(j, "broker");
    cJSON_AddStringToObject(broker, "last_event",
                            event_id >= 0 && event_id < (int)(sizeof(broker_events) /
                                                               sizeof(broker_events[0]))
                                ? broker_events[event_id]
                                : "unknown");
    cJSON_AddNumberToObject(broker, "event_ms", atomic_load(&broker_event_ms));
    cJSON_AddNumberToObject(broker, "error_type", atomic_load(&broker_error_type));
    cJSON_AddNumberToObject(broker, "connect_return_code", atomic_load(&broker_connect_code));
    cJSON_AddNumberToObject(broker, "socket_errno", atomic_load(&broker_socket_errno));
    cJSON_AddNumberToObject(broker, "tls_error", atomic_load(&broker_tls_error));
    cJSON_AddStringToObject(j, "measurement",
                            "QoS 1 broker acknowledgements; not HA processing time");
    return j;
#undef N
}
