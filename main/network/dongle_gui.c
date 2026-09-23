#include "app.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include "lwip/lwip_napt.h"
#include "lwip/sockets.h"
#include "lwip/tcpip.h"
#include "network/clients.h"
#include <string.h>

/* All mapping state and lwIP table access belong to tcpip_thread. */
static uint32_t target, external;
atomic_uint ghost_gui_target;
static uint64_t expires;
static uint8_t target_mac[6];
typedef struct {
    int operation; /* 0: inspect/expire, 1: open, 2: close */
    uint32_t requested, sta;
    uint32_t clients[ESP_WIFI_MAX_CONN_NUM];
    uint8_t macs[ESP_WIFI_MAX_CONN_NUM][6];
    unsigned count;
    bool active, success;
    uint32_t current;
    unsigned seconds;
} gui_request_t;

static int client_index(const gui_request_t *r, uint32_t ip) {
    for (unsigned i = 0; i < r->count; i++)
        if (ip && r->clients[i] == ip)
            return (int)i;
    return -1;
}

static void close_mapping(void) {
    uint32_t mapped, destination;
    uint16_t port;
    /* Disabling NAPT may already have freed the entire table. */
    if (target && ip_portmap_get(IPPROTO_TCP, 8080, &mapped, &destination, &port))
        ip_portmap_remove(IPPROTO_TCP, 8080);
    target = external = 0;
    atomic_store(&ghost_gui_target, 0);
    expires = 0;
}

static void mapping(void *context) {
    gui_request_t *r = context;
    uint64_t now = ghost_millis();
    bool ready = !ghost_setup && atomic_load(&ghost_sta_up) && atomic_load(&ghost_nat_active) &&
                 atomic_load(&ghost_cloud_enabled);
    uint32_t mapped = 0, destination = 0;
    uint16_t port = 0;
    bool exists = target && ip_portmap_get(IPPROTO_TCP, 8080, &mapped, &destination, &port);
    int current = client_index(r, target);
    if (target && (!ready || now >= expires || external != r->sta || current < 0 ||
                   (current >= 0 && memcmp(target_mac, r->macs[current], 6)) || !exists ||
                   mapped != external || destination != target || port != 80))
        close_mapping();
    if (r->operation == 2) {
        close_mapping();
        r->success = true;
    } else if (r->operation == 1 && ready && client_index(r, r->requested) >= 0) {
        close_mapping();
        if (ip_portmap_add(IPPROTO_TCP, r->sta, 8080, r->requested, 80)) {
            target = r->requested;
            atomic_store(&ghost_gui_target, target);
            external = r->sta;
            memcpy(target_mac, r->macs[client_index(r, target)], 6);
            expires = now + 15 * 60 * 1000;
            r->success = true;
        }
    }
    r->active = target != 0;
    r->current = target;
    r->seconds = target ? (unsigned)((expires - now + 999) / 1000) : 0;
}

static bool execute(gui_request_t *r) {
    esp_netif_ip_info_t sta = {0};
    esp_netif_get_ip_info(ghost_sta, &sta);
    r->sta = sta.ip.addr;
    ghost_ap_clients_t clients;
    ghost_network_clients(&clients);
    if (clients.error == ESP_OK) {
        for (unsigned i = 0; i < clients.count && r->count < ESP_WIFI_MAX_CONN_NUM; i++) {
            esp_netif_pair_mac_ip_t pair = clients.clients[i].address;
            if (pair.ip.addr) {
                memcpy(r->macs[r->count], pair.mac, 6);
                r->clients[r->count++] = pair.ip.addr;
            }
        }
    }
    return tcpip_callback_wait(mapping, r) == ERR_OK;
}

bool ghost_dongle_gui_set(const char *ip) {
    gui_request_t r = {.operation = ip ? 1 : 2};
    if (ip) {
        ip4_addr_t address;
        if (!ip4addr_aton(ip, &address) || !address.addr)
            return false;
        r.requested = address.addr;
    }
    return execute(&r) && r.success;
}

void ghost_dongle_gui_poll(void) {
    gui_request_t r = {0};
    execute(&r);
}

cJSON *ghost_dongle_gui_status(void) {
    gui_request_t r = {0};
    execute(&r);
    cJSON *j = cJSON_CreateObject();
    cJSON_AddBoolToObject(j, "active", r.active);
    cJSON_AddNumberToObject(j, "remaining_seconds", r.seconds);
    cJSON_AddNumberToObject(j, "port", 8080);
    esp_ip4_addr_t ip = {.addr = r.current};
    char text[16];
    esp_ip4addr_ntoa(&ip, text, sizeof(text));
    cJSON_AddStringToObject(j, "ip", text);
    return j;
}
