#include "network/clients.h"
#include "app.h"
#include "esp_netif_net_stack.h"
#include "lwip/etharp.h"
#include "lwip/tcpip.h"
#include <string.h>

static void resolve_arp(void *context) {
    ghost_ap_clients_t *out = context;
    struct netif *ap = esp_netif_get_netif_impl(ghost_ap);
    if (!ap)
        return;
    for (unsigned c = 0; c < out->count; c++) {
        ghost_ap_client_t *client = &out->clients[c];
        if (client->address.ip.addr)
            continue;
        for (size_t i = 0; i < ARP_TABLE_SIZE; i++) {
            ip4_addr_t *ip;
            struct netif *netif;
            struct eth_addr *mac;
            if (etharp_get_entry(i, &ip, &netif, &mac) && netif == ap && ip->addr &&
                ip->addr != netif_ip4_addr(ap)->addr &&
                ip4_addr_netcmp(ip, netif_ip4_addr(ap), netif_ip4_netmask(ap)) &&
                !memcmp(mac->addr, client->address.mac, 6)) {
                client->address.ip.addr = ip->addr;
                client->from_arp = true;
                break;
            }
        }
    }
}

void ghost_network_clients(ghost_ap_clients_t *out) {
    memset(out, 0, sizeof(*out));
    wifi_sta_list_t wifi = {0};
    out->error = esp_wifi_ap_get_sta_list(&wifi);
    if (out->error != ESP_OK)
        return;
    for (int i = 0; i < wifi.num && out->count < ESP_WIFI_MAX_CONN_NUM; i++) {
        ghost_ap_client_t *client = &out->clients[out->count++];
        memcpy(client->address.mac, wifi.sta[i].mac, 6);
        if (esp_netif_dhcps_get_clients_by_mac(ghost_ap, 1, &client->address) != ESP_OK)
            client->address.ip.addr = 0;
    }
    /* A connected device may retain its IP across an ESP32 restart, or use a
     * static IP. Resolve it only through a matching MAC on this AP's ARP table. */
    if (out->count && tcpip_callback_wait(resolve_arp, out) != ERR_OK)
        out->error = ESP_FAIL;
}
