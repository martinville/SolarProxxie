#include "app.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include <stdlib.h>
#include <string.h>

#define RELEASE_API "https://api.github.com/repos/martinville/SolarProxxie/releases/latest"
#define RELEASE_PREFIX "https://github.com/martinville/SolarProxxie/releases/download/"
#define RELEASE_ASSET "SolarProxxie.bin"
#define RELEASE_RESPONSE_MAX 24576

static const char *TAG = "ota";
static atomic_bool ota_busy;
static portMUX_TYPE update_lock = portMUX_INITIALIZER_UNLOCKED;
static char latest_version[32], latest_url[512], update_message[128] = "Not checked";
static size_t latest_size;
static bool latest_available;

typedef struct {
    char *data;
    size_t used, capacity;
    bool overflow;
} response_t;

typedef struct {
    char version[32];
    char url[512];
    size_t size;
} update_job_t;

static bool version_parts(const char *text, unsigned parts[3]) {
    if (!text)
        return false;
    if (*text == 'v' || *text == 'V')
        text++;
    for (unsigned i = 0; i < 3; i++) {
        char *end = NULL;
        unsigned long value = strtoul(text, &end, 10);
        if (end == text || value > 999999)
            return false;
        parts[i] = (unsigned)value;
        if (i < 2) {
            if (*end != '.')
                return false;
            text = end + 1;
        } else if (*end && *end != '-' && *end != '+')
            return false;
    }
    return true;
}

static int version_compare(const char *left, const char *right) {
    unsigned a[3], b[3];
    if (!version_parts(left, a) || !version_parts(right, b))
        return 0;
    for (unsigned i = 0; i < 3; i++)
        if (a[i] != b[i])
            return a[i] > b[i] ? 1 : -1;
    return 0;
}

static void update_state(const char *message, bool available) {
    portENTER_CRITICAL(&update_lock);
    strlcpy(update_message, message, sizeof(update_message));
    latest_available = available;
    portEXIT_CRITICAL(&update_lock);
}

static esp_err_t collect_response(esp_http_client_event_t *event) {
    response_t *response = event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || !event->data_len)
        return ESP_OK;
    if (!response || response->used + (size_t)event->data_len >= response->capacity) {
        if (response)
            response->overflow = true;
        return ESP_FAIL;
    }
    memcpy(response->data + response->used, event->data, event->data_len);
    response->used += (size_t)event->data_len;
    response->data[response->used] = 0;
    return ESP_OK;
}

static bool copy_json_string(const cJSON *object, const char *key, char *out, size_t capacity) {
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(value) || !value->valuestring || strlen(value->valuestring) >= capacity)
        return false;
    strlcpy(out, value->valuestring, capacity);
    return true;
}

esp_err_t ghost_cloud_update_check(char *error_text, size_t error_capacity) {
    if (atomic_load(&ota_busy)) {
        snprintf(error_text, error_capacity, "An update is already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    response_t response = {.data = malloc(RELEASE_RESPONSE_MAX),
                           .capacity = RELEASE_RESPONSE_MAX};
    if (!response.data) {
        snprintf(error_text, error_capacity, "Not enough memory to check GitHub");
        return ESP_ERR_NO_MEM;
    }
    response.data[0] = 0;
    esp_http_client_config_t config = {
        .url = RELEASE_API,
        .event_handler = collect_response,
        .user_data = &response,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 12000,
        .keep_alive_enable = true,
        .buffer_size = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t result = client ? ESP_OK : ESP_ERR_NO_MEM;
    if (client) {
        esp_http_client_set_header(client, "Accept", "application/vnd.github+json");
        esp_http_client_set_header(client, "X-GitHub-Api-Version", "2022-11-28");
        esp_http_client_set_header(client, "User-Agent", "SolarProxxie-ESP32");
        result = esp_http_client_perform(client);
    }
    int status = client ? esp_http_client_get_status_code(client) : 0;
    if (client)
        esp_http_client_cleanup(client);
    if (result != ESP_OK || response.overflow || status != 200) {
        if (status == 404)
            snprintf(error_text, error_capacity, "No published GitHub release is available");
        else
            snprintf(error_text, error_capacity, "GitHub release check failed (HTTP %d, %s)", status,
                     esp_err_to_name(result));
        free(response.data);
        update_state(error_text, false);
        return result == ESP_OK ? ESP_FAIL : result;
    }

    cJSON *release = cJSON_ParseWithLength(response.data, response.used);
    free(response.data);
    if (!release) {
        snprintf(error_text, error_capacity, "GitHub returned invalid release metadata");
        update_state(error_text, false);
        return ESP_ERR_INVALID_RESPONSE;
    }
    char version[32] = {0}, url[512] = {0};
    size_t size = 0;
    unsigned parsed[3];
    bool valid = copy_json_string(release, "tag_name", version, sizeof(version)) &&
                 version_parts(version, parsed);
    if (version[0] == 'v' || version[0] == 'V')
        memmove(version, version + 1, strlen(version));
    const cJSON *assets = cJSON_GetObjectItemCaseSensitive(release, "assets");
    const cJSON *asset = NULL;
    cJSON_ArrayForEach(asset, assets) {
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(asset, "name");
        if (cJSON_IsString(name) && !strcmp(name->valuestring, RELEASE_ASSET)) {
            const cJSON *bytes = cJSON_GetObjectItemCaseSensitive(asset, "size");
            valid = valid && copy_json_string(asset, "browser_download_url", url, sizeof(url)) &&
                    cJSON_IsNumber(bytes) && bytes->valuedouble > 0;
            if (cJSON_IsNumber(bytes))
                size = (size_t)bytes->valuedouble;
            break;
        }
    }
    cJSON_Delete(release);
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    valid = valid && url[0] && !strncmp(url, RELEASE_PREFIX, strlen(RELEASE_PREFIX)) && partition &&
            size <= partition->size;
    if (!valid) {
        snprintf(error_text, error_capacity,
                 "Latest release needs a valid SolarProxxie.bin asset for the original ESP32");
        update_state(error_text, false);
        return ESP_ERR_INVALID_RESPONSE;
    }

    const char *current = esp_app_get_description()->version;
    bool available = version_compare(version, current) > 0;
    portENTER_CRITICAL(&update_lock);
    strlcpy(latest_version, version, sizeof(latest_version));
    strlcpy(latest_url, url, sizeof(latest_url));
    latest_size = size;
    latest_available = available;
    snprintf(update_message, sizeof(update_message), available ? "Update available" : "Firmware is current");
    portEXIT_CRITICAL(&update_lock);
    return ESP_OK;
}

cJSON *ghost_cloud_update_status(void) {
    char version[32], message[128];
    size_t size;
    bool available;
    portENTER_CRITICAL(&update_lock);
    strlcpy(version, latest_version, sizeof(version));
    strlcpy(message, update_message, sizeof(message));
    size = latest_size;
    available = latest_available;
    portEXIT_CRITICAL(&update_lock);
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "current", esp_app_get_description()->version);
    cJSON_AddStringToObject(json, "latest", version);
    cJSON_AddBoolToObject(json, "available", available);
    cJSON_AddBoolToObject(json, "busy", atomic_load(&ota_busy));
    cJSON_AddNumberToObject(json, "bytes", size);
    cJSON_AddStringToObject(json, "status", message);
    cJSON_AddStringToObject(json, "repository", "martinville/SolarProxxie");
    return json;
}

static void cloud_update_task(void *context) {
    update_job_t *job = context;
    update_state("Connecting to GitHub release", true);
    esp_http_client_config_t http = {
        .url = job->url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .keep_alive_enable = true,
        .buffer_size = 4096,
        .buffer_size_tx = 1024,
        .max_redirection_count = 5,
    };
    esp_https_ota_config_t ota = {
        .http_config = &http,
        .partial_http_download = true,
        .max_http_request_size = 4096,
    };
    esp_https_ota_handle_t handle = NULL;
    esp_err_t result = esp_https_ota_begin(&ota, &handle);
    if (result == ESP_OK) {
        esp_app_desc_t image = {0};
        result = esp_https_ota_get_img_desc(handle, &image);
        const esp_app_desc_t *current = esp_app_get_description();
        if (result == ESP_OK &&
            (strcmp(image.project_name, current->project_name) || strcmp(image.version, job->version))) {
            ESP_LOGE(TAG, "Release asset identity mismatch: project=%s version=%s", image.project_name,
                     image.version);
            result = ESP_ERR_INVALID_VERSION;
        }
    }
    while (result == ESP_OK || result == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        update_state("Downloading and writing firmware", true);
        result = esp_https_ota_perform(handle);
        if (result != ESP_ERR_HTTPS_OTA_IN_PROGRESS)
            break;
    }
    if (result == ESP_OK && !esp_https_ota_is_complete_data_received(handle))
        result = ESP_ERR_INVALID_SIZE;
    if (result == ESP_OK)
        result = esp_https_ota_finish(handle);
    else if (handle)
        esp_https_ota_abort(handle);

    if (result == ESP_OK) {
        update_state("Update verified; restarting", false);
        ESP_LOGI(TAG, "Cloud update to %s complete", job->version);
        free(job);
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }
    char message[128];
    snprintf(message, sizeof(message), "Cloud update failed: %s", esp_err_to_name(result));
    ESP_LOGE(TAG, "%s", message);
    update_state(message, true);
    atomic_store(&ota_busy, false);
    memset(job, 0, sizeof(*job));
    free(job);
    vTaskDelete(NULL);
}

esp_err_t ghost_cloud_update_start(const char *confirmed_version, char *error_text,
                                   size_t error_capacity) {
    update_job_t *job = calloc(1, sizeof(*job));
    if (!job) {
        snprintf(error_text, error_capacity, "Not enough memory to start update");
        return ESP_ERR_NO_MEM;
    }
    portENTER_CRITICAL(&update_lock);
    bool valid = latest_available && confirmed_version &&
                 !strcmp(confirmed_version, latest_version) && latest_url[0] && latest_size;
    if (valid) {
        strlcpy(job->version, latest_version, sizeof(job->version));
        strlcpy(job->url, latest_url, sizeof(job->url));
        job->size = latest_size;
    }
    portEXIT_CRITICAL(&update_lock);
    if (!valid) {
        free(job);
        snprintf(error_text, error_capacity, "Check GitHub again before installing this release");
        return ESP_ERR_INVALID_STATE;
    }
    bool expected = false;
    if (!atomic_compare_exchange_strong(&ota_busy, &expected, true)) {
        free(job);
        snprintf(error_text, error_capacity, "Another firmware update is already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    if (xTaskCreate(cloud_update_task, "cloud_ota", 8192, job, 3, NULL) != pdPASS) {
        atomic_store(&ota_busy, false);
        free(job);
        snprintf(error_text, error_capacity, "Could not start the update task");
        return ESP_ERR_NO_MEM;
    }
    update_state("Update scheduled", true);
    return ESP_OK;
}

esp_err_t ghost_ota_upload(httpd_req_t *r) {
    bool expected = false;
    if (!atomic_compare_exchange_strong(&ota_busy, &expected, true)) {
        httpd_resp_set_status(r, "409 Conflict");
        return httpd_resp_sendstr(r, "Another update is in progress");
    }
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part || r->content_len < sizeof(esp_image_header_t) || r->content_len > part->size) {
        atomic_store(&ota_busy, false);
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Invalid firmware size");
    }
    esp_ota_handle_t handle;
    esp_err_t rc = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &handle);
    if (rc != ESP_OK) {
        atomic_store(&ota_busy, false);
        return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA unavailable");
    }
    char buf[2048];
    size_t left = r->content_len;
    unsigned timeouts = 0;
    while (left) {
        int n = httpd_req_recv(r, buf, left > sizeof(buf) ? sizeof(buf) : left);
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= 3)
            continue;
        if (n <= 0 || esp_ota_write(handle, buf, n) != ESP_OK) {
            esp_ota_abort(handle);
            atomic_store(&ota_busy, false);
            return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST,
                                       "Upload interrupted or invalid image");
        }
        left -= n;
        timeouts = 0;
    }
    if (esp_ota_end(handle) != ESP_OK) {
        atomic_store(&ota_busy, false);
        return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Image validation failed");
    }
    if (esp_ota_set_boot_partition(part) != ESP_OK) {
        atomic_store(&ota_busy, false);
        return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to select image");
    }
    httpd_resp_set_type(r, "application/json");
    httpd_resp_sendstr(r, "{\"ok\":true,\"restart\":true}");
    ghost_restart_later();
    return ESP_OK;
}
