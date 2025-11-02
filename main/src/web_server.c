#include "web_server.h"
#include <esp_log.h>
#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string.h>
#include <inttypes.h>

// Embedded frontend files
#include "index_html.h"
#include "bundle_js.h"
#include "styles_css.h"

#define TAG "WEB_SERVER"
#define HTTP_PORT 80

static httpd_handle_t server = NULL;
static QueueHandle_t message_queue = NULL;
static message_send_callback_t send_callback = NULL;

// HTTP server handlers
static esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, INDEX_HTML, INDEX_HTML_size);
    return ESP_OK;
}

static esp_err_t js_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req, BUNDLE_JS, BUNDLE_JS_size);
    return ESP_OK;
}

static esp_err_t css_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, STYLES_CSS, STYLES_CSS_size);
    return ESP_OK;
}

static esp_err_t send_handler(httpd_req_t *req) {
    char content[512];
    int content_len = httpd_req_recv(req, content, sizeof(content) - 1);

    if (content_len > 0 && send_callback) {
        content[content_len] = '\0';

        // Parse message from POST data
        char *msg_start = strstr(content, "message=");
        if (msg_start) {
            msg_start += 8; // Skip "message="
            char *msg_end = strchr(msg_start, '&');
            if (msg_end) *msg_end = '\0';

            // URL decode (basic)
            char decoded[256];
            int len = 0;
            for (int i = 0; msg_start[i] && len < sizeof(decoded) - 1; i++) {
                if (msg_start[i] == '+') {
                    decoded[len++] = ' ';
                } else if (msg_start[i] == '%' && msg_start[i+1] && msg_start[i+2]) {
                    int hex;
                    sscanf(&msg_start[i+1], "%2x", &hex);
                    decoded[len++] = (char)hex;
                    i += 2;
                } else {
                    decoded[len++] = msg_start[i];
                }
            }
            decoded[len] = '\0';

            // Send message via callback
            send_callback(decoded);
        }
    }

    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

static esp_err_t messages_handler(httpd_req_t *req) {
    char json_response[2048];
    strcpy(json_response, "{\"messages\":[");

    // Message structure (should match mesh_now.h)
    typedef struct {
        char message[256];
        uint8_t sender_mac[6];
        uint32_t timestamp;
    } mesh_message_t;

    mesh_message_t msg;
    int msg_count = 0;
    bool first = true;

    while (message_queue && xQueueReceive(message_queue, &msg, 0) == pdTRUE && msg_count < 10) {
        if (!first) {
            strcat(json_response, ",");
        }

        char temp[512];
        snprintf(temp, sizeof(temp),
                "{\"sender\":\"%02x:%02x:%02x:%02x:%02x:%02x\",\"content\":\"%s\",\"timestamp\":%" PRIu32 "}",
                msg.sender_mac[0], msg.sender_mac[1], msg.sender_mac[2],
                msg.sender_mac[3], msg.sender_mac[4], msg.sender_mac[5],
                msg.message, msg.timestamp);

        strcat(json_response, temp);
        first = false;
        msg_count++;
    }

    strcat(json_response, "]}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_response, strlen(json_response));
    return ESP_OK;
}

esp_err_t web_server_init(QueueHandle_t queue) {
    message_queue = queue;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = HTTP_PORT;
    config.stack_size = 8192;

    if (httpd_start(&server, &config) == ESP_OK) {
        // Main page
        httpd_uri_t index_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = index_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &index_uri);

        // JavaScript bundle
        httpd_uri_t js_uri = {
            .uri = "/bundle.js",
            .method = HTTP_GET,
            .handler = js_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &js_uri);

        // CSS styles
        httpd_uri_t css_uri = {
            .uri = "/styles.css",
            .method = HTTP_GET,
            .handler = css_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &css_uri);

        // API endpoints
        httpd_uri_t send_uri = {
            .uri = "/send",
            .method = HTTP_POST,
            .handler = send_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &send_uri);

        httpd_uri_t messages_uri = {
            .uri = "/messages",
            .method = HTTP_GET,
            .handler = messages_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &messages_uri);

        ESP_LOGI(TAG, "HTTP server started successfully");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }
}

esp_err_t web_server_deinit(void) {
    if (server) {
        httpd_stop(server);
        server = NULL;
    }
    return ESP_OK;
}

void web_server_set_send_callback(message_send_callback_t callback) {
    send_callback = callback;
}


esp_err_t web_server_send_message(const char *message) {
    // This should be set by the main application
    // For now, just log the message
    ESP_LOGI(TAG, "Web server received message: %s", message);
    return ESP_OK;
}