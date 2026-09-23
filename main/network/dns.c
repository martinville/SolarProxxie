#include "app.h"
#include "esp_random.h"
#include "lwip/sockets.h"
#include <string.h>
static atomic_uint dns_ok, dns_fail;
static atomic_bool dns_ready;
TaskHandle_t ghost_dns_task;
static bool question(const uint8_t *p, size_t n, size_t *end) {
    if (n < 17 || p[2] & 0x80 || p[4] != 0 || p[5] != 1 || p[6] || p[7] || p[8] || p[9])
        return false;
    size_t i = 12;
    unsigned labels = 0;
    while (i < n && p[i]) {
        size_t len = p[i++];
        if (len > 63 || len > n - i || ++labels > 64)
            return false;
        i += len;
    }
    if (i + 5 > n)
        return false;
    *end = i + 5;
    return true;
}
static void dns_task(void *arg) {
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        vTaskDelete(NULL);
        return;
    }
    esp_netif_ip_info_t ap;
    esp_netif_get_ip_info(ghost_ap, &ap);
    struct sockaddr_in bindaddr = {
        .sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = ap.ip.addr};
    if (bind(fd, (struct sockaddr *)&bindaddr, sizeof(bindaddr)) < 0) {
        close(fd);
        vTaskDelete(NULL);
        return;
    }
    atomic_store(&dns_ready, true);
    uint8_t query[512], reply[512];
    for (;;) {
        /* Includes malformed requests: setup DNS floods cannot starve idle. */
        vTaskDelay(1);
        struct sockaddr_in peer;
        socklen_t plen = sizeof(peer);
        int n = recvfrom(fd, query, sizeof(query), 0, (struct sockaddr *)&peer, &plen);
        size_t end;
        if (n < 0 || !question(query, n, &end))
            continue;
        int rn = 0;
        if (ghost_setup) {
            memcpy(reply, query, end);
            reply[2] = 0x81;
            reply[3] = 0x80;
            reply[10] = reply[11] = 0;
            if (query[end - 4] == 0 && query[end - 3] == 1 && query[end - 2] == 0 &&
                query[end - 1] == 1 && end + 16 <= sizeof(reply)) {
                reply[6] = 0;
                reply[7] = 1;
                uint8_t answer[] = {0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0};
                memcpy(answer + 12, &ap.ip.addr, 4);
                memcpy(reply + end, answer, sizeof(answer));
                rn = end + 16;
            } else {
                reply[6] = reply[7] = 0;
                rn = end;
            }
        } else if (atomic_load(&ghost_sta_up) && atomic_load(&ghost_cloud_enabled)) {
            for (int which = 0; which < 2 && !rn; which++) {
                esp_netif_dns_info_t dns = {0};
                if (esp_netif_get_dns_info(ghost_sta,
                                           which ? ESP_NETIF_DNS_BACKUP : ESP_NETIF_DNS_MAIN,
                                           &dns) != ESP_OK ||
                    !dns.ip.u_addr.ip4.addr || dns.ip.u_addr.ip4.addr == ap.ip.addr)
                    continue;
                int upstream = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                if (upstream < 0)
                    break;
                struct timeval timeout = {.tv_sec = 1};
                setsockopt(upstream, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
                struct sockaddr_in dst = {.sin_family = AF_INET,
                                          .sin_port = htons(53),
                                          .sin_addr.s_addr = dns.ip.u_addr.ip4.addr};
                uint8_t id[2] = {query[0], query[1]};
                uint16_t random = esp_random();
                memcpy(query, &random, 2);
                if (connect(upstream, (struct sockaddr *)&dst, sizeof(dst)) == 0 &&
                    send(upstream, query, n, 0) == n) {
                    int got = recv(upstream, reply, sizeof(reply), 0);
                    if (got >= 12 && !memcmp(reply, query, 2) && (reply[2] & 0x80)) {
                        rn = got;
                        reply[0] = id[0];
                        reply[1] = id[1];
                    }
                }
                query[0] = id[0];
                query[1] = id[1];
                close(upstream);
            }
        }
        if (!rn) {
            memcpy(reply, query, end);
            reply[2] = 0x81;
            reply[3] = 0x82;
            memset(reply + 6, 0, 6);
            rn = end;
            atomic_fetch_add(&dns_fail, 1);
        } else
            atomic_fetch_add(&dns_ok, 1);
        sendto(fd, reply, rn, 0, (struct sockaddr *)&peer, plen);
    }
}
void ghost_dns_start(void) {
    xTaskCreate(dns_task, "dns_proxy", 3072, NULL, 3, &ghost_dns_task);
}
cJSON *ghost_dns_status(void) {
    cJSON *j = cJSON_CreateObject();
    cJSON_AddBoolToObject(j, "ready", atomic_load(&dns_ready));
    cJSON_AddNumberToObject(j, "responses", atomic_load(&dns_ok));
    cJSON_AddNumberToObject(j, "failures", atomic_load(&dns_fail));
    return j;
}
