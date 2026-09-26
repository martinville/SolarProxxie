#include "app.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "protocol/cloud_emulator.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    int fd;
    uint32_t ip;
    uint64_t last_activity_ms;
    ghost_cloud_role_t role;
    size_t used;
    uint8_t input[GHOST_CLOUD_FRAME_MAX];
} offline_client_t;

static atomic_uint accepted, rejected, responses, protocol_errors, active, fallback_clock_responses;
static atomic_bool listening, reset_clients;
static TaskHandle_t offline_task;
static offline_client_t clients[GHOST_DONGLES_MAX];
static portMUX_TYPE role_lock = portMUX_INITIALIZER_UNLOCKED;
static unsigned role_sequence;
static struct {
    uint32_t ip;
    unsigned telemetry_sequence;
    unsigned redirect_sequence;
} pending_roles[GHOST_DONGLES_MAX];

void ghost_offline_note_dns(uint32_t ip, ghost_cloud_role_t role) {
    if (!ip || role == GHOST_CLOUD_ROLE_UNKNOWN)
        return;
    portENTER_CRITICAL(&role_lock);
    unsigned slot = 0;
    for (unsigned i = 0; i < GHOST_DONGLES_MAX; i++) {
        if (pending_roles[i].ip == ip) {
            slot = i;
            break;
        }
        if (!pending_roles[i].ip)
            slot = i;
    }
    if (!pending_roles[slot].ip)
        pending_roles[slot].ip = ip;
    unsigned *sequence = role == GHOST_CLOUD_ROLE_REDIRECT
                             ? &pending_roles[slot].redirect_sequence
                             : &pending_roles[slot].telemetry_sequence;
    if (!*sequence)
        *sequence = ++role_sequence;
    portEXIT_CRITICAL(&role_lock);
}

void ghost_offline_reset_clients(void) {
    atomic_store(&reset_clients, true);
}

static void clear_pending_roles(void) {
    portENTER_CRITICAL(&role_lock);
    memset(pending_roles, 0, sizeof(pending_roles));
    portEXIT_CRITICAL(&role_lock);
}

static ghost_cloud_role_t claim_role(uint32_t ip) {
    ghost_cloud_role_t role = GHOST_CLOUD_ROLE_TELEMETRY;
    portENTER_CRITICAL(&role_lock);
    for (unsigned i = 0; i < GHOST_DONGLES_MAX; i++)
        if (pending_roles[i].ip == ip) {
            unsigned telemetry = pending_roles[i].telemetry_sequence;
            unsigned redirect = pending_roles[i].redirect_sequence;
            if (redirect && (!telemetry || redirect < telemetry)) {
                role = GHOST_CLOUD_ROLE_REDIRECT;
                pending_roles[i].redirect_sequence = 0;
            } else
                pending_roles[i].telemetry_sequence = 0;
            if (!pending_roles[i].telemetry_sequence && !pending_roles[i].redirect_sequence)
                pending_roles[i].ip = 0;
            break;
        }
    portEXIT_CRITICAL(&role_lock);
    return role;
}

static bool clock_bytes(uint8_t out[6], ghost_cloud_role_t role, bool *synchronized) {
    time_t now = time(NULL);
    *synchronized = now >= 1704067200;
    if (!*synchronized) {
        const esp_app_desc_t *app = esp_app_get_description();
        if (!ghost_cloud_fallback_clock(out, app->date, app->time, ghost_millis() / 1000, role))
            return false;
        return true;
    }
    if (role == GHOST_CLOUD_ROLE_TELEMETRY)
        now += 2 * 60 * 60; /* Captured UK endpoint supplies the inverter's UTC+2 clock. */
    struct tm value = {0};
    gmtime_r(&now, &value);
    out[0] = (uint8_t)((value.tm_year + 1900) % 100);
    out[1] = (uint8_t)(value.tm_mon + 1);
    out[2] = (uint8_t)value.tm_mday;
    out[3] = (uint8_t)value.tm_hour;
    out[4] = (uint8_t)value.tm_min;
    out[5] = (uint8_t)value.tm_sec;
    return true;
}

static bool send_all(int fd, const uint8_t *data, size_t length) {
    while (length) {
        int sent = send(fd, data, length, 0);
        if (sent <= 0)
            return false;
        data += sent;
        length -= sent;
    }
    return true;
}

static void close_client(offline_client_t *client) {
    if (client->fd >= 0) {
        shutdown(client->fd, SHUT_RDWR);
        close(client->fd);
        client->fd = -1;
        client->used = 0;
        atomic_fetch_sub(&active, 1);
    }
}

static bool process_client(offline_client_t *client) {
    int got = recv(client->fd, client->input + client->used,
                   sizeof(client->input) - client->used, 0);
    if (got <= 0) {
        if (got < 0)
            ESP_LOGW("offline", "Cloud session receive failed: errno %d", errno);
        else
            ESP_LOGI("offline", "Dongle closed cloud session");
        return false;
    }
    client->used += (size_t)got;
    client->last_activity_ms = ghost_millis();
    while (client->used >= 11) {
        if (client->input[0] != 0xa5 || client->input[1] != 0x06 || client->input[2] != 0x01) {
            memmove(client->input, client->input + 1, --client->used);
            atomic_fetch_add(&protocol_errors, 1);
            continue;
        }
        size_t frame = 11 + ((size_t)client->input[9] << 8) + client->input[10];
        if (frame > sizeof(client->input)) {
            atomic_fetch_add(&protocol_errors, 1);
            return false;
        }
        if (client->used < frame)
            break;
        uint8_t reply[GHOST_CLOUD_FRAME_MAX], clock[6];
        bool clock_synchronized = false;
        bool clock_valid = clock_bytes(clock, client->role, &clock_synchronized);
        size_t length = ghost_cloud_response(client->input, frame, reply, sizeof(reply),
                                             clock_valid ? clock : NULL, client->role);
        if (!length) {
            ESP_LOGW("offline",
                     "Unsupported cloud request type 0x%02x (role=%s, clock=%s)",
                     client->input[3],
                     client->role == GHOST_CLOUD_ROLE_REDIRECT ? "redirect" : "telemetry",
                     clock_synchronized ? "network" : clock_valid ? "firmware-fallback" : "invalid");
            atomic_fetch_add(&protocol_errors, 1);
            return false;
        }
        if (!send_all(client->fd, reply, length)) {
            ESP_LOGW("offline", "Could not send cloud reply type 0x%02x: errno %d",
                     client->input[3], errno);
            atomic_fetch_add(&protocol_errors, 1);
            return false;
        }
        ESP_LOGI("offline", "Answered cloud request type 0x%02x with %u bytes (role=%s)",
                 client->input[3], (unsigned)length,
                 client->role == GHOST_CLOUD_ROLE_REDIRECT ? "redirect" : "telemetry");
        atomic_fetch_add(&responses, 1);
        if (client->input[3] == 0x01 && !clock_synchronized)
            atomic_fetch_add(&fallback_clock_responses, 1);
        client->used -= frame;
        memmove(client->input, client->input + frame, client->used);
    }
    return true;
}

static void server(void *arg) {
    for (;;) {
        if (!ghost_router_local_enabled()) {
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        int reuse = 1;
        struct sockaddr_in address = {.sin_family = AF_INET,
                                      .sin_port = htons(51100),
                                      .sin_addr.s_addr = htonl(INADDR_ANY)};
        if (listener < 0 ||
            setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0 ||
            bind(listener, (struct sockaddr *)&address, sizeof(address)) < 0 ||
            listen(listener, GHOST_DONGLES_MAX) < 0) {
            ESP_LOGE("offline", "Could not open TCP/51100: errno %d", errno);
            if (listener >= 0)
                close(listener);
            atomic_store(&listening, false);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        for (unsigned i = 0; i < GHOST_DONGLES_MAX; i++)
            clients[i].fd = -1;
        atomic_store(&listening, true);
        ESP_LOGI("offline", "Local dongle service listening on TCP/51100");
        while (ghost_router_local_enabled()) {
        if (atomic_exchange(&reset_clients, false)) {
            for (unsigned i = 0; i < GHOST_DONGLES_MAX; i++)
                close_client(&clients[i]);
            clear_pending_roles();
            ESP_LOGI("offline", "Cleared cloud sessions after dongle Wi-Fi disconnect");
        }
        fd_set ready;
        FD_ZERO(&ready);
        FD_SET(listener, &ready);
        int highest = listener;
        for (unsigned i = 0; i < GHOST_DONGLES_MAX; i++)
            if (clients[i].fd >= 0) {
                if (!ghost_router_local_for_ip(clients[i].ip) ||
                    ghost_millis() - clients[i].last_activity_ms > 120000) {
                    close_client(&clients[i]);
                    continue;
                }
                FD_SET(clients[i].fd, &ready);
                if (clients[i].fd > highest)
                    highest = clients[i].fd;
            }
        struct timeval timeout = {.tv_sec = 1};
        int count = select(highest + 1, &ready, NULL, NULL, &timeout);
        if (count < 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (count > 0 && FD_ISSET(listener, &ready)) {
            struct sockaddr_in peer;
            socklen_t size = sizeof(peer);
            int fd = accept(listener, (struct sockaddr *)&peer, &size);
            offline_client_t *slot = NULL;
            offline_client_t *oldest_same_ip = NULL;
            unsigned same_ip = 0;
            for (unsigned i = 0; i < GHOST_DONGLES_MAX; i++) {
                if (clients[i].fd < 0 && !slot)
                    slot = &clients[i];
                if (clients[i].fd >= 0 && clients[i].ip == peer.sin_addr.s_addr) {
                    same_ip++;
                    if (!oldest_same_ip ||
                        clients[i].last_activity_ms < oldest_same_ip->last_activity_ms)
                        oldest_same_ip = &clients[i];
                }
            }
            /* Captures show two simultaneous roles per dongle. A third connection is
             * a retry: replace the oldest session instead of exhausting lwIP sockets
             * and starving the HTTP server, MQTT, DNS and reachability probe. */
            if (fd >= 0 && same_ip >= 2 && oldest_same_ip) {
                close_client(oldest_same_ip);
                slot = oldest_same_ip;
            }
            if (fd < 0 || !slot || !ghost_router_local_for_ip(peer.sin_addr.s_addr)) {
                if (fd >= 0)
                    close(fd);
                atomic_fetch_add(&rejected, 1);
            } else {
                struct timeval io_timeout = {.tv_sec = 2};
                int keepalive = 1, keep_idle = 75, keep_interval = 10, keep_count = 3;
                setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &io_timeout, sizeof(io_timeout));
                setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &io_timeout, sizeof(io_timeout));
                setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
                setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &keep_idle, sizeof(keep_idle));
                setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &keep_interval,
                           sizeof(keep_interval));
                setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &keep_count, sizeof(keep_count));
                ghost_cloud_role_t role = claim_role(peer.sin_addr.s_addr);
                bool has_telemetry = false, has_redirect = false;
                for (unsigned i = 0; i < GHOST_DONGLES_MAX; i++)
                    if (clients[i].fd >= 0 && &clients[i] != slot &&
                        clients[i].ip == peer.sin_addr.s_addr) {
                        has_telemetry |= clients[i].role == GHOST_CLOUD_ROLE_TELEMETRY;
                        has_redirect |= clients[i].role == GHOST_CLOUD_ROLE_REDIRECT;
                    }
                /* A reconnect can reuse cached DNS and therefore arrive without a new
                 * hostname hint. Keep the two observed endpoint roles unique per dongle
                 * instead of allowing two telemetry sessions and breaking redirect login. */
                if (role == GHOST_CLOUD_ROLE_TELEMETRY && has_telemetry && !has_redirect)
                    role = GHOST_CLOUD_ROLE_REDIRECT;
                else if (role == GHOST_CLOUD_ROLE_REDIRECT && has_redirect && !has_telemetry)
                    role = GHOST_CLOUD_ROLE_TELEMETRY;
                *slot = (offline_client_t){.fd = fd,
                                           .ip = peer.sin_addr.s_addr,
                                           .last_activity_ms = ghost_millis(),
                                           .role = role};
                char peer_address[16];
                inet_ntoa_r(peer.sin_addr, peer_address, sizeof(peer_address));
                ESP_LOGI("offline", "Accepted %s cloud session from %s",
                         slot->role == GHOST_CLOUD_ROLE_REDIRECT ? "redirect" : "telemetry",
                         peer_address);
                atomic_fetch_add(&accepted, 1);
                atomic_fetch_add(&active, 1);
            }
        }
        for (unsigned i = 0; i < GHOST_DONGLES_MAX; i++)
            if (clients[i].fd >= 0 && FD_ISSET(clients[i].fd, &ready) &&
                !process_client(&clients[i]))
                close_client(&clients[i]);
        }
        for (unsigned i = 0; i < GHOST_DONGLES_MAX; i++)
            close_client(&clients[i]);
        close(listener);
        atomic_store(&listening, false);
        ESP_LOGI("offline", "Local dongle service stopped");
    }
}

void ghost_offline_start(void) {
    xTaskCreate(server, "offline_cloud", 4096, NULL, 3, &offline_task);
}
bool ghost_offline_ready(void) {
    return atomic_load(&listening);
}

cJSON *ghost_offline_status(void) {
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "enabled", atomic_load(&ghost_offline_active));
    cJSON_AddBoolToObject(json, "server_ready", atomic_load(&listening));
    cJSON_AddBoolToObject(json, "clock_ready", true);
    cJSON_AddBoolToObject(json, "clock_synchronized", time(NULL) >= 1704067200);
    cJSON_AddNumberToObject(json, "fallback_clock_responses",
                            atomic_load(&fallback_clock_responses));
    cJSON_AddNumberToObject(json, "active_connections", atomic_load(&active));
    cJSON_AddNumberToObject(json, "accepted_connections", atomic_load(&accepted));
    cJSON_AddNumberToObject(json, "rejected_connections", atomic_load(&rejected));
    cJSON_AddNumberToObject(json, "responses", atomic_load(&responses));
    cJSON_AddNumberToObject(json, "protocol_errors", atomic_load(&protocol_errors));
    return json;
}
