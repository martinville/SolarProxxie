#pragma once
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ESP_WIFI_MAX_CONN_NUM 10
#define ESP_OK 0
#define ESP_FAIL -1
#define ERR_OK 0
#define IPPROTO_TCP 6
typedef void esp_netif_t;
typedef int esp_err_t;
typedef struct {
    uint32_t addr;
} esp_ip4_addr_t;
typedef esp_ip4_addr_t ip4_addr_t;
#define ARP_TABLE_SIZE 4
struct netif {
    ip4_addr_t ip, mask;
};
struct eth_addr {
    uint8_t addr[6];
};
#define netif_ip4_addr(n) (&(n)->ip)
#define netif_ip4_netmask(n) (&(n)->mask)
#define ip4_addr_netcmp(a, b, m) (((a)->addr & (m)->addr) == ((b)->addr & (m)->addr))
void *esp_netif_get_netif_impl(esp_netif_t *n);
int etharp_get_entry(size_t i, ip4_addr_t **ip, struct netif **n, struct eth_addr **mac);
typedef struct {
    esp_ip4_addr_t ip;
} esp_netif_ip_info_t;
typedef struct {
    uint8_t mac[6];
    esp_ip4_addr_t ip;
} esp_netif_pair_mac_ip_t;
typedef struct {
    int num;
    struct {
        uint8_t mac[6];
    } sta[10];
} wifi_sta_list_t;
typedef void cJSON;
extern bool ghost_setup;
extern atomic_bool ghost_sta_up, ghost_nat_active, ghost_cloud_enabled;
extern esp_netif_t *ghost_ap, *ghost_sta;
uint64_t ghost_millis(void);
int ip_portmap_get(int, int, uint32_t *, uint32_t *, uint16_t *);
int ip_portmap_remove(int, int);
int ip_portmap_add(int, uint32_t, int, uint32_t, int);
int tcpip_callback_wait(void (*)(void *), void *);
int esp_netif_get_ip_info(esp_netif_t *, esp_netif_ip_info_t *);
int esp_wifi_ap_get_sta_list(wifi_sta_list_t *);
int esp_netif_dhcps_get_clients_by_mac(esp_netif_t *, int, esp_netif_pair_mac_ip_t *);
int ip4addr_aton(const char *, ip4_addr_t *);
void esp_ip4addr_ntoa(const esp_ip4_addr_t *, char *, unsigned);
cJSON *cJSON_CreateObject(void);
void cJSON_AddBoolToObject(cJSON *, const char *, int);
void cJSON_AddNumberToObject(cJSON *, const char *, double);
void cJSON_AddStringToObject(cJSON *, const char *, const char *);
