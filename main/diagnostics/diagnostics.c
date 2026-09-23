#include "app.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_system.h"
#include "nvs.h"
void ghost_diagnostics_start(void) {}
cJSON *ghost_diagnostics_status(void) {
    cJSON *j = cJSON_CreateObject();
    const esp_app_desc_t *app = esp_app_get_description();
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash = 0;
    esp_flash_get_size(NULL, &flash);
    nvs_stats_t nvs = {0};
    nvs_get_stats(NULL, &nvs);
    cJSON_AddStringToObject(j, "version", app->version);
    cJSON_AddStringToObject(j, "git_commit", GHOST_GIT_COMMIT);
    cJSON_AddStringToObject(j, "build_date", app->date);
    cJSON_AddStringToObject(j, "build_time", app->time);
    cJSON_AddStringToObject(j, "idf", esp_get_idf_version());
    cJSON_AddStringToObject(j, "cpu", "ESP32 dual-core Xtensa LX6");
    cJSON_AddNumberToObject(j, "cores", chip.cores);
    cJSON_AddNumberToObject(j, "cpu_mhz", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    cJSON_AddNumberToObject(j, "flash_bytes", flash);
    cJSON_AddNumberToObject(j, "uptime_ms", ghost_millis());
    cJSON_AddNumberToObject(j, "heap_free", esp_get_free_heap_size());
    cJSON_AddNumberToObject(j, "heap_min", esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(j, "heap_largest", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(j, "reset_reason", esp_reset_reason());
    cJSON_AddNumberToObject(j, "nvs_used", nvs.used_entries);
    cJSON_AddNumberToObject(j, "nvs_free", nvs.free_entries);
    if (ghost_decoder_task)
        cJSON_AddNumberToObject(j, "decoder_stack_free",
                                uxTaskGetStackHighWaterMark(ghost_decoder_task));
    if (ghost_mqtt_task)
        cJSON_AddNumberToObject(j, "mqtt_stack_free", uxTaskGetStackHighWaterMark(ghost_mqtt_task));
#define STACK(name, task)                                                                            \
    do {                                                                                             \
        if (task)                                                                                    \
            cJSON_AddNumberToObject(j, name "_stack_free", uxTaskGetStackHighWaterMark(task));      \
    } while (0)
    STACK("health", ghost_health_task);
    STACK("dns", ghost_dns_task);
    STACK("wifi", ghost_wifi_task);
    STACK("reachability", ghost_reachability_task);
#undef STACK
    return j;
}
