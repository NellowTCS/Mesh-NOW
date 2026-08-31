#include "web_server.h"
#include "wifi_manager.h"
#include "mesh_now.h"
#include "message_queue.h"

#include <esp_log.h>
#include <esp_http_server.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string.h>
#include <inttypes.h>
#include <ctype.h>
#include <cJSON.h>

#include "index_html.h"
#include "bundle_js.h"
#include "styles_css.h"

#define TAG "WEB_SERVER"
#define HTTP_PORT 80

static httpd_handle_t server = NULL;
static QueueHandle_t message_queue = NULL;
static web_server_callbacks_t callbacks = {0};

static int url_decode(char *dst, size_t dst_size, const char *src)
{
    int out = 0;
    for (int i = 0; src[i] && out < (int)dst_size - 1; i++)
    {
        if (src[i] == '+')
        {
            dst[out++] = ' ';
        }
        else if (src[i] == '%' && src[i + 1] && src[i + 2])
        {
            unsigned int hex;
            if (sscanf(&src[i + 1], "%2x", &hex) == 1)
            {
                dst[out++] = (char)hex;
            }
            i += 2;
        }
        else
        {
            dst[out++] = src[i];
        }
    }
    dst[out] = '\0';
    return out;
}

static const char *form_get_value(const char *body, const char *key)
{
    size_t key_len = strlen(key);
    for (const char *p = body; *p; p++)
    {
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=')
        {
            return p + key_len + 1;
        }
        while (*p && *p != '&')
            p++;
    }
    return NULL;
}

static bool parse_mac(uint8_t *out, const char *str)
{
    unsigned int mac[6];
    if (sscanf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
               &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) != 6)
    {
        return false;
    }
    for (int i = 0; i < 6; i++)
        out[i] = (uint8_t)mac[i];
    return true;
}

static int read_body(httpd_req_t *req, char *buf, size_t buf_size)
{
    int total = 0;
    int remaining = req->content_len;
    while (remaining > 0 && total < (int)buf_size - 1)
    {
        int read = httpd_req_recv(req, buf + total,
                                   (remaining < (int)(buf_size - 1 - total))
                                       ? remaining
                                       : (buf_size - 1 - total));
        if (read <= 0)
            break;
        total += read;
        remaining -= read;
    }
    buf[total] = '\0';
    return total;
}

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, INDEX_HTML, INDEX_HTML_size);
    return ESP_OK;
}

static esp_err_t js_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req, BUNDLE_JS, BUNDLE_JS_size);
    return ESP_OK;
}

static esp_err_t css_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, STYLES_CSS, STYLES_CSS_size);
    return ESP_OK;
}

static esp_err_t send_handler(httpd_req_t *req)
{
    char body[512];
    read_body(req, body, sizeof(body));

    const char *raw = form_get_value(body, "message");
    if (!raw || !callbacks.send_broadcast)
    {
        httpd_resp_send(req, "{\"ok\":false}", 12);
        return ESP_OK;
    }

    char decoded[256];
    url_decode(decoded, sizeof(decoded), raw);
    callbacks.send_broadcast(decoded);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", 11);
    return ESP_OK;
}

static esp_err_t send_direct_handler(httpd_req_t *req)
{
    char body[512];
    read_body(req, body, sizeof(body));

    const char *raw_target = form_get_value(body, "target");
    const char *raw_msg = form_get_value(body, "message");
    if (!raw_target || !raw_msg || !callbacks.send_direct)
    {
        httpd_resp_send(req, "{\"ok\":false}", 12);
        return ESP_OK;
    }

    char target_decoded[32];
    url_decode(target_decoded, sizeof(target_decoded), raw_target);
    uint8_t target_mac[6];
    if (!parse_mac(target_mac, target_decoded))
    {
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"bad mac\"}", 28);
        return ESP_OK;
    }

    char msg_decoded[256];
    url_decode(msg_decoded, sizeof(msg_decoded), raw_msg);
    callbacks.send_direct(target_mac, msg_decoded);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", 11);
    return ESP_OK;
}

static esp_err_t send_group_handler(httpd_req_t *req)
{
    char body[512];
    read_body(req, body, sizeof(body));

    const char *raw_group = form_get_value(body, "group_id");
    const char *raw_msg = form_get_value(body, "message");
    if (!raw_group || !raw_msg || !callbacks.send_group)
    {
        httpd_resp_send(req, "{\"ok\":false}", 12);
        return ESP_OK;
    }

    uint8_t group_id = (uint8_t)atoi(raw_group);
    char msg_decoded[256];
    url_decode(msg_decoded, sizeof(msg_decoded), raw_msg);
    callbacks.send_group(group_id, msg_decoded);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", 11);
    return ESP_OK;
}

static esp_err_t typing_handler(httpd_req_t *req)
{
    char body[512];
    read_body(req, body, sizeof(body));

    const char *raw_target = form_get_value(body, "target");
    const char *raw_typing = form_get_value(body, "typing");
    if (!raw_target || !raw_typing || !callbacks.send_typing)
    {
        httpd_resp_send(req, "{\"ok\":false}", 12);
        return ESP_OK;
    }

    char target_decoded[32];
    url_decode(target_decoded, sizeof(target_decoded), raw_target);
    uint8_t target_mac[6];
    if (!parse_mac(target_mac, target_decoded))
    {
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"bad mac\"}", 28);
        return ESP_OK;
    }

    bool typing = (strcmp(raw_typing, "true") == 0);
    callbacks.send_typing(target_mac, typing);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", 11);
    return ESP_OK;
}

static esp_err_t name_handler(httpd_req_t *req)
{
    char body[512];
    read_body(req, body, sizeof(body));

    const char *raw_name = form_get_value(body, "name");
    if (!raw_name)
    {
        httpd_resp_send(req, "{\"ok\":false}", 12);
        return ESP_OK;
    }

    char decoded[17];
    url_decode(decoded, sizeof(decoded), raw_name);

    if (callbacks.set_name)
    {
        callbacks.set_name(decoded);
    }

    httpd_resp_set_type(req, "application/json");
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"name\":\"%s\"}", mesh_now_get_name());
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

static esp_err_t self_handler(httpd_req_t *req)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    cJSON *json = cJSON_CreateObject();
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str),
             "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cJSON_AddStringToObject(json, "mac", mac_str);

    const char *name = mesh_now_get_name();
    cJSON_AddStringToObject(json, "name", (name && name[0]) ? name : "unknown");

    char *str = cJSON_PrintUnformatted(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, str, strlen(str));
    cJSON_free(str);
    cJSON_Delete(json);
    return ESP_OK;
}

static esp_err_t messages_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "messages");

    message_t msg;
    int msg_count = 0;

    while (message_queue && xQueueReceive(message_queue, &msg, 0) == pdTRUE &&
           msg_count < 20)
    {
        cJSON *item = cJSON_CreateObject();

        char sender[18];
        snprintf(sender, sizeof(sender),
                 "%02x:%02x:%02x:%02x:%02x:%02x",
                 msg.sender_mac[0], msg.sender_mac[1], msg.sender_mac[2],
                 msg.sender_mac[3], msg.sender_mac[4], msg.sender_mac[5]);
        cJSON_AddStringToObject(item, "sender", sender);
        cJSON_AddStringToObject(item, "content", msg.message);
        cJSON_AddNumberToObject(item, "timestamp", msg.timestamp);
        cJSON_AddNumberToObject(item, "type", msg.type);
        cJSON_AddNumberToObject(item, "group_id", msg.group_id);

        char target[18];
        snprintf(target, sizeof(target),
                 "%02x:%02x:%02x:%02x:%02x:%02x",
                 msg.target_mac[0], msg.target_mac[1], msg.target_mac[2],
                 msg.target_mac[3], msg.target_mac[4], msg.target_mac[5]);
        cJSON_AddStringToObject(item, "target", target);

        cJSON_AddItemToArray(arr, item);
        msg_count++;
    }

    char *str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, str, strlen(str));
    cJSON_free(str);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t peers_handler(httpd_req_t *req)
{
    int count = mesh_now_get_peer_count();
    mesh_peer_t *peers = mesh_now_get_peers();

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "peers");

    for (int i = 0; i < count && i < MAX_PEERS; i++)
    {
        if (!peers[i].active)
            continue;

        cJSON *item = cJSON_CreateObject();

        char mac[18];
        snprintf(mac, sizeof(mac),
                 "%02x:%02x:%02x:%02x:%02x:%02x",
                 peers[i].peer_addr[0], peers[i].peer_addr[1],
                 peers[i].peer_addr[2], peers[i].peer_addr[3],
                 peers[i].peer_addr[4], peers[i].peer_addr[5]);
        cJSON_AddStringToObject(item, "mac", mac);

        if (peers[i].node_name[0] != '\0') {
            cJSON_AddStringToObject(item, "name", peers[i].node_name);
        }

        cJSON_AddBoolToObject(item, "online",
                              mesh_now_peer_is_online(&peers[i]));

        cJSON_AddItemToArray(arr, item);
    }

    char *str = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, str, strlen(str));
    cJSON_free(str);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t status_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "group_id", mesh_now_get_group_id());
    cJSON_AddBoolToObject(json, "encrypted", mesh_now_is_encrypted());

    char *str = cJSON_PrintUnformatted(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, str, strlen(str));
    cJSON_free(str);
    cJSON_Delete(json);
    return ESP_OK;
}

static esp_err_t wifi_info_handler(httpd_req_t *req)
{
    wifi_config_t wifi_config;
    esp_err_t err = esp_wifi_get_config(WIFI_IF_AP, &wifi_config);
    if (err != ESP_OK)
    {
        httpd_resp_send(req, "{\"error\":\"failed\"}", 18);
        return ESP_OK;
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "ssid",
                            (char *)wifi_config.ap.ssid);
    cJSON_AddNumberToObject(json, "channel", wifi_config.ap.channel);

    char *str = cJSON_PrintUnformatted(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, str, strlen(str));
    cJSON_free(str);
    cJSON_Delete(json);
    return ESP_OK;
}

static esp_err_t presence_handler(httpd_req_t *req)
{
    char body[512];
    read_body(req, body, sizeof(body));

    const char *raw_status = form_get_value(body, "status");
    if (!raw_status || !callbacks.send_presence)
    {
        httpd_resp_send(req, "{\"ok\":false}", 12);
        return ESP_OK;
    }

    char decoded[128];
    url_decode(decoded, sizeof(decoded), raw_status);
    callbacks.send_presence(decoded);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", 11);
    return ESP_OK;
}

static esp_err_t group_handler(httpd_req_t *req)
{
    char body[512];
    read_body(req, body, sizeof(body));

    const char *raw_group = form_get_value(body, "group_id");
    if (!raw_group || !callbacks.set_group)
    {
        httpd_resp_send(req, "{\"ok\":false}", 12);
        return ESP_OK;
    }

    uint8_t group_id = (uint8_t)atoi(raw_group);
    callbacks.set_group(group_id);

    httpd_resp_set_type(req, "application/json");
    char resp[48];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"group_id\":%u}", group_id);
    httpd_resp_send(req, resp, strlen(resp));
    return ESP_OK;
}

static esp_err_t encryption_handler(httpd_req_t *req)
{
    char body[512];
    read_body(req, body, sizeof(body));

    const char *raw_key = form_get_value(body, "key");
    if (!raw_key || !callbacks.set_encryption)
    {
        httpd_resp_send(req, "{\"ok\":false}", 12);
        return ESP_OK;
    }

    char decoded[64];
    url_decode(decoded, sizeof(decoded), raw_key);
    size_t key_len = strlen(decoded);
    if (key_len > 32) key_len = 32;
    callbacks.set_encryption((const uint8_t *)decoded, key_len);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", 11);
    return ESP_OK;
}

static esp_err_t favicon_handler(httpd_req_t *req)
{
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t web_server_init(QueueHandle_t queue, const web_server_callbacks_t *cbs)
{
    message_queue = queue;
    if (cbs)
        callbacks = *cbs;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = HTTP_PORT;
    config.max_uri_handlers = 20;
    config.stack_size = 8192;

    if (httpd_start(&server, &config) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }

    const httpd_uri_t uris[] = {
        {.uri = "/", .method = HTTP_GET, .handler = index_handler},
        {.uri = "/bundle.js", .method = HTTP_GET, .handler = js_handler},
        {.uri = "/styles.css", .method = HTTP_GET, .handler = css_handler},
        {.uri = "/send", .method = HTTP_POST, .handler = send_handler},
        {.uri = "/send/direct", .method = HTTP_POST, .handler = send_direct_handler},
        {.uri = "/send/group", .method = HTTP_POST, .handler = send_group_handler},
        {.uri = "/presence", .method = HTTP_POST, .handler = presence_handler},
        {.uri = "/typing", .method = HTTP_POST, .handler = typing_handler},
        {.uri = "/name", .method = HTTP_POST, .handler = name_handler},
        {.uri = "/group", .method = HTTP_POST, .handler = group_handler},
        {.uri = "/encryption", .method = HTTP_POST, .handler = encryption_handler},
        {.uri = "/self", .method = HTTP_GET, .handler = self_handler},
        {.uri = "/status", .method = HTTP_GET, .handler = status_handler},
        {.uri = "/messages", .method = HTTP_GET, .handler = messages_handler},
        {.uri = "/peers", .method = HTTP_GET, .handler = peers_handler},
        {.uri = "/wifi-info", .method = HTTP_GET, .handler = wifi_info_handler},
        {.uri = "/favicon.ico", .method = HTTP_GET, .handler = favicon_handler},
    };

    for (int i = 0; i < (int)(sizeof(uris) / sizeof(uris[0])); i++)
    {
        httpd_uri_t entry = uris[i];
        entry.user_ctx = NULL;
        httpd_register_uri_handler(server, &entry);
    }

    ESP_LOGI(TAG, "HTTP server started on port %d", HTTP_PORT);
    return ESP_OK;
}

esp_err_t web_server_deinit(void)
{
    if (server)
    {
        httpd_stop(server);
        server = NULL;
    }
    return ESP_OK;
}
