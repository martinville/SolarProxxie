#include "app.h"
#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include <stdlib.h>
bool ghost_setup;
TaskHandle_t ghost_health_task;
uint64_t ghost_millis(void) {
    return esp_timer_get_time() / 1000;
}
static void led(bool on) {
#if CONFIG_GHOST_LED_GPIO >= 0
#ifdef CONFIG_GHOST_LED_ACTIVE_LOW
    gpio_set_level(CONFIG_GHOST_LED_GPIO, !on);
#else
    gpio_set_level(CONFIG_GHOST_LED_GPIO, on);
#endif
#endif
}
static void restart(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}
void ghost_restart_later(void) {
    xTaskCreate(restart, "restart", 2048, NULL, 2, NULL);
}
static void health(void *arg) {
    uint64_t pressed = 0;
    unsigned tick = 0;
    bool validated = false;
    esp_task_wdt_add(NULL);
    for (;;) {
        uint64_t now = ghost_millis();
        if (!gpio_get_level(GPIO_NUM_0)) {
            if (!pressed)
                pressed = now;
            if (now - pressed >= 10000) {
                ESP_LOGW("boot", "Physical factory reset");
                ghost_config_reset();
                esp_restart();
            }
        } else
            pressed = 0;
        bool light = pressed                      ? ((tick % 2) == 0)
                     : ghost_setup                ? ((tick % 10) < 5)
                     : atomic_load(&ghost_sta_up) ? (tick % 50 == 0)
                                                  : ((tick % 20 == 0) || (tick % 20 == 3));
        led(light);
        if (ghost_setup && now > 900000) {
            ESP_LOGI("boot", "Setup window expired");
            esp_restart();
        }
        if (!validated && now > 60000) {
            esp_ota_mark_app_valid_cancel_rollback();
            validated = true;
        }
        if (esp_get_free_heap_size() < 20000)
            ghost_capture_stream_stop();
        esp_task_wdt_reset();
        tick++;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
void app_main(void) {
    gpio_config_t button = {.pin_bit_mask = 1ULL << GPIO_NUM_0,
                            .mode = GPIO_MODE_INPUT,
                            .pull_up_en = GPIO_PULLUP_ENABLE};
    gpio_config(&button);
#if CONFIG_GHOST_LED_GPIO >= 0
    gpio_reset_pin(CONFIG_GHOST_LED_GPIO);
    gpio_set_direction(CONFIG_GHOST_LED_GPIO, GPIO_MODE_OUTPUT);
#endif
    ghost_diagnostics_start();
    ESP_LOGI("boot", "SolarProxxie v%s — Inverter Gateway / Monitor / MQTT Bridge",
             esp_app_get_description()->version);
    bool physical = false;
    for (unsigned i = 0; i < 50; i++) {
        led(i % 2 == 0);
        if (!gpio_get_level(GPIO_NUM_0))
            physical = true;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    bool valid = false;
    esp_err_t r = ghost_config_init(&valid);
    if (r != ESP_OK) {
        ESP_LOGE("boot", "Configuration load: %s. Hold BOOT for 10 seconds to reset.",
                 esp_err_to_name(r));
        /* Corrupted/future config must not expose an unprotected setup portal automatically. */
        uint64_t held = 0;
        for (;;) {
            uint64_t now = ghost_millis();
            led((now / 100) % 2);
            if (!gpio_get_level(GPIO_NUM_0)) {
                if (!held)
                    held = now;
                if (now - held >= 10000) {
                    ghost_config_reset();
                    esp_restart();
                }
            } else
                held = 0;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    ghost_setup = !valid || physical;
    ghost_config_t *c = malloc(sizeof(*c));
    if (!c) {
        ESP_LOGE("boot", "Insufficient heap");
        return;
    }
    ghost_config_get(c);
    ESP_ERROR_CHECK(ghost_network_start(c));
    ghost_router_config(c);
    ghost_router_start();
    if (!ghost_setup)
        ghost_offline_start();
    if (!ghost_setup)
        ghost_reachability_start();
    if (!ghost_setup)
        ghost_mqtt_start();
    ghost_web_start();
    xTaskCreate(health, "health", 2048, NULL, 2, &ghost_health_task);
    free(c);
    ESP_LOGI("boot", "SolarProxxie ready. Mode: %s", ghost_setup ? "setup" : "gateway");
}
