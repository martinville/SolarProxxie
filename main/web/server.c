#include "app.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "mbedtls/platform_util.h"
#include "mqtt/discovery.h"
#include "network/clients.h"
#include "esp_http_client.h"
#include "security/security.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern const uint8_t html_start[] asm("_binary_index_html_gz_start");
extern const uint8_t html_end[] asm("_binary_index_html_gz_end");
extern const uint8_t css_start[] asm("_binary_style_css_gz_start");
extern const uint8_t css_end[] asm("_binary_style_css_gz_end");
extern const uint8_t js_start[] asm("_binary_app_js_gz_start");
extern const uint8_t js_end[] asm("_binary_app_js_gz_end");
extern const uint8_t logo_start[] asm("_binary_logo_svg_gz_start");
extern const uint8_t logo_end[] asm("_binary_logo_svg_gz_end");
typedef struct {
    char token[65], csrf[65];
    uint64_t expires;
    uint32_t peer;
} session_t;
static session_t sessions[4];
static char setup_csrf[65];
static uint64_t login_after;
static unsigned attempts;
static atomic_bool login_busy;
static bool connected_mapped_dongle(const ghost_config_t *config, const char *ip) {
    if (ghost_setup || !config || !ip || strlen(ip) >= 16)
        return false;
    bool mapped = false;
    for (unsigned i = 0; i < GHOST_DONGLES_MAX; i++)
        if (config->dongles[i].ip[0] && !strcmp(ip, config->dongles[i].ip)) {
            mapped = true;
            break;
        }
    if (!mapped)
        return false;
    ghost_ap_clients_t clients;
    ghost_network_clients(&clients);
    bool connected = false;
    uint32_t address = inet_addr(ip);
    if (clients.error == ESP_OK)
        for (unsigned i = 0; i < clients.count; i++)
            if (clients.clients[i].address.ip.addr == address) {
                connected = true;
                break;
            }
    return connected;
}
static bool reboot_connected_dongle(const ghost_config_t *config, const char *ip) {
    if (!connected_mapped_dongle(config, ip))
        return false;
    char url[64];
    snprintf(url, sizeof(url), "http://%s/config?command=reboot", ip);
    esp_http_client_config_t request = {
        .url = url, .timeout_ms = 3000, .disable_auto_redirect = true};
    esp_http_client_handle_t client = esp_http_client_init(&request);
    if (!client)
        return false;
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_err_t result = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return result == ESP_OK && status >= 200 && status < 300;
}
static cJSON *connected_dongle_info(const ghost_config_t *config, const char *ip) {
    if (!connected_mapped_dongle(config, ip))
        return NULL;
    ghost_dongle_identity_t identity;
    uint32_t age;
    if (!ghost_dongle_identity_get(ip, &identity, &age))
        return NULL;
    cJSON *out = cJSON_CreateObject();
    if (!out)
        return NULL;
    cJSON_AddStringToObject(out, "serial", identity.serial);
    cJSON_AddStringToObject(out, "register_key", identity.register_key);
    cJSON_AddNumberToObject(out, "observed_seconds_ago", age);
    memset(&identity, 0, sizeof(identity));
    return out;
}
static uint32_t login_verification_ms;
typedef struct {
    httpd_req_t *request;
    char password[129];
    uint8_t salt[16], hash[32], upgraded_salt[16], upgraded_hash[32];
    bool username_matches, valid, upgrade_hash;
    uint32_t peer, verification_ms;
} login_job_t;
static uint32_t peer_address(httpd_req_t *r) {
    struct sockaddr_in peer = {0};
    socklen_t length = sizeof(peer);
    return getpeername(httpd_req_to_sockfd(r), (struct sockaddr *)&peer, &length) == 0
               ? peer.sin_addr.s_addr
               : 0;
}
static bool setup_interface(httpd_req_t *r) {
    if (!ghost_setup)
        return true;
    struct sockaddr_in local;
    socklen_t n = sizeof(local);
    esp_netif_ip_info_t ip;
    esp_netif_get_ip_info(ghost_ap, &ip);
    return getsockname(httpd_req_to_sockfd(r), (struct sockaddr *)&local, &n) == 0 &&
           local.sin_addr.s_addr == ip.ip.addr;
}
static esp_err_t json(httpd_req_t *r, cJSON *j) {
    if (!j)
        return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory unavailable");
    char *s = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!s)
        return ESP_ERR_NO_MEM;
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    esp_err_t e = httpd_resp_sendstr(r, s);
    free(s);
    return e;
}
static esp_err_t error(httpd_req_t *r, const char *status, const char *message) {
    httpd_resp_set_status(r, status);
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "error", message);
    return json(r, j);
}
static esp_err_t ok(httpd_req_t *r) {
    cJSON *j = cJSON_CreateObject();
    cJSON_AddBoolToObject(j, "ok", true);
    return json(r, j);
}
static cJSON *body(httpd_req_t *r);
/* Finish on the HTTP-server task, which owns sessions and login backoff state. */
static void login_complete(void *context) {
    login_job_t *job = context;
    httpd_req_t *r = job->request;
    login_verification_ms = job->verification_ms;
    ESP_LOGI("web", "Password verification took %lu ms", (unsigned long)job->verification_ms);
    if (!job->valid || !job->peer) {
        if (attempts < 6)
            attempts++;
        login_after = ghost_millis() + (1000U << attempts);
        error(r, "401 Unauthorized", "Invalid username or password");
    } else {
        if (job->upgrade_hash) {
            ghost_config_t *c = malloc(sizeof(*c));
            if (c) {
                ghost_config_get(c);
                if (ghost_equal(c->salt, job->salt, sizeof(c->salt)) &&
                    ghost_equal(c->hash, job->hash, sizeof(c->hash))) {
                    memcpy(c->salt, job->upgraded_salt, sizeof(c->salt));
                    memcpy(c->hash, job->upgraded_hash, sizeof(c->hash));
                    if (ghost_config_save(c) != ESP_OK)
                        ESP_LOGW("web", "Could not save faster password hash");
                }
                mbedtls_platform_zeroize(c, sizeof(*c));
                free(c);
            }
        }
        attempts = 0;
        session_t *s = &sessions[0];
        for (unsigned i = 0; i < 4; i++)
            if (sessions[i].expires < s->expires)
                s = &sessions[i];
        ghost_random_hex(s->token, 32);
        ghost_random_hex(s->csrf, 32);
        s->expires = ghost_millis() + 1800000;
        s->peer = job->peer;
        char cookie[160];
        snprintf(cookie, sizeof(cookie),
                 "ghost=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=1800", s->token);
        httpd_resp_set_hdr(r, "Set-Cookie", cookie);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "csrf", s->csrf);
        json(r, o);
    }
    httpd_req_async_handler_complete(r);
    mbedtls_platform_zeroize(job, sizeof(*job));
    free(job);
    atomic_store(&login_busy, false);
}

static void login_worker(void *context) {
    login_job_t *job = context;
    uint64_t start = ghost_millis();
    bool matches = ghost_password_verify(job->password, job->salt, job->hash);
    if (!matches) {
        matches = ghost_password_verify_iterations(job->password, job->salt, job->hash,
                                                    GHOST_LEGACY_KDF_ITERATIONS);
        if (matches && job->username_matches) {
            esp_fill_random(job->upgraded_salt, sizeof(job->upgraded_salt));
            job->upgrade_hash = ghost_password_hash(job->password, job->upgraded_salt,
                                                    job->upgraded_hash);
        }
    }
    job->verification_ms = (uint32_t)(ghost_millis() - start);
    job->valid = matches && job->username_matches;
    mbedtls_platform_zeroize(job->password, sizeof(job->password));
    /* Ownership transfers to login_complete only when queueing succeeds. */
    if (httpd_queue_work(job->request->handle, login_complete, job) != ESP_OK) {
        error(job->request, "503 Service Unavailable", "Sign-in could not finish; try again");
        httpd_req_async_handler_complete(job->request);
        mbedtls_platform_zeroize(job, sizeof(*job));
        free(job);
        atomic_store(&login_busy, false);
    }
    vTaskDelete(NULL);
}

static esp_err_t login_begin(httpd_req_t *r) {
    if (ghost_setup)
        return error(r, "409 Conflict", "Device is in Setup Mode");
    if (atomic_load(&login_busy))
        return error(r, "429 Too Many Requests", "A sign-in is in progress; wait and try again");
    if (ghost_millis() < login_after)
        return error(r, "429 Too Many Requests", "Wait before trying again");
    cJSON *j = body(r);
    if (!j)
        return error(r, "400 Bad Request", "Invalid JSON");
    const cJSON *u = cJSON_GetObjectItemCaseSensitive(j, "username"),
                *p = cJSON_GetObjectItemCaseSensitive(j, "password");
    if (!cJSON_IsString(u) || !cJSON_IsString(p) || strlen(u->valuestring) > 32 ||
        strlen(p->valuestring) > 128) {
        if (cJSON_IsString(p))
            mbedtls_platform_zeroize(p->valuestring, strlen(p->valuestring));
        cJSON_Delete(j);
        return error(r, "400 Bad Request", "Invalid login fields");
    }
    login_job_t *job = calloc(1, sizeof(*job));
    ghost_config_t *c = malloc(sizeof(*c));
    if (!job || !c) {
        free(job);
        free(c);
        mbedtls_platform_zeroize(p->valuestring, strlen(p->valuestring));
        cJSON_Delete(j);
        return error(r, "503 Service Unavailable", "Memory unavailable; try again");
    }
    ghost_config_get(c);
    job->username_matches = !strcmp(u->valuestring, c->admin);
    memcpy(job->salt, c->salt, sizeof(job->salt));
    memcpy(job->hash, c->hash, sizeof(job->hash));
    strcpy(job->password, p->valuestring);
    job->peer = peer_address(r);
    mbedtls_platform_zeroize(c, sizeof(*c));
    free(c);
    mbedtls_platform_zeroize(p->valuestring, strlen(p->valuestring));
    cJSON_Delete(j);
    if (httpd_req_async_handler_begin(r, &job->request) != ESP_OK) {
        mbedtls_platform_zeroize(job, sizeof(*job));
        free(job);
        return error(r, "503 Service Unavailable", "Sign-in could not start; try again");
    }
    atomic_store(&login_busy, true);
    if (xTaskCreatePinnedToCore(login_worker, "login_verify", 6144, job, 1, NULL, 1) != pdPASS) {
        error(job->request, "503 Service Unavailable", "Sign-in worker unavailable; try again");
        httpd_req_async_handler_complete(job->request);
        mbedtls_platform_zeroize(job, sizeof(*job));
        free(job);
        atomic_store(&login_busy, false);
    }
    return ESP_OK;
}
static session_t *session(httpd_req_t *r) {
    char cookie[256];
    if (httpd_req_get_hdr_value_str(r, "Cookie", cookie, sizeof(cookie)) != ESP_OK)
        return NULL;
    char *p = cookie;
    while (*p) {
        while (*p == ' ' || *p == ';')
            p++;
        char *end = strchr(p, ';');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (n == 70 && !strncmp(p, "ghost=", 6))
            for (unsigned i = 0; i < 4; i++)
                if (sessions[i].peer && sessions[i].peer == peer_address(r) &&
                    sessions[i].expires > ghost_millis() &&
                    ghost_equal(p + 6, sessions[i].token, 64)) {
                    sessions[i].expires = ghost_millis() + 1800000;
                    return &sessions[i];
                }
        if (!end)
            break;
        p = end + 1;
    }
    return NULL;
}
static bool csrf(httpd_req_t *r, const char *expected) {
    char value[80];
    return httpd_req_get_hdr_value_str(r, "X-CSRF-Token", value, sizeof(value)) == ESP_OK &&
           strlen(value) == 64 && ghost_equal(value, expected, 64);
}
static cJSON *body(httpd_req_t *r) {
    if (r->content_len <= 0 || r->content_len > 24000)
        return NULL;
    char type[64];
    if (httpd_req_get_hdr_value_str(r, "Content-Type", type, sizeof(type)) != ESP_OK ||
        strncmp(type, "application/json", 16))
        return NULL;
    char *s = malloc(r->content_len + 1);
    if (!s)
        return NULL;
    int got = 0, timeouts = 0;
    while (got < r->content_len) {
        int n = httpd_req_recv(r, s + got, r->content_len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT && timeouts++ < 2)
            continue;
        if (n <= 0) {
            free(s);
            return NULL;
        }
        got += n;
    }
    s[got] = 0;
    const char *end = NULL;
    cJSON *j = cJSON_ParseWithLengthOpts(s, got + 1, &end, true);
    memset(s, 0, got);
    free(s);
    return j;
}
static esp_err_t points(httpd_req_t *r, const ghost_config_t *c, unsigned slot) {
    ghost_source_t source;
    ghost_telemetry_get(c->dongles[slot].ip, &source);
    ghost_values_t v = source.values;
    uint64_t updated = source.updated;
    if (source.conflict)
        v.valid = 0;
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    if (httpd_resp_send_chunk(r, "[", 1) != ESP_OK)
        return ESP_FAIL;
    for (size_t i = 0; i < ghost_field_count; i++) {
        cJSON *j = cJSON_CreateObject();
        const ghost_field_t *f = &ghost_fields[i];
        const ghost_entity_t *e = &c->entities[i];
        cJSON_AddStringToObject(j, "id", f->id);
        cJSON_AddStringToObject(j, "name", e->name);
        cJSON_AddStringToObject(j, "suggested_name", f->name);
        cJSON_AddStringToObject(j, "ha_name", e->ha_name);
        cJSON_AddStringToObject(j, "suffix", e->suffix);
        cJSON_AddNumberToObject(j, "slot", slot + 1);
        cJSON_AddStringToObject(j, "inverter_name", c->dongles[slot].name);
        cJSON_AddStringToObject(j, "dongle_ip", c->dongles[slot].ip);
        cJSON_AddStringToObject(j, "inverter_serial", source.values.serial);
        cJSON_AddBoolToObject(j, "serial_conflict", source.conflict);
        char topic[192];
        cJSON_AddStringToObject(j, "mqtt_topic",
                                ghost_mapped_topic(topic, sizeof(topic), c, slot, i) ? topic : "");
        cJSON_AddStringToObject(j, "unit", e->unit);
        cJSON_AddBoolToObject(j, "enabled", e->enabled);
        if (v.valid & (UINT64_C(1) << i))
            cJSON_AddNumberToObject(j, "value", v.value[i]);
        else
            cJSON_AddNullToObject(j, "value");
        cJSON_AddNumberToObject(j, "register", f->reg[0]);
        cJSON_AddNumberToObject(j, "register2", f->reg[1]);
        cJSON_AddNumberToObject(j, "words", f->words);
        cJSON_AddNumberToObject(j, "scale", f->scale);
        cJSON_AddNumberToObject(j, "offset", f->offset);
        cJSON_AddStringToObject(j, "evidence", f->evidence);
        cJSON_AddNumberToObject(j, "updated_ms", updated);
        cJSON_AddStringToObject(j, "status",
                                source.conflict                   ? "serial conflict"
                                : !(v.valid & (UINT64_C(1) << i)) ? "unobserved"
                                : ghost_millis() - updated >= (uint64_t)c->stale_seconds * 1000
                                    ? "stale"
                                    : "fresh");
        char *text = cJSON_PrintUnformatted(j);
        cJSON_Delete(j);
        if (!text)
            return ESP_ERR_NO_MEM;
        esp_err_t result = ESP_OK;
        if (i)
            result = httpd_resp_send_chunk(r, ",", 1);
        if (result == ESP_OK)
            result = httpd_resp_send_chunk(r, text, strlen(text));
        free(text);
        if (result != ESP_OK)
            return result;
    }
    if (httpd_resp_send_chunk(r, "]", 1) != ESP_OK)
        return ESP_FAIL;
    return httpd_resp_send_chunk(r, NULL, 0);
}
static esp_err_t config_response(httpd_req_t *r, const ghost_config_t *c, bool setup) {
    cJSON *header = ghost_config_header_json(c, setup);
    char *text = cJSON_PrintUnformatted(header);
    cJSON_Delete(header);
    if (!text)
        return ESP_ERR_NO_MEM;
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    esp_err_t result = httpd_resp_send_chunk(r, text, strlen(text) - 1);
    free(text);
    if (result != ESP_OK)
        return result;
    if (httpd_resp_send_chunk(r, ",\"entities\":[", 13) != ESP_OK)
        return ESP_FAIL;
    for (size_t i = 0; i < ghost_field_count; i++) {
        cJSON *entity = ghost_entity_json(c, i);
        text = cJSON_PrintUnformatted(entity);
        cJSON_Delete(entity);
        if (!text)
            return ESP_ERR_NO_MEM;
        result = i ? httpd_resp_send_chunk(r, ",", 1) : ESP_OK;
        if (result == ESP_OK)
            result = httpd_resp_send_chunk(r, text, strlen(text));
        free(text);
        if (result != ESP_OK)
            return result;
    }
    if (httpd_resp_send_chunk(r, "]}", 2) != ESP_OK)
        return ESP_FAIL;
    return httpd_resp_send_chunk(r, NULL, 0);
}
static int point_slot(const char *uri, const char *prefix) {
    size_t n = strlen(prefix);
    if (!strcmp(uri, prefix))
        return 0;
    if (!strncmp(uri, prefix, n) && uri[n] == '/' && uri[n + 1] >= '1' &&
        uri[n + 1] < '1' + GHOST_DONGLES_MAX &&
        !uri[n + 2])
        return uri[n + 1] - '1';
    return -1;
}
static esp_err_t get(httpd_req_t *r) {
    if (!setup_interface(r))
        return error(r, "403 Forbidden", "Connect to the physical setup AP");
    if (!strcmp(r->uri, "/api/session")) {
        session_t *s = session(r);
        cJSON *j = cJSON_CreateObject();
        cJSON_AddBoolToObject(j, "setup", ghost_setup);
        cJSON_AddBoolToObject(j, "authenticated", s != NULL);
        cJSON_AddStringToObject(j, "version", esp_app_get_description()->version);
        if (ghost_setup || s)
            cJSON_AddStringToObject(j, "csrf", ghost_setup ? setup_csrf : s->csrf);
        return json(r, j);
    }
    if (!ghost_setup && !session(r))
        return error(r, "401 Unauthorized", "Sign in to continue");
    if (!strcmp(r->uri, "/api/status") || !strcmp(r->uri, "/api/diagnostics")) {
        cJSON *j = cJSON_CreateObject();
        cJSON_AddItemToObject(j, "system", ghost_diagnostics_status());
        cJSON_AddItemToObject(j, "network", ghost_network_status());
        cJSON_AddItemToObject(j, "router", ghost_router_status());
        cJSON_AddItemToObject(j, "mqtt", ghost_mqtt_status());
        cJSON_AddItemToObject(j, "dns", ghost_dns_status());
        cJSON_AddItemToObject(j, "internet", ghost_reachability_status());
        cJSON_AddItemToObject(j, "offline", ghost_offline_status());
        cJSON *auth = cJSON_AddObjectToObject(j, "authentication");
        cJSON_AddBoolToObject(auth, "busy", atomic_load(&login_busy));
        cJSON_AddNumberToObject(auth, "last_verification_ms", login_verification_ms);
        return json(r, j);
    }
    if (!strcmp(r->uri, "/api/gateway")) {
        cJSON *j = ghost_network_status();
        cJSON_AddItemToObject(j, "connections", ghost_packet_list());
        cJSON_AddItemToObject(j, "dongle_gui", ghost_dongle_gui_status());
        cJSON_AddItemToObject(j, "offline", ghost_offline_status());
        return json(r, j);
    }
    if (!strcmp(r->uri, "/api/update"))
        return ghost_setup ? error(r, "403 Forbidden", "Cloud update requires normal administrator mode")
                           : json(r, ghost_cloud_update_status());
    if (!strcmp(r->uri, "/api/scan")) {
        if (!ghost_setup)
            return error(r, "403 Forbidden", "Physical Setup Mode required");
        cJSON *a = ghost_network_scan();
        return a ? json(r, a) : error(r, "503 Service Unavailable", "Scan busy; retry shortly");
    }
    ghost_config_t *c = malloc(sizeof(*c));
    if (!c)
        return error(r, "503 Service Unavailable", "Memory unavailable");
    ghost_config_get(c);
    cJSON *j = NULL;
    bool debug = c->debug;
    if (!strcmp(r->uri, "/api/config") || !strcmp(r->uri, "/api/export")) {
        esp_err_t result = config_response(r, c, !strcmp(r->uri, "/api/config"));
        memset(c, 0, sizeof(*c));
        free(c);
        return result;
    }
    int slot = point_slot(r->uri, "/api/datapoints");
    if (slot >= 0) {
        esp_err_t result = points(r, c, (unsigned)slot);
        memset(c, 0, sizeof(*c));
        free(c);
        return result;
    }
    if (!strcmp(r->uri, "/api/mqtt"))
        j = ghost_mqtt_status();
    memset(c, 0, sizeof(*c));
    free(c);
    if (j)
        return json(r, j);
    if (!strcmp(r->uri, "/api/capture-stream")) {
        if (!debug)
            return error(r, "403 Forbidden", "Enable Debug Mode first");
        esp_err_t result = ghost_capture_stream_start(r);
        return result == ESP_OK
                   ? ESP_OK
                   : error(r,
                           result == ESP_ERR_INVALID_STATE ? "409 Conflict"
                                                           : "503 Service Unavailable",
                           result == ESP_ERR_INVALID_STATE ? "A long download is already running"
                                                           : "Could not start long download");
    }
    if (!strcmp(r->uri, "/api/packets")) {
        if (!debug)
            return error(r, "403 Forbidden", "Enable Debug Mode first");
        cJSON *packets = cJSON_CreateObject();
        cJSON_AddItemToObject(packets, "connections", ghost_packet_list());
        cJSON_AddItemToObject(packets, "status", ghost_router_status());
        return json(r, packets);
    }
    return error(r, "404 Not Found", "Unknown endpoint");
}
static esp_err_t post(httpd_req_t *r) {
    if (!setup_interface(r))
        return error(r, "403 Forbidden", "Connect to the physical setup AP");
    if (!strcmp(r->uri, "/api/login"))
        return login_begin(r);
    session_t *s = session(r);
    if (!ghost_setup && !s)
        return error(r, "401 Unauthorized", "Sign in to continue");
    if (!csrf(r, ghost_setup ? setup_csrf : s->csrf))
        return error(r, "403 Forbidden", "Invalid CSRF token");
    if (!strcmp(r->uri, "/api/logout")) {
        ghost_dongle_gui_set(NULL);
        if (s)
            memset(s, 0, sizeof(*s));
        httpd_resp_set_hdr(r, "Set-Cookie", "ghost=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
        return ok(r);
    }
    if (!strcmp(r->uri, "/api/ota")) {
        if (ghost_setup)
            return error(r, "403 Forbidden", "OTA requires normal administrator mode");
        return ghost_ota_upload(r);
    }
    cJSON *j = body(r);
    if (!j)
        return error(r, "400 Bad Request", "Invalid JSON or request too large");
    if (!strcmp(r->uri, "/api/update")) {
        if (ghost_setup) {
            cJSON_Delete(j);
            return error(r, "403 Forbidden", "Cloud update requires normal administrator mode");
        }
        const cJSON *action = cJSON_GetObjectItemCaseSensitive(j, "action");
        const cJSON *version = cJSON_GetObjectItemCaseSensitive(j, "version");
        char update_error[160] = "Invalid cloud update request";
        esp_err_t result = ESP_ERR_INVALID_ARG;
        if (cJSON_IsString(action) && !strcmp(action->valuestring, "check"))
            result = ghost_cloud_update_check(update_error, sizeof(update_error));
        else if (cJSON_IsString(action) && !strcmp(action->valuestring, "install") &&
                 cJSON_IsString(version))
            result = ghost_cloud_update_start(version->valuestring, update_error,
                                              sizeof(update_error));
        cJSON_Delete(j);
        return result == ESP_OK ? json(r, ghost_cloud_update_status())
                                : error(r, result == ESP_ERR_INVALID_STATE ? "409 Conflict"
                                                                         : "502 Bad Gateway",
                                        update_error);
    }
    if (!strcmp(r->uri, "/api/dongle-gui")) {
        const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(j, "enabled");
        const cJSON *ip = cJSON_GetObjectItemCaseSensitive(j, "ip");
        bool valid =
            !ghost_setup && cJSON_IsBool(enabled) && (!cJSON_IsTrue(enabled) || cJSON_IsString(ip));
        bool done = valid && ghost_dongle_gui_set(cJSON_IsTrue(enabled) ? ip->valuestring : NULL);
        cJSON_Delete(j);
        return done ? json(r, ghost_dongle_gui_status())
                    : error(r, "409 Conflict",
                            "Select a configured, connected dongle with a known IP while the "
                            "gateway is online");
    }
    ghost_config_t *c = malloc(sizeof(*c));
    if (!c) {
        cJSON_Delete(j);
        return error(r, "503 Service Unavailable", "Memory unavailable");
    }
    ghost_config_get(c);
    esp_err_t rc = ESP_OK;
    char err[160] = "Invalid settings";
    if (!strcmp(r->uri, "/api/dongle-info")) {
        const cJSON *ip = cJSON_GetObjectItemCaseSensitive(j, "ip");
        cJSON *info = cJSON_IsString(ip) ? connected_dongle_info(c, ip->valuestring) : NULL;
        rc = info ? json(r, info)
                  : error(r, "409 Conflict", "No cloud identity packet captured for this connected dongle yet");
    } else if (!strcmp(r->uri, "/api/dongle-reboot")) {
        const cJSON *ip = cJSON_GetObjectItemCaseSensitive(j, "ip");
        rc = cJSON_IsString(ip) && reboot_connected_dongle(c, ip->valuestring)
                 ? ok(r)
                 : error(r, "409 Conflict",
                         "Reboot not confirmed. Check that this mapped dongle is connected and reachable.");
    } else if (!strcmp(r->uri, "/api/config") || !strcmp(r->uri, "/api/import") ||
        !strcmp(r->uri, "/api/setup") || !strcmp(r->uri, "/api/wifi-test")) {
        bool setup = !strcmp(r->uri, "/api/setup") || !strcmp(r->uri, "/api/wifi-test");
        if (setup && !ghost_setup)
            rc = error(r, "403 Forbidden", "Physical Setup Mode required");
        else if (!ghost_config_from_json(c, j,
                                         (setup && ghost_setup) ||
                                             (!ghost_setup && !strcmp(r->uri, "/api/config")),
                                         err, sizeof(err)))
            rc = error(r, "400 Bad Request", err);
        else if (!strcmp(r->uri, "/api/wifi-test")) {
            rc = ghost_network_test(c) == ESP_OK
                     ? ok(r)
                     : error(r, "409 Conflict", "Wi-Fi test could not start");
        } else {
            const cJSON *force = cJSON_GetObjectItemCaseSensitive(j, "save_without_wifi");
            if (setup && !ghost_network_test_matches(c) && !cJSON_IsTrue(force))
                rc = error(r, "409 Conflict",
                           "Test these Wi-Fi settings first or explicitly choose Save without a "
                           "successful Wi-Fi test");
            else if (ghost_config_save(c) != ESP_OK)
                rc = error(r, "500 Internal Server Error", "Configuration was not saved");
            else {
                ghost_network_cloud(c->cloud);
                ghost_router_config(c);
                esp_log_level_set("*", c->log_level);
                if (!c->debug)
                    ghost_capture_stream_stop();
                if (!ghost_setup)
                    ghost_mqtt_reload();
                rc = ok(r);
                if (setup)
                    ghost_restart_later();
            }
        }
    } else if (!strcmp(r->uri, "/api/mqtt-test")) {
        if (!ghost_config_from_json(c, j, false, err, sizeof(err)))
            rc = error(r, "400 Bad Request", err);
        else
            rc = ghost_mqtt_test(c) == ESP_OK
                     ? ok(r)
                     : error(r, "409 Conflict", "MQTT test busy or broker missing");
    } else if (!strcmp(r->uri, "/api/action")) {
        const cJSON *cmd = cJSON_GetObjectItemCaseSensitive(j, "command");
        const char *a = cJSON_IsString(cmd) ? cmd->valuestring : "";
        if (!strcmp(a, "gateway-restart")) {
            ghost_network_restart();
            rc = ok(r);
        } else if (!strcmp(a, "discovery") || !strcmp(a, "publish") || !strcmp(a, "test")) {
            ghost_mqtt_command(a);
            rc = ok(r);
        } else if (!strcmp(a, "stop-stream")) {
            if (!c->debug)
                rc = error(r, "403 Forbidden", "Enable Debug Mode first");
            else {
                ghost_capture_stream_stop();
                rc = ok(r);
            }
        } else if (!strcmp(a, "restart")) {
            rc = ok(r);
            ghost_restart_later();
        } else
            rc = error(r, "400 Bad Request", "Unknown action");
    } else
        rc = error(r, "404 Not Found", "Unknown endpoint");
    memset(c, 0, sizeof(*c));
    free(c);
    cJSON_Delete(j);
    return rc;
}
static esp_err_t asset(httpd_req_t *r) {
    httpd_resp_set_hdr(r, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(r, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(r, "Content-Security-Policy",
                       "default-src 'self'; script-src 'self'; style-src 'self'; connect-src "
                       "'self'; frame-ancestors 'none'; base-uri 'none'");
    const uint8_t *p = html_start, *end = html_end;
    const char *type = "text/html; charset=utf-8";
    if (!strcmp(r->uri, "/style.css")) {
        p = css_start;
        end = css_end;
        type = "text/css";
    } else if (!strcmp(r->uri, "/app.js")) {
        p = js_start;
        end = js_end;
        type = "application/javascript";
    } else if (!strcmp(r->uri, "/logo.svg")) {
        p = logo_start;
        end = logo_end;
        type = "image/svg+xml";
    } else if (strcmp(r->uri, "/") && strcmp(r->uri, "/index.html")) {
        if (ghost_setup) {
            httpd_resp_set_status(r, "302 Found");
            httpd_resp_set_hdr(r, "Location", "http://192.168.4.1/");
            return httpd_resp_sendstr(r, "");
        }
        return error(r, "404 Not Found", "Not found");
    }
    httpd_resp_set_hdr(r, "Content-Encoding", "gzip");
    httpd_resp_set_type(r, type);
    return httpd_resp_send(r, (const char *)p, end - p);
}
void ghost_web_start(void) {
    ghost_random_hex(setup_csrf, 32);
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.task_priority = 2;
    cfg.core_id = 1;
    cfg.max_open_sockets = 4;
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout = 5;
    cfg.send_wait_timeout = 5;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    httpd_handle_t h;
    if (httpd_start(&h, &cfg) != ESP_OK) {
        ESP_LOGE("web", "HTTP server failed");
        return;
    }
    httpd_uri_t g = {.uri = "/api/*", .method = HTTP_GET, .handler = get},
                p = {.uri = "/api/*", .method = HTTP_POST, .handler = post},
                a = {.uri = "/*", .method = HTTP_GET, .handler = asset};
    httpd_register_uri_handler(h, &g);
    httpd_register_uri_handler(h, &p);
    httpd_register_uri_handler(h, &a);
}
