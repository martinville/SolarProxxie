#include "app.h"
#include "lwip/inet.h"
#include "ping/ping_sock.h"
#include <stdlib.h>
static atomic_uint state, latency;
static atomic_ullong checked;
static atomic_uint consecutive_successes, consecutive_failures;
TaskHandle_t ghost_reachability_task;
static void success(esp_ping_handle_t h, void *arg) {
    uint32_t ms;
    esp_ping_get_profile(h, ESP_PING_PROF_TIMEGAP, &ms, sizeof(ms));
    atomic_store(&latency, ms);
    atomic_store(&state, 1);
    atomic_store(&checked, ghost_millis());
    atomic_store(&consecutive_failures, 0);
    atomic_fetch_add(&consecutive_successes, 1);
    /* A successful probe is positive evidence of recovery. Requiring two here
     * kept every normal boot in fallback for more than a minute. */
    ghost_network_set_offline(false);
}
static void timeout(esp_ping_handle_t h, void *arg) {
    atomic_store(&state, 2);
    atomic_store(&checked, ghost_millis());
    atomic_store(&consecutive_successes, 0);
    if (atomic_fetch_add(&consecutive_failures, 1) + 1 >= 2)
        ghost_network_set_offline(true);
}
static void probe(void *arg) {
    for (;;) {
        bool enabled;
        char target[16];
        ghost_config_probe(&enabled, target);
        if (!enabled) {
            atomic_store(&state, 0);
            atomic_store(&consecutive_successes, 0);
            atomic_store(&consecutive_failures, 0);
            if (atomic_load(&ghost_sta_up))
                ghost_network_set_offline(false);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (!atomic_load(&ghost_sta_up)) {
            /* The Wi-Fi event owns the disconnected transition. Poll quickly so
             * the first Internet probe runs as soon as a link obtains an IP. */
            atomic_store(&state, 0);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        {
            esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
            cfg.count = 1;
            cfg.timeout_ms = 1500;
            cfg.task_stack_size = 2048;
            cfg.data_size = 8;
            cfg.interface = esp_netif_get_netif_impl_index(ghost_sta);
            ipaddr_aton(target, &cfg.target_addr);
            esp_ping_callbacks_t callbacks = {.on_ping_success = success,
                                              .on_ping_timeout = timeout};
            esp_ping_handle_t h;
            if (esp_ping_new_session(&cfg, &callbacks, &h) == ESP_OK) {
                esp_ping_start(h);
                vTaskDelay(pdMS_TO_TICKS(2500));
                esp_ping_stop(h);
                esp_ping_delete_session(h);
            } else {
                /* Socket pressure is not evidence of an Internet outage. Retry
                 * promptly instead of leaving a stale state for another 30s. */
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
void ghost_reachability_start(void) {
    xTaskCreate(probe, "reachability", 2560, NULL, 1, &ghost_reachability_task);
}
cJSON *ghost_reachability_status(void) {
    cJSON *j = cJSON_CreateObject();
    unsigned s = atomic_load(&state);
    cJSON_AddStringToObject(j, "status",
                            s == 1   ? "probe reachable"
                            : s == 2 ? "probe did not reply"
                                     : "unknown / disabled");
    cJSON_AddNumberToObject(j, "latency_ms", atomic_load(&latency));
    cJSON_AddNumberToObject(j, "checked_ms", atomic_load(&checked));
    cJSON_AddNumberToObject(j, "consecutive_successes", atomic_load(&consecutive_successes));
    cJSON_AddNumberToObject(j, "consecutive_failures", atomic_load(&consecutive_failures));
    cJSON_AddBoolToObject(j, "offline_mode", atomic_load(&ghost_offline_active));
    cJSON_AddStringToObject(j, "meaning",
                            "ICMP reachability only; not proof of inverter cloud health");
    return j;
}
