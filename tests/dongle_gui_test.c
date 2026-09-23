/* SDK substitutes are supplied by test_dongle_gui.py. Test the actual C module. */
#include "gui_sdk.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
bool ghost_setup;
atomic_bool ghost_sta_up = true, ghost_nat_active = true, ghost_cloud_enabled = true;
esp_netif_t *ghost_ap, *ghost_sta;
static uint64_t clock_ms;
static bool have_mapping, fail_add, callback_fails;
static uint32_t map_external, map_target;
static unsigned removes;
static wifi_sta_list_t associated;
static uint32_t leased_ip;
static bool wifi_error;
static struct netif ap_netif = {.ip = {0x01000001}, .mask = {0xff000000}}, other_netif;
static struct {
    bool valid;
    ip4_addr_t ip;
    struct netif *netif;
    struct eth_addr mac;
} arp[ARP_TABLE_SIZE];
void *esp_netif_get_netif_impl(esp_netif_t *n) {
    return &ap_netif;
}
int etharp_get_entry(size_t i, ip4_addr_t **ip, struct netif **n, struct eth_addr **mac) {
    if (!arp[i].valid)
        return 0;
    *ip = &arp[i].ip;
    *n = arp[i].netif;
    *mac = &arp[i].mac;
    return 1;
}
uint64_t ghost_millis(void) {
    return clock_ms;
}
int ip_portmap_get(int proto, int port, uint32_t *ext, uint32_t *dst, uint16_t *dport) {
    assert(proto == IPPROTO_TCP && port == 8080);
    if (!have_mapping)
        return 0;
    *ext = map_external;
    *dst = map_target;
    *dport = 80;
    return 1;
}
int ip_portmap_remove(int proto, int port) {
    assert(proto == IPPROTO_TCP && port == 8080 && have_mapping);
    have_mapping = false;
    removes++;
    return 1;
}
int ip_portmap_add(int proto, uint32_t ext, int port, uint32_t dst, int dport) {
    assert(proto == IPPROTO_TCP && port == 8080 && dport == 80 && !have_mapping);
    if (fail_add)
        return 0;
    have_mapping = true;
    map_external = ext;
    map_target = dst;
    return 1;
}
int tcpip_callback_wait(void (*fn)(void *), void *r) {
    if (callback_fails)
        return -1;
    fn(r);
    return 0;
}
int esp_netif_get_ip_info(esp_netif_t *n, esp_netif_ip_info_t *ip) {
    ip->ip.addr = 123;
    return 0;
}
int esp_wifi_ap_get_sta_list(wifi_sta_list_t *c) {
    *c = associated;
    return wifi_error ? ESP_FAIL : ESP_OK;
}
int esp_netif_dhcps_get_clients_by_mac(esp_netif_t *n, int count, esp_netif_pair_mac_ip_t *p) {
    p->ip.addr = leased_ip;
    return ESP_OK;
}
int ip4addr_aton(const char *s, ip4_addr_t *ip) {
    return 0;
}
void esp_ip4addr_ntoa(const esp_ip4_addr_t *ip, char *s, unsigned n) {
    s[0] = 0;
}
cJSON *cJSON_CreateObject(void) {
    return NULL;
}
void cJSON_AddBoolToObject(cJSON *j, const char *k, int v) {}
void cJSON_AddNumberToObject(cJSON *j, const char *k, double v) {}
void cJSON_AddStringToObject(cJSON *j, const char *k, const char *v) {}
#include "../main/network/clients.c"
#include "../main/network/dongle_gui.c"
static void discovery_tests(void) {
    ghost_ap_clients_t c;
    ghost_network_clients(&c);
    assert(c.count == 0 && c.error == ESP_OK);
    associated.num = 1;
    associated.sta[0].mac[0] = 7;
    ghost_network_clients(&c);
    assert(c.count == 1 && c.clients[0].address.ip.addr == 0);
    arp[0].valid = true;
    arp[0].ip.addr = 0x01000010;
    arp[0].netif = &ap_netif;
    arp[0].mac.addr[0] = 7;
    ghost_network_clients(&c);
    assert(c.clients[0].from_arp && c.clients[0].address.ip.addr == 0x01000010);
    /* Use discovery through the actual enable path, not just its snapshot. */
    gui_request_t resolved = {.operation = 1, .requested = 0x01000010};
    assert(execute(&resolved) && resolved.success && have_mapping);
    assert(map_target == 0x01000010);
    associated.num = 0;
    ghost_dongle_gui_poll();
    assert(!have_mapping); /* Stale ARP must not keep access alive. */
    associated.num = 1;
    leased_ip = 0x01000020;
    ghost_network_clients(&c);
    assert(!c.clients[0].from_arp && c.clients[0].address.ip.addr == leased_ip);
    leased_ip = 0;
    arp[0].netif = &other_netif;
    ghost_network_clients(&c);
    assert(c.clients[0].address.ip.addr == 0);
    arp[0].netif = &ap_netif;
    arp[0].mac.addr[0] = 8;
    ghost_network_clients(&c);
    assert(c.clients[0].address.ip.addr == 0);
    arp[0].mac.addr[0] = 7;
    arp[0].ip.addr = 0x02000010;
    ghost_network_clients(&c);
    assert(c.clients[0].address.ip.addr == 0);
    wifi_error = true;
    ghost_network_clients(&c);
    assert(c.error != ESP_OK && c.count == 0);
    wifi_error = false;
    associated.num = 0;
    arp[0].ip.addr = 0x01000010;
    ghost_network_clients(&c);
    assert(c.count == 0); /* Old ARP entries aren't connected clients. */
}
static gui_request_t request(int operation) {
    gui_request_t r = {
        .operation = operation, .sta = 123, .requested = 456, .clients = {456, 789}, .count = 2};
    r.macs[0][0] = 1;
    r.macs[1][0] = 2;
    return r;
}
static void open_gui(void) {
    gui_request_t r = request(1);
    mapping(&r);
    assert(r.active && r.success && r.seconds == 900 && map_target == 456);
}
int main(void) {
    discovery_tests();
    gui_request_t r;
    open_gui();
    r = request(1);
    r.requested = 999;
    mapping(&r);
    assert(!r.success && map_target == 456); /* Reject arbitrary LAN/cloud targets. */
    r = request(1);
    r.requested = 789;
    mapping(&r);
    assert(r.success && map_target == 789);
    r = request(2);
    mapping(&r);
    assert(r.success && !r.active && !have_mapping);
    open_gui();
    clock_ms += 899999;
    r = request(0);
    mapping(&r);
    assert(r.active && r.seconds == 1);
    clock_ms++;
    r = request(0);
    mapping(&r);
    assert(!r.active && !have_mapping);
    open_gui();
    r = request(0);
    r.count = 0;
    mapping(&r);
    assert(!r.active);
    open_gui();
    r = request(0);
    r.macs[0][0] = 3;
    mapping(&r);
    assert(!r.active);
    open_gui();
    r = request(0);
    r.sta = 321;
    mapping(&r);
    assert(!r.active);
    open_gui();
    ghost_cloud_enabled = false;
    r = request(0);
    mapping(&r);
    assert(!r.active);
    r = request(1);
    mapping(&r);
    assert(!r.success);
    ghost_cloud_enabled = true;
    open_gui();
    ghost_sta_up = false;
    r = request(0);
    mapping(&r);
    assert(!r.active);
    ghost_sta_up = true;
    open_gui();
    ghost_setup = true;
    r = request(0);
    mapping(&r);
    assert(!r.active);
    r = request(1);
    mapping(&r);
    assert(!r.success);
    ghost_setup = false;
    open_gui();
    have_mapping = false;
    unsigned before = removes;
    r = request(0);
    mapping(&r);
    assert(!r.active && removes == before); /* Table was freed. */
    fail_add = true;
    r = request(1);
    mapping(&r);
    assert(!r.active && !r.success);
    fail_add = false;
    callback_fails = true;
    assert(!ghost_dongle_gui_set(NULL));
    puts("PASS dongle GUI: DHCP/ARP discovery, unknown IPs, interface/MAC/subnet restrictions, "
         "target validation, replacement, close, expiry, MAC/IP changes, "
         "network/setup gating, freed table, allocation/callback failure");
    return 0;
}
