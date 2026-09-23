#pragma once
#include "config/config.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "protocol/telemetry_store.h"
#include "protocol/dongle_identity.h"
#include "protocol/cloud_emulator.h"
#include <stdatomic.h>
extern bool ghost_setup;
extern esp_netif_t *ghost_ap, *ghost_sta;
extern atomic_bool ghost_sta_up, ghost_nat_active, ghost_cloud_enabled, ghost_offline_active;
extern atomic_uint ghost_gui_target;
extern TaskHandle_t ghost_decoder_task, ghost_mqtt_task, ghost_health_task;
extern TaskHandle_t ghost_dns_task, ghost_wifi_task, ghost_reachability_task;
uint64_t ghost_millis(void);
void ghost_restart_later(void);
esp_err_t ghost_ota_upload(httpd_req_t *r);
cJSON *ghost_cloud_update_status(void);
esp_err_t ghost_cloud_update_check(char *error_text, size_t error_capacity);
esp_err_t ghost_cloud_update_start(const char *confirmed_version, char *error_text,
                                   size_t error_capacity);
esp_err_t ghost_network_start(const ghost_config_t *c);
void ghost_network_cloud(bool enable);
void ghost_network_set_offline(bool offline);
void ghost_network_restart(void);
bool ghost_dongle_gui_set(const char *ip);
void ghost_dongle_gui_poll(void);
cJSON *ghost_dongle_gui_status(void);
cJSON *ghost_network_status(void);
cJSON *ghost_network_scan(void);
esp_err_t ghost_network_test(const ghost_config_t *c);
bool ghost_network_test_matches(const ghost_config_t *c);
void ghost_dns_start(void);
cJSON *ghost_dns_status(void);
void ghost_reachability_start(void);
cJSON *ghost_reachability_status(void);
esp_err_t ghost_mqtt_test(const ghost_config_t *c);
void ghost_router_start(void);
void ghost_router_config(const ghost_config_t *c);
bool ghost_router_local_for_ip(uint32_t ip);
bool ghost_router_local_enabled(void);
cJSON *ghost_router_status(void);
void ghost_telemetry_get(const char *ip, ghost_source_t *out);
bool ghost_dongle_identity_get(const char *ip, ghost_dongle_identity_t *out,
                               uint32_t *observed_seconds_ago);
esp_err_t ghost_capture_stream_start(httpd_req_t *r);
void ghost_capture_stream_stop(void);
cJSON *ghost_packet_list(void);
void ghost_offline_start(void);
void ghost_offline_note_dns(uint32_t ip, ghost_cloud_role_t role);
void ghost_offline_reset_clients(void);
cJSON *ghost_offline_status(void);
void ghost_mqtt_start(void);
void ghost_mqtt_reload(void);
void ghost_mqtt_command(const char *command);
cJSON *ghost_mqtt_status(void);
void ghost_web_start(void);
void ghost_diagnostics_start(void);
cJSON *ghost_diagnostics_status(void);
