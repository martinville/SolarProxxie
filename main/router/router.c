#include "app.h"
#include "esp_netif_net_stack.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "lwip/inet.h"
#include "lwip/ip.h"
#include "lwip/ip4.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/sockets.h"
#include <errno.h>
#include "protocol/pcap.h"
#include "protocol/dongle_identity.h"
#include "protocol/cloud_emulator.h"
#include <stdlib.h>
#include <string.h>
#define SNAP 768
#define QUEUE_DEPTH 8
#define STREAM_QUEUE_DEPTH 12
typedef struct {
    uint64_t ms;
    uint32_t id;
    uint16_t len, wire;
    bool downstream;
    uint8_t data[SNAP];
} packet_t;
static QueueHandle_t queue;
static SemaphoreHandle_t data_lock;
static ghost_store_t telemetry;
static QueueHandle_t stream_queue;
static bool stream_active;
static atomic_bool stream_stop;
static atomic_uint stream_drops;
static atomic_uint stream_packets;
static uint64_t stream_started_ms;
static atomic_uint record_length = 292;
static struct {
    uint32_t ip;
    uint16_t layout;
    uint8_t profile;
} source_layouts[GHOST_DONGLES_MAX];
static struct {
    uint32_t ip;
    uint64_t observed_ms;
    ghost_dongle_identity_t data;
} identities[GHOST_DONGLES_MAX];
static atomic_uint received, inspected, decoded, rejected, copy_drops, forward_attempts, blocked;
static atomic_uint received_downstream;
static atomic_uint dns_spoofed, dns_spoof_errors;
static atomic_uint route_ips[GHOST_DONGLES_MAX];
static atomic_bool route_cloud[GHOST_DONGLES_MAX];
static struct netif *ap_netif;
/* LWIP_HOOK_IP4_INPUT runs on lwIP's small tcpip stack. These buffers are safe
 * as shared storage because that hook is serialized on the tcpip thread. */
static uint8_t dns_input[512], dns_output[8 + 512];
TaskHandle_t ghost_decoder_task;
typedef struct {
    uint32_t src, dst;
    uint16_t sport, dport;
    uint64_t seen;
    unsigned packets, replies;
    ghost_stream_t stream;
    ghost_identity_stream_t identity_stream;
    bool identity_found;
} flow_t;
static flow_t flows[GHOST_DONGLES_MAX];
static atomic_ullong decoder_heartbeat;
static void copy_packet(struct pbuf *p, bool downstream) {
    if (!queue)
        return;
    if (uxQueueSpacesAvailable(queue) == 0) {
        atomic_fetch_add(&copy_drops, 1);
        return;
    }
    packet_t copy = {.ms = ghost_millis(), .wire = p->tot_len, .downstream = downstream};
    copy.len = p->tot_len > SNAP ? SNAP : p->tot_len;
    pbuf_copy_partial(p, copy.data, copy.len, 0);
    if (xQueueSend(queue, &copy, 0) != pdTRUE)
        atomic_fetch_add(&copy_drops, 1);
}
bool ghost_router_local_for_ip(uint32_t ip) {
    if (!ip || ghost_setup)
        return false;
    for (unsigned i = 0; i < GHOST_DONGLES_MAX; i++)
        if (ip == atomic_load(&route_ips[i]))
            return !atomic_load(&ghost_cloud_enabled) || !atomic_load(&route_cloud[i]);
    return false;
}
bool ghost_router_local_enabled(void) {
    if (ghost_setup)
        return false;
    for (unsigned i = 0; i < GHOST_DONGLES_MAX; i++) {
        uint32_t ip = atomic_load(&route_ips[i]);
        if (ip && (!atomic_load(&ghost_cloud_enabled) || !atomic_load(&route_cloud[i])))
            return true;
    }
    return false;
}

static bool spoof_cloud_dns(struct pbuf *packet) {
    size_t copied = pbuf_copy_partial(packet, dns_input,
                                      packet->tot_len < sizeof(dns_input) ? packet->tot_len
                                                                          : sizeof(dns_input),
                                      0);
    if (copied < 20 || dns_input[0] >> 4 != 4 || dns_input[9] != 17)
        return false;
    size_t ihl = (dns_input[0] & 15) * 4;
    uint16_t total = ghost_be16(dns_input + 2), fragment = ghost_be16(dns_input + 6);
    if (ihl < 20 || total > copied || total < ihl + 8 || (fragment & 0x3fff))
        return false;
    uint8_t *udp = dns_input + ihl;
    uint16_t source_port = ghost_be16(udp), destination_port = ghost_be16(udp + 2);
    uint16_t udp_length = ghost_be16(udp + 4);
    uint32_t client_ip, resolver_ip;
    memcpy(&client_ip, dns_input + 12, 4);
    memcpy(&resolver_ip, dns_input + 16, 4);
    if (destination_port != 53 || udp_length < 8 || ihl + udp_length > total ||
        !ghost_router_local_for_ip(client_ip))
        return false;
    const uint8_t *query = udp + 8;
    size_t query_length = udp_length - 8, question_end;
    ghost_cloud_role_t role = GHOST_CLOUD_ROLE_UNKNOWN;
    if (query_length < 17 || query[2] & 0x80 || query[4] != 0 || query[5] != 1 ||
        !ghost_cloud_dns_name(query, query_length, &question_end, &role))
        return false;
    if (question_end > sizeof(dns_input) - 16) {
        atomic_fetch_add(&dns_spoof_errors, 1);
        return true;
    }
    size_t dns_length = question_end + 16;
    struct pbuf *reply = pbuf_alloc(PBUF_IP, 8 + dns_length, PBUF_RAM);
    if (!reply) {
        atomic_fetch_add(&dns_spoof_errors, 1);
        return true;
    }
    dns_output[0] = 0;
    dns_output[1] = 53;
    dns_output[2] = (uint8_t)(source_port >> 8);
    dns_output[3] = (uint8_t)source_port;
    dns_output[4] = (uint8_t)((8 + dns_length) >> 8);
    dns_output[5] = (uint8_t)(8 + dns_length);
    dns_output[6] = dns_output[7] = 0; /* A zero UDP checksum is valid for IPv4. */
    memcpy(dns_output + 8, query, question_end);
    uint8_t *dns = dns_output + 8;
    dns[2] = 0x81;
    dns[3] = 0x80;
    dns[4] = 0;
    dns[5] = 1;
    dns[6] = 0;
    dns[7] = 1;
    memset(dns + 8, 0, 4);
    uint8_t answer[] = {0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, 5, 0, 4, 0, 0, 0, 0};
    esp_netif_ip_info_t ap = {0};
    esp_netif_get_ip_info(ghost_ap, &ap);
    memcpy(answer + 12, &ap.ip.addr, 4);
    memcpy(dns + question_end, answer, sizeof(answer));
    ghost_offline_note_dns(client_ip, role);
    pbuf_take(reply, dns_output, 8 + dns_length);
    ip4_addr_t source = {.addr = resolver_ip}, target = {.addr = client_ip};
    err_t result = ip4_output_if_src(reply, &source, &target, 64, 0, IP_PROTO_UDP, ap_netif);
    pbuf_free(reply);
    atomic_fetch_add(result == ERR_OK ? &dns_spoofed : &dns_spoof_errors, 1);
    return true;
}
int ghost_ip4_input(struct pbuf *p, struct netif *inp) {
    /* Called on tcpip_thread before full IP validation. Never dereference wire structs. */
    if (inp == ap_netif) {
        atomic_fetch_add(&received, 1);
        copy_packet(p, false);
        if (spoof_cloud_dns(p))
            return 1;
    }
    return 0;
}
static bool web_gui_packet(struct pbuf *p, bool downstream) {
    uint32_t target = atomic_load(&ghost_gui_target);
    if (!target || p->tot_len < 24)
        return false;
    uint8_t header[64];
    size_t copied = pbuf_copy_partial(p, header, p->tot_len < sizeof(header) ? p->tot_len : sizeof(header), 0);
    if (copied < 24 || header[0] >> 4 != 4 || header[9] != 6)
        return false;
    size_t ihl = (header[0] & 15) * 4;
    if (ihl < 20 || copied < ihl + 4)
        return false;
    uint32_t src = ip4_addr_get_u32(ip4_current_src_addr());
    uint32_t dst = ip4_addr_get_u32(ip4_current_dest_addr());
    uint16_t sport = ghost_be16(header + ihl), dport = ghost_be16(header + ihl + 2);
    /* Accept both sides of the lwIP port-map rewrite. Inbound traffic can reach this
       hook as STA:8080 or as dongle:80 depending on the NAPT stage; replies arrive
       from dongle:80 before their source is rewritten to STA:8080. */
    return downstream ? (dport == 8080 || (dst == target && dport == 80))
                      : (src == target && sport == 80);
}
int ghost_ip4_canforward(struct pbuf *p, unsigned int dest) {
    /* Applied after destination NAT as well, so both directions of existing flows stop. */
    (void)dest;
    bool downstream = ip_current_input_netif() != ap_netif;
    if (downstream) {
        atomic_fetch_add(&received_downstream, 1);
        copy_packet(p, true);
    }
    if (ghost_setup || !atomic_load(&ghost_cloud_enabled)) {
        atomic_fetch_add(&blocked, 1);
        return 0;
    }
    if (web_gui_packet(p, downstream)) {
        atomic_fetch_add(&forward_attempts, 1);
        return 1;
    }
    uint32_t device = downstream ? ip4_addr_get_u32(ip4_current_dest_addr())
                                 : ip4_addr_get_u32(ip4_current_src_addr());
    bool matched = false, allowed = false;
    for (unsigned i = 0; i < GHOST_DONGLES_MAX; i++)
        if (device && device == atomic_load(&route_ips[i])) {
            matched = true;
            allowed = atomic_load(&route_cloud[i]);
            break;
        }
    /* Unknown traffic remains pass-through. Blocking an IP that was not positively
       matched would take every dongle offline if interface/NAPT state is incomplete. */
    if (matched && !allowed) {
        atomic_fetch_add(&blocked, 1);
        return 0;
    }
    atomic_fetch_add(&forward_attempts, 1);
    return -1;
}
static void decoded_frame(const uint8_t *p, size_t n, void *context) {
    ghost_values_t v;
    if (!ghost_inteless_decode(p, n, &v)) {
        atomic_fetch_add(&rejected, 1);
        return;
    }
    xSemaphoreTake(data_lock, portMAX_DELAY);
    uint32_t ip = context ? ((flow_t *)context)->src : inet_addr("192.168.50.254");
    bool accepted = ghost_store_update(&telemetry, ip, &v, ghost_millis());
    xSemaphoreGive(data_lock);
    atomic_fetch_add(accepted ? &decoded : &rejected, 1);
}
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static void parse(packet_t *p) {
    atomic_fetch_add(&inspected, 1);
    if (p->len < 40 || p->data[0] >> 4 != 4)
        return;
    size_t ihl = (p->data[0] & 15) * 4, total = ghost_be16(p->data + 2);
    if (ihl < 20 || total > p->len || total < ihl + 20 || (ghost_be16(p->data + 6) & 0x3fff) ||
        p->data[9] != 6)
        return;
    const uint8_t *tcp = p->data + ihl;
    size_t thl = (tcp[12] >> 4) * 4;
    if (thl < 20 || ihl + thl > total)
        return;
    uint32_t src, dst;
    memcpy(&src, p->data + 12, 4);
    memcpy(&dst, p->data + 16, 4);
    uint16_t sport = ghost_be16(tcp), dport = ghost_be16(tcp + 2);
    if (p->downstream) {
        uint32_t swap = src;
        src = dst;
        dst = swap;
        uint16_t port = sport;
        sport = dport;
        dport = port;
    }
    size_t count = total - ihl - thl;
    const uint8_t *payload = tcp + thl;
    xSemaphoreTake(data_lock, portMAX_DELAY);
    flow_t *f = NULL, *old = &flows[0];
    for (unsigned i = 0; i < GHOST_DONGLES_MAX; i++) {
        if (flows[i].src == src && flows[i].dst == dst && flows[i].sport == sport &&
            flows[i].dport == dport)
            f = &flows[i];
        if (flows[i].seen < old->seen)
            old = &flows[i];
    }
    if (!f) {
        f = old;
        memset(f, 0, sizeof(*f));
        f->src = src;
        f->dst = dst;
        f->sport = sport;
        f->dport = dport;
    }
    unsigned layout = atomic_load(&record_length);
    for (unsigned i = 0; i < GHOST_DONGLES_MAX; i++)
        if (source_layouts[i].ip == src && src) {
            layout = source_layouts[i].layout;
            break;
        }
    if (p->ms - f->seen > 30000 || (tcp[13] & 6) || f->stream.frame_length != layout)
        memset(&f->stream, 0, sizeof(f->stream));
    if (p->ms - f->seen > 30000 || (tcp[13] & 6)) {
        memset(&f->identity_stream, 0, sizeof(f->identity_stream));
        f->identity_found = false;
    }
    f->stream.frame_length = layout;
    f->seen = p->ms;
    if (p->downstream)
        f->replies++;
    else
        f->packets++;
    if (count && !p->downstream && dport == 51100 && !f->identity_found) {
        ghost_dongle_identity_t found;
        uint32_t seq = be32(tcp + 4) + ((tcp[13] & 2) ? 1 : 0);
        if (ghost_identity_feed(&f->identity_stream, seq, payload, count, &found)) {
            f->identity_found = true;
            unsigned target = 0;
            for (unsigned i = 0; i < GHOST_DONGLES_MAX; i++) {
                if (identities[i].ip == src) {
                    target = i;
                    break;
                }
                if (!identities[i].ip ||
                    identities[i].observed_ms < identities[target].observed_ms)
                    target = i;
            }
            identities[target].ip = src;
            identities[target].observed_ms = p->ms;
            identities[target].data = found;
        }
    }
    xSemaphoreGive(data_lock);
    if (count && !p->downstream)
        ghost_stream_feed(&f->stream, be32(tcp + 4) + ((tcp[13] & 2) ? 1 : 0), payload, count,
                          decoded_frame, f);
}
static void decoder(void *arg) {
    packet_t p;
    unsigned batch = 0;
#ifdef CONFIG_GHOST_TEST_PACKET_REPLAY
    uint64_t replay_at = 0;
#endif
    for (;;) {
        atomic_store(&decoder_heartbeat, ghost_millis());
#ifdef CONFIG_GHOST_TEST_PACKET_REPLAY
        if (ghost_millis() - replay_at > 10000) {
            uint8_t sample[GHOST_FRAME_MAX] = {0};
            size_t n = atomic_load(&record_length);
            sample[0] = 0xa5;
            memcpy(sample + 11, "TEST000001", 10);
            sample[37] = 24;
            sample[38] = 2;
            sample[39] = 29;
            sample[40] = 12;
            unsigned d = n != 292 ? 8 : 0;
            sample[244 + d] = 0;
            sample[245 + d] = 54;
            sample[240 + d] = 4;
            sample[241 + d] = 186;
            decoded_frame(sample, n, NULL);
            replay_at = ghost_millis();
        }
#endif
        if (xQueueReceive(queue, &p, pdMS_TO_TICKS(1000)) != pdTRUE)
            continue;
        xSemaphoreTake(data_lock, portMAX_DELAY);
        if (stream_queue && xQueueSend(stream_queue, &p, 0) != pdTRUE)
            atomic_fetch_add(&stream_drops, 1);
        xSemaphoreGive(data_lock);
        parse(&p);
        /* A saturated decoder queue must leave CPU time for MQTT, HTTP and
         * the idle task/watchdog. One tick every eight copies bounds CPU use. */
        if (++batch == 8) {
            batch = 0;
            vTaskDelay(1);
        }
    }
}
void ghost_router_start(void) {
    data_lock = xSemaphoreCreateMutex();
    queue = xQueueCreate(QUEUE_DEPTH, sizeof(packet_t));
    ap_netif = esp_netif_get_netif_impl(ghost_ap);
    if (data_lock && queue)
        xTaskCreatePinnedToCore(decoder, "decoder", 4096, NULL, 4, &ghost_decoder_task, 1);
}
void ghost_router_config(const ghost_config_t *c) {
    if (data_lock)
        xSemaphoreTake(data_lock, portMAX_DELAY);
    unsigned previous_default = atomic_load(&record_length);
    ghost_decoder_profile_t previous_default_profile =
        ghost_decoder_profile_from_layout(previous_default);
    for (unsigned i = 0; i < GHOST_TELEMETRY_SLOTS; i++) {
        uint32_t ip = telemetry.sources[i].ip;
        if (!ip)
            continue;
        ghost_decoder_profile_t previous = previous_default_profile;
        ghost_decoder_profile_t next = ghost_decoder_profile_from_layout(c->layout);
        for (unsigned j = 0; j < GHOST_DONGLES_MAX; j++) {
            if (source_layouts[j].ip == ip)
                previous = (ghost_decoder_profile_t)source_layouts[j].profile;
            if (c->dongles[j].ip[0] && inet_addr(c->dongles[j].ip) == ip)
                next = (ghost_decoder_profile_t)c->dongle_profiles[j];
        }
        if (previous != next) {
            memset(&telemetry.sources[i], 0, sizeof(telemetry.sources[i]));
            for (unsigned j = 0; j < GHOST_DONGLES_MAX; j++)
                if (flows[j].src == ip)
                    memset(&flows[j].stream, 0, sizeof(flows[j].stream));
        }
    }
    atomic_store(&record_length, c->layout);
    for (unsigned i = 0; i < GHOST_DONGLES_MAX; i++) {
        source_layouts[i].ip = c->dongles[i].ip[0] ? inet_addr(c->dongles[i].ip) : 0;
        source_layouts[i].profile = c->dongle_profiles[i];
        source_layouts[i].layout =
            ghost_decoder_profile_layout((ghost_decoder_profile_t)c->dongle_profiles[i]);
        atomic_store(&route_ips[i], source_layouts[i].ip);
        atomic_store(&route_cloud[i], c->dongle_cloud[i]);
    }
    if (data_lock)
        xSemaphoreGive(data_lock);
}
void ghost_telemetry_get(const char *ip, ghost_source_t *out) {
    memset(out, 0, sizeof(*out));
    if (!data_lock || !ip || !*ip)
        return;
    xSemaphoreTake(data_lock, portMAX_DELAY);
    ghost_store_get(&telemetry, inet_addr(ip), out);
    xSemaphoreGive(data_lock);
}
bool ghost_dongle_identity_get(const char *ip, ghost_dongle_identity_t *out,
                               uint32_t *observed_seconds_ago) {
    if (!data_lock || !ip || !*ip || !out || !observed_seconds_ago)
        return false;
    uint32_t address = inet_addr(ip);
    bool found = false;
    xSemaphoreTake(data_lock, portMAX_DELAY);
    for (unsigned i = 0; i < GHOST_DONGLES_MAX; i++)
        if (identities[i].ip == address && address) {
            *out = identities[i].data;
            uint64_t now = ghost_millis();
            *observed_seconds_ago = now >= identities[i].observed_ms
                                        ? (uint32_t)((now - identities[i].observed_ms) / 1000)
                                        : 0;
            found = true;
            break;
        }
    xSemaphoreGive(data_lock);
    return found;
}
typedef struct {
    httpd_req_t *request;
    QueueHandle_t packets;
} stream_job_t;

static void stream_download(void *arg) {
    stream_job_t *job = arg;
    httpd_req_t *r = job->request;
    uint8_t header[24];
    ghost_pcap_header(header, SNAP);
    httpd_resp_set_type(r, "application/vnd.tcpdump.pcap");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    httpd_resp_set_hdr(r, "Content-Disposition", "attachment; filename=solarproxxie-live.pcap");
    esp_err_t result = httpd_resp_send_chunk(r, (const char *)header, sizeof(header));
    while (result == ESP_OK && !atomic_load(&stream_stop)) {
        packet_t packet;
        if (xQueueReceive(job->packets, &packet, pdMS_TO_TICKS(1000)) != pdTRUE) {
            char byte;
            int received = recv(httpd_req_to_sockfd(r), &byte, 1, MSG_PEEK | MSG_DONTWAIT);
            if (received == 0 || (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
                break;
            continue;
        }
        uint8_t record[16];
        ghost_pcap_record(record, packet.ms, packet.len, packet.wire);
        result = httpd_resp_send_chunk(r, (const char *)record, sizeof(record));
        if (result == ESP_OK)
            result = httpd_resp_send_chunk(r, (const char *)packet.data, packet.len);
        if (result == ESP_OK)
            atomic_fetch_add(&stream_packets, 1);
    }
    xSemaphoreTake(data_lock, portMAX_DELAY);
    stream_active = false;
    stream_queue = NULL;
    xSemaphoreGive(data_lock);
    vQueueDelete(job->packets);
    if (result == ESP_OK)
        httpd_resp_send_chunk(r, NULL, 0);
    httpd_req_async_handler_complete(r);
    free(job);
    vTaskDelete(NULL);
}

esp_err_t ghost_capture_stream_start(httpd_req_t *r) {
    if (!data_lock)
        return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(data_lock, portMAX_DELAY);
    bool busy = stream_active;
    if (!busy)
        stream_active = true;
    xSemaphoreGive(data_lock);
    if (busy)
        return ESP_ERR_INVALID_STATE;
    stream_job_t *job = calloc(1, sizeof(*job));
    if (job)
        job->packets = xQueueCreate(STREAM_QUEUE_DEPTH, sizeof(packet_t));
    esp_err_t result =
        job && job->packets ? httpd_req_async_handler_begin(r, &job->request) : ESP_ERR_NO_MEM;
    if (result == ESP_OK) {
        atomic_store(&stream_stop, false);
        atomic_store(&stream_drops, 0);
        atomic_store(&stream_packets, 0);
        xSemaphoreTake(data_lock, portMAX_DELAY);
        stream_started_ms = ghost_millis();
        stream_queue = job->packets;
        xSemaphoreGive(data_lock);
        if (xTaskCreatePinnedToCore(stream_download, "pcap_download", 6144, job, 1, NULL, 1) ==
            pdPASS)
            return ESP_OK;
        xSemaphoreTake(data_lock, portMAX_DELAY);
        stream_queue = NULL;
        xSemaphoreGive(data_lock);
        httpd_req_async_handler_complete(job->request);
        result = ESP_ERR_NO_MEM;
    }
    if (job) {
        if (job->packets)
            vQueueDelete(job->packets);
        free(job);
    }
    xSemaphoreTake(data_lock, portMAX_DELAY);
    stream_active = false;
    xSemaphoreGive(data_lock);
    return result;
}

void ghost_capture_stream_stop(void) {
    atomic_store(&stream_stop, true);
}
cJSON *ghost_packet_list(void) {
    cJSON *a = cJSON_CreateArray();
    if (!data_lock)
        return a;
    for (unsigned i = 0; i < 4; i++) {
        flow_t f;
        xSemaphoreTake(data_lock, portMAX_DELAY);
        memcpy(&f, &flows[i], offsetof(flow_t, stream));
        xSemaphoreGive(data_lock);
        if (!f.seen)
            continue;
        cJSON *j = cJSON_CreateObject();
        char ip[16];
        inet_ntoa_r((struct in_addr){.s_addr = f.src}, ip, sizeof(ip));
        cJSON_AddStringToObject(j, "source", ip);
        inet_ntoa_r((struct in_addr){.s_addr = f.dst}, ip, sizeof(ip));
        cJSON_AddStringToObject(j, "remote_ip", ip);
        cJSON_AddNumberToObject(j, "source_port", f.sport);
        cJSON_AddNumberToObject(j, "remote_port", f.dport);
        cJSON_AddNumberToObject(j, "packets", f.packets);
        cJSON_AddNumberToObject(j, "replies", f.replies);
        cJSON_AddNumberToObject(j, "last_activity_ms", f.seen);
        cJSON_AddStringToObject(j, "state",
                                ghost_millis() - f.seen < 30000 ? "recent traffic" : "idle");
        cJSON_AddItemToArray(a, j);
    }
    return a;
}
cJSON *ghost_router_status(void) {
    cJSON *j = cJSON_CreateObject();
    if (!data_lock) {
        cJSON_AddStringToObject(j, "error",
                                "Packet inspection unavailable; routing remains independent");
        return j;
    }
#define N(name, var) cJSON_AddNumberToObject(j, name, atomic_load(&var))
    N("packets_received", received);
    N("packets_downstream", received_downstream);
    N("packets_inspected", inspected);
    N("packets_decoded", decoded);
    N("packets_rejected", rejected);
    N("copy_drops", copy_drops);
    N("forward_attempts", forward_attempts);
    N("blocked", blocked);
    N("offline_dns_responses", dns_spoofed);
    N("offline_dns_errors", dns_spoof_errors);
    cJSON_AddNumberToObject(j, "queue_used", queue ? uxQueueMessagesWaiting(queue) : 0);
    cJSON_AddNumberToObject(j, "queue_capacity", QUEUE_DEPTH);
    xSemaphoreTake(data_lock, portMAX_DELAY);
    bool streaming = stream_active;
    uint64_t stream_start = stream_started_ms;
    xSemaphoreGive(data_lock);
    cJSON_AddBoolToObject(j, "stream_active", streaming);
    cJSON_AddNumberToObject(j, "stream_packets", atomic_load(&stream_packets));
    cJSON_AddNumberToObject(j, "stream_drops", atomic_load(&stream_drops));
    cJSON_AddNumberToObject(j, "stream_elapsed_seconds",
                            streaming ? (ghost_millis() - stream_start) / 1000 : 0);
    cJSON *sources = cJSON_AddArrayToObject(j, "dongles");
    for (unsigned i = 0; i < GHOST_TELEMETRY_SLOTS; i++) {
        ghost_source_t source;
        xSemaphoreTake(data_lock, portMAX_DELAY);
        source = telemetry.sources[i];
        xSemaphoreGive(data_lock);
        if (!source.ip)
            continue;
        cJSON *item = cJSON_CreateObject();
        char address[16];
        inet_ntoa_r((struct in_addr){.s_addr = source.ip}, address, sizeof(address));
        cJSON_AddStringToObject(item, "dongle_ip", address);
        unsigned layout = atomic_load(&record_length);
        xSemaphoreTake(data_lock, portMAX_DELAY);
        for (unsigned d = 0; d < GHOST_DONGLES_MAX; d++)
            if (source_layouts[d].ip == source.ip) {
                layout = source_layouts[d].layout;
                cJSON_AddStringToObject(
                    item, "decoder_profile",
                    ghost_decoder_profile_id((ghost_decoder_profile_t)source_layouts[d].profile));
                break;
            }
        xSemaphoreGive(data_lock);
        cJSON_AddNumberToObject(item, "configured_layout", layout);
        cJSON_AddStringToObject(item, "inverter_serial", source.values.serial);
        cJSON_AddNumberToObject(item, "last_inverter_ms", source.updated);
        cJSON_AddNumberToObject(item, "last_valid_age_seconds",
                                (ghost_millis() - source.updated) / 1000);
        cJSON_AddNumberToObject(item, "packets_decoded", source.frames);
        cJSON_AddBoolToObject(item, "serial_conflict", source.conflict);
        cJSON_AddItemToArray(sources, item);
    }
    cJSON_AddNumberToObject(j, "configured_layout", atomic_load(&record_length));
    cJSON_AddNumberToObject(j, "modbus_frames", 0);
    cJSON_AddStringToObject(j, "modbus_status",
                            "Inteless register snapshots; raw Modbus encapsulation unverified");
#ifdef CONFIG_GHOST_TEST_PACKET_REPLAY
    cJSON_AddBoolToObject(j, "synthetic_replay", true);
#else
    cJSON_AddBoolToObject(j, "synthetic_replay", false);
#endif
    cJSON_AddBoolToObject(j, "decoder_alive",
                          ghost_millis() - atomic_load(&decoder_heartbeat) < 5000);
    return j;
#undef N
}
