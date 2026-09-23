#pragma once
#include "esp_netif.h"
#include "esp_wifi.h"
#include <stdbool.h>
typedef struct {
    esp_netif_pair_mac_ip_t address;
    bool from_arp;
} ghost_ap_client_t;
typedef struct {
    unsigned count;
    esp_err_t error;
    ghost_ap_client_t clients[ESP_WIFI_MAX_CONN_NUM];
} ghost_ap_clients_t;
void ghost_network_clients(ghost_ap_clients_t *out);
