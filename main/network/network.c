#include "app.h"
#include "esp_netif_sntp.h"
#include "dhcpserver/dhcpserver.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include "mbedtls/sha256.h"
#include "network/clients.h"
#include <stdlib.h>
#include <string.h>
esp_netif_t *ghost_ap, *ghost_sta;
atomic_bool ghost_sta_up, ghost_nat_active, ghost_cloud_enabled, ghost_offline_active;
TaskHandle_t ghost_wifi_task;
static atomic_bool cloud_requested;
static atomic_bool reconnect_requested;
static atomic_uint retry = 1;
static atomic_ullong connect_started;
static atomic_bool testing;
static uint8_t tested_settings[32];
bool ghost_network_test_matches(const ghost_config_t *c) {
    uint8_t hash[32];
    mbedtls_sha256((const unsigned char *)c, offsetof(ghost_config_t, ap_ssid), hash, 0);
    return testing && atomic_load(&ghost_sta_up) && !memcmp(hash, tested_settings, 32);
}
static atomic_bool overlap;
static const char *overlap_error =
    "STA and AP subnets overlap; routing disabled. Enter physical Setup Mode.";
static void apply_dns(const ghost_config_t *c) {
    const char *dns[] = {c->dns1, c->dns2};
    for (unsigned i = 0; i < 2; i++)
        if (dns[i][0]) {
            esp_netif_dns_info_t d = {.ip.type = ESP_IPADDR_TYPE_V4};
            d.ip.u_addr.ip4.addr = inet_addr(dns[i]);
            esp_netif_set_dns_info(ghost_sta, i == 0 ? ESP_NETIF_DNS_MAIN : ESP_NETIF_DNS_BACKUP,
                                   &d);
        }
}
static void apply_cloud(void) {
    bool enable = atomic_load(&cloud_requested) && !atomic_load(&ghost_offline_active) &&
                  !atomic_load(&overlap) && !ghost_setup;
    atomic_store(&ghost_cloud_enabled, enable);
    if (!ghost_ap)
        return;
    if (!enable)
        ghost_dongle_gui_set(NULL);
    esp_err_t r;
    if (enable && !ghost_setup && atomic_load(&ghost_sta_up))
        r = esp_netif_napt_enable(ghost_ap);
    else
        r = esp_netif_napt_disable(ghost_ap);
    atomic_store(&ghost_nat_active,
                 enable && !ghost_setup && atomic_load(&ghost_sta_up) && r == ESP_OK);
}
void ghost_network_cloud(bool enable) {
    atomic_store(&cloud_requested, enable && !ghost_setup);
    apply_cloud();
}
void ghost_network_set_offline(bool offline) {
    if (atomic_exchange(&ghost_offline_active, offline) != offline) {
        ESP_LOGW("network", "Dongle cloud emulator %s", offline ? "enabled" : "disabled");
        apply_cloud();
    }
}
static void event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (!ghost_setup || testing)
            atomic_store(&reconnect_requested, true);
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        atomic_store(&ghost_sta_up, false);
        ghost_network_set_offline(true);
        ghost_dongle_gui_set(NULL);
        atomic_store(&ghost_nat_active, false);
        if (!ghost_setup || testing)
            atomic_store(&reconnect_requested, true);
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED)
        ghost_offline_reset_clients();
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        esp_netif_ip_info_t ap = {0};
        esp_netif_get_ip_info(ghost_ap, &ap);
        uint32_t common = e->ip_info.netmask.addr & ap.netmask.addr;
        if ((e->ip_info.ip.addr & common) == (ap.ip.addr & common)) {
            atomic_store(&overlap, true);
            ESP_LOGE("network", "%s", overlap_error);
            apply_cloud();
            return;
        }
        atomic_store(&overlap, false);
        atomic_store(&ghost_sta_up, true);
        retry = 1;
        apply_cloud();
        ESP_LOGI("network", "Home Wi-Fi connected. IP: " IPSTR, IP2STR(&e->ip_info.ip));
    }
}
static void reconnect_task(void *arg) {
    uint64_t due = 0;
    for (;;) {
        ghost_dongle_gui_poll();
        uint64_t now = ghost_millis();
        if (atomic_exchange(&reconnect_requested, false)) {
            due = now + (retry * 1000) + (esp_random() % 500);
            retry = retry < 60 ? retry * 2 : 60;
        }
        if (due && now >= due) {
            due = 0;
            esp_wifi_connect();
            connect_started = now;
        }
        if (!atomic_load(&ghost_sta_up) && connect_started && now - connect_started > 45000 &&
            (!ghost_setup || testing)) {
            connect_started = 0;
            esp_wifi_disconnect();
            atomic_store(&reconnect_requested, true);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
esp_err_t ghost_network_test(const ghost_config_t *c) {
    if (!ghost_setup)
        return ESP_ERR_INVALID_STATE;
    testing = true;
    mbedtls_sha256((const unsigned char *)c, offsetof(ghost_config_t, ap_ssid), tested_settings, 0);
    esp_wifi_disconnect();
    atomic_store(&ghost_sta_up, false);
    esp_netif_dhcpc_stop(ghost_sta);
    if (c->dhcp)
        esp_netif_dhcpc_start(ghost_sta);
    else {
        esp_netif_ip_info_t ip = {.ip.addr = inet_addr(c->ip),
                                  .netmask.addr = inet_addr(c->mask),
                                  .gw.addr = inet_addr(c->gateway)};
        esp_netif_set_ip_info(ghost_sta, &ip);
        apply_dns(c);
    }
    wifi_config_t wifi = {0};
    memcpy(wifi.sta.ssid, c->sta_ssid, strlen(c->sta_ssid));
    strlcpy((char *)wifi.sta.password, c->sta_password, sizeof(wifi.sta.password));
    wifi.sta.pmf_cfg.capable = true;
    esp_err_t r = esp_wifi_set_config(WIFI_IF_STA, &wifi);
    if (r == ESP_OK) {
        connect_started = ghost_millis();
        r = esp_wifi_connect();
    }
    return r;
}
esp_err_t ghost_network_start(const ghost_config_t *c) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ghost_sta = esp_netif_create_default_wifi_sta();
    ghost_ap = esp_netif_create_default_wifi_ap();
    if (!ghost_ap || !ghost_sta)
        return ESP_ERR_NO_MEM;
    esp_netif_dhcps_stop(ghost_ap);
    esp_netif_ip_info_t ip = {.ip.addr = inet_addr(ghost_setup ? "192.168.4.1" : c->ap_ip),
                              .netmask.addr =
                                  inet_addr(ghost_setup ? "255.255.255.0" : c->ap_mask)};
    ip.gw = ip.ip;
    ESP_ERROR_CHECK(esp_netif_set_ip_info(ghost_ap, &ip));
    dhcps_lease_t lease = {.enable = true};
    uint32_t network = ntohl(ip.ip.addr) & ntohl(ip.netmask.addr);
    lease.start_ip.addr = htonl(network + 10);
    lease.end_ip.addr = htonl(network + 50);
    ESP_ERROR_CHECK(esp_netif_dhcps_option(ghost_ap, ESP_NETIF_OP_SET,
                                           ESP_NETIF_REQUESTED_IP_ADDRESS, &lease, sizeof(lease)));
    esp_netif_dns_info_t dns = {.ip.type = ESP_IPADDR_TYPE_V4};
    dns.ip.u_addr.ip4 = ip.ip;
    ESP_ERROR_CHECK(esp_netif_set_dns_info(ghost_ap, ESP_NETIF_DNS_MAIN, &dns));
    uint8_t offer = 1;
    ESP_ERROR_CHECK(esp_netif_dhcps_option(ghost_ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
                                           &offer, sizeof(offer)));
    uint32_t lease_minutes = 120;
    esp_netif_dhcps_option(ghost_ap, ESP_NETIF_OP_SET, ESP_NETIF_IP_ADDRESS_LEASE_TIME,
                           &lease_minutes, sizeof(lease_minutes));
    if (!ghost_setup && !c->dhcp) {
        esp_netif_dhcpc_stop(ghost_sta);
        esp_netif_ip_info_t sta = {.ip.addr = inet_addr(c->ip),
                                   .netmask.addr = inet_addr(c->mask),
                                   .gw.addr = inet_addr(c->gateway)};
        ESP_ERROR_CHECK(esp_netif_set_ip_info(ghost_sta, &sta));
        apply_dns(c);
    }
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    wifi_config_t ap = {0}, sta = {0};
    const char *ap_name = ghost_setup ? "SolarProxxie-Setup" : c->ap_ssid;
    ap.ap.ssid_len = strlen(ap_name);
    memcpy(ap.ap.ssid, ap_name, ap.ap.ssid_len);
    ap.ap.channel = 1;
    ap.ap.max_connection = GHOST_DONGLES_MAX;
    ap.ap.authmode = ghost_setup ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    if (!ghost_setup)
        strlcpy((char *)ap.ap.password, c->ap_password, sizeof(ap.ap.password));
    ap.ap.pmf_cfg.capable = true;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    if (!ghost_setup) {
        memcpy(sta.sta.ssid, c->sta_ssid, strlen(c->sta_ssid));
        strlcpy((char *)sta.sta.password, c->sta_password, sizeof(sta.sta.password));
        sta.sta.pmf_cfg.capable = true;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    }
    /* NAPT stays available in normal mode; the router enforces each dongle's policy. */
    atomic_store(&cloud_requested, c->cloud && !ghost_setup);
    atomic_store(&ghost_cloud_enabled, c->cloud && !ghost_setup);
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    esp_netif_set_default_netif(ghost_sta);
    if (!ghost_setup) {
        esp_sntp_config_t time_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        esp_err_t time_result = esp_netif_sntp_init(&time_config);
        if (time_result != ESP_OK)
            ESP_LOGW("network", "SNTP initialization failed: %s", esp_err_to_name(time_result));
    }
    xTaskCreate(reconnect_task, "wifi_recovery", 2048, NULL, 4, &ghost_wifi_task);
    ghost_dns_start();
    ESP_LOGI("network", "AP: %s, IP: " IPSTR, ap_name, IP2STR(&ip.ip));
    return ESP_OK;
}
void ghost_network_restart(void) {
    esp_wifi_disconnect();
    atomic_store(&reconnect_requested, true);
}
cJSON *ghost_network_scan(void) {
    if (!ghost_setup)
        return NULL;
    wifi_scan_config_t scan = {.show_hidden = false};
    if (esp_wifi_scan_start(&scan, true) != ESP_OK)
        return NULL;
    wifi_ap_record_t records[20];
    uint16_t n = 20;
    if (esp_wifi_scan_get_ap_records(&n, records) != ESP_OK)
        return NULL;
    cJSON *a = cJSON_CreateArray();
    for (unsigned i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "ssid", (char *)records[i].ssid);
        cJSON_AddNumberToObject(o, "rssi", records[i].rssi);
        cJSON_AddNumberToObject(o, "security", records[i].authmode);
        cJSON_AddItemToArray(a, o);
    }
    return a;
}
cJSON *ghost_network_status(void) {
    cJSON *j = cJSON_CreateObject();
    esp_netif_ip_info_t sta = {0}, ap = {0};
    esp_netif_get_ip_info(ghost_sta, &sta);
    esp_netif_get_ip_info(ghost_ap, &ap);
    char s[16];
    esp_ip4addr_ntoa(&sta.ip, s, sizeof(s));
    cJSON_AddStringToObject(j, "sta_ip", s);
    esp_ip4addr_ntoa(&ap.ip, s, sizeof(s));
    cJSON_AddStringToObject(j, "ap_ip", s);
    cJSON_AddBoolToObject(j, "sta_connected", atomic_load(&ghost_sta_up));
    cJSON_AddBoolToObject(j, "nat_active", atomic_load(&ghost_nat_active));
    cJSON_AddBoolToObject(j, "cloud_forwarding", atomic_load(&ghost_cloud_enabled));
    cJSON_AddBoolToObject(j, "offline_mode", atomic_load(&ghost_offline_active));
    cJSON_AddStringToObject(j, "error", atomic_load(&overlap) ? overlap_error : "");
    wifi_ap_record_t info = {0};
    if (esp_wifi_sta_get_ap_info(&info) == ESP_OK) {
        cJSON_AddStringToObject(j, "ssid", (char *)info.ssid);
        cJSON_AddNumberToObject(j, "rssi", info.rssi);
    }
    wifi_config_t ap_config = {0};
    if (esp_wifi_get_config(WIFI_IF_AP, &ap_config) == ESP_OK) {
        char ssid[33] = {0};
        memcpy(ssid, ap_config.ap.ssid, 32);
        cJSON_AddStringToObject(j, "ap_ssid", ssid);
    }
    ghost_ap_clients_t clients;
    ghost_network_clients(&clients);
    cJSON_AddStringToObject(j, "clients_error",
                            clients.error == ESP_OK ? ""
                                                    : "Device lookup failed; refresh to retry");
    cJSON *a = cJSON_AddArrayToObject(j, "clients");
    for (unsigned i = 0; i < clients.count; i++) {
        esp_netif_pair_mac_ip_t pair = clients.clients[i].address;
        cJSON *o = cJSON_CreateObject();
        char mac[18];
        snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x", pair.mac[0], pair.mac[1],
                 pair.mac[2], pair.mac[3], pair.mac[4], pair.mac[5]);
        cJSON_AddStringToObject(o, "mac", mac);
        esp_ip4addr_ntoa(&pair.ip, s, sizeof(s));
        cJSON_AddStringToObject(o, "ip", s);
        cJSON_AddStringToObject(o, "ip_source",
                                !pair.ip.addr                 ? "unknown"
                                : clients.clients[i].from_arp ? "arp"
                                                              : "dhcp");
        cJSON_AddItemToArray(a, o);
    }
    cJSON_AddNumberToObject(j, "dhcp_lease_minutes", 120);
    return j;
}
