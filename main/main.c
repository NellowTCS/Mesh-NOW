#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>

#define TAG "MESH_NOW"
#define MAX_PEERS 20
#define MAC_ADDR_LEN 6
#define MAX_MSG_LEN 256
#define ESPNOW_CHANNEL 1
#define WIFI_SSID "MESH-NOW"
#define WIFI_PASS "password"
#define WIFI_CHANNEL 1
#define HTTP_PORT 80
#define UDP_PORT 12345

// Message structure for ESP-NOW
typedef struct {
    char message[MAX_MSG_LEN];
    uint8_t sender_mac[MAC_ADDR_LEN];
    uint32_t timestamp;
} message_t;

// Peer management
typedef struct {
    uint8_t peer_addr[ESP_NOW_ETH_ALEN];
    bool active;
} peer_info_t;

static peer_info_t peers[MAX_PEERS];
static int peer_count = 0;
static uint8_t broadcast_mac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static QueueHandle_t message_queue;
static httpd_handle_t server = NULL;

// Add peer if not already exists
void add_peer(const uint8_t *mac) {
    for (int i = 0; i < peer_count; i++) {
        if (memcmp(peers[i].peer_addr, mac, ESP_NOW_ETH_ALEN) == 0) {
            return; // Peer already exists
        }
    }
    
    if (peer_count < MAX_PEERS) {
        memcpy(peers[peer_count].peer_addr, mac, ESP_NOW_ETH_ALEN);
        peers[peer_count].active = true;
        
        esp_now_peer_info_t peer;
        memset(&peer, 0, sizeof(esp_now_peer_info_t));
        memcpy(peer.peer_addr, mac, ESP_NOW_ETH_ALEN);
        peer.channel = ESPNOW_CHANNEL;
        peer.encrypt = false;
        
        esp_now_add_peer(&peer);
        peer_count++;
        
        ESP_LOGI(TAG, "Added peer: %02x:%02x:%02x:%02x:%02x:%02x", 
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
}

// Remove peer
void remove_peer(const uint8_t *mac) {
    for (int i = 0; i < peer_count; i++) {
        if (memcmp(peers[i].peer_addr, mac, ESP_NOW_ETH_ALEN) == 0) {
            esp_now_del_peer(mac);
            
            // Shift remaining peers
            for (int j = i; j < peer_count - 1; j++) {
                peers[j] = peers[j + 1];
            }
            peer_count--;
            ESP_LOGI(TAG, "Removed peer: %02x:%02x:%02x:%02x:%02x:%02x",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            return;
        }
    }
}

// ESP-NOW send callback
static void esp_now_send_cb(const esp_now_send_info_t *send_info, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        ESP_LOGI(TAG, "Message sent successfully to %02x:%02x:%02x:%02x:%02x:%02x", 
                 send_info->dest_addr[0], send_info->dest_addr[1], send_info->dest_addr[2],
                 send_info->dest_addr[3], send_info->dest_addr[4], send_info->dest_addr[5]);
    } else {
        ESP_LOGW(TAG, "Failed to send message to %02x:%02x:%02x:%02x:%02x:%02x",
                 send_info->dest_addr[0], send_info->dest_addr[1], send_info->dest_addr[2],
                 send_info->dest_addr[3], send_info->dest_addr[4], send_info->dest_addr[5]);
    }
}

// ESP-NOW receive callback
static void esp_now_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    ESP_LOGI(TAG, "Received ESP-NOW message from %02x:%02x:%02x:%02x:%02x:%02x, len: %d", 
             recv_info->src_addr[0], recv_info->src_addr[1], recv_info->src_addr[2],
             recv_info->src_addr[3], recv_info->src_addr[4], recv_info->src_addr[5], len);
    
    if (len == sizeof(message_t)) {
        message_t msg;
        memcpy(&msg, data, sizeof(message_t));
        
        // Add sender as peer if not exists
        add_peer(recv_info->src_addr);
        
        // Queue message for web interface
        if (xQueueSend(message_queue, &msg, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Failed to queue message");
        }
    }
}

// HTTP server handlers
static esp_err_t index_handler(httpd_req_t *req) {
    const char* html = 
        "<!DOCTYPE html>"
        "<html><head><title>Mesh-NOW</title>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "<style>"
        "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background-color: #f0f0f0; }"
        ".container { max-width: 600px; margin: 0 auto; background-color: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }"
        "h1 { color: #333; text-align: center; }"
        "#messages { height: 300px; overflow-y: auto; border: 1px solid #ddd; padding: 10px; margin-bottom: 10px; background-color: #fafafa; border-radius: 5px; }"
        ".message { margin-bottom: 10px; padding: 8px; background-color: #e3f2fd; border-radius: 5px; }"
        ".message-sender { font-weight: bold; color: #1976d2; }"
        ".message-content { margin-top: 5px; }"
        ".input-group { display: flex; gap: 10px; }"
        "#messageInput { flex: 1; padding: 10px; border: 1px solid #ddd; border-radius: 5px; font-size: 16px; }"
        "#sendBtn { padding: 10px 20px; background-color: #1976d2; color: white; border: none; border-radius: 5px; cursor: pointer; font-size: 16px; }"
        "#sendBtn:hover { background-color: #1565c0; }"
        ".info { text-align: center; color: #666; margin-bottom: 20px; }"
        "</style>"
        "</head><body>"
        "<div class='container'>"
        "<h1>Mesh-NOW</h1>"
        "<div class='info'>Connect multiple ESP32 devices to create a mesh network</div>"
        "<div id='messages'></div>"
        "<div class='input-group'>"
        "<input type='text' id='messageInput' placeholder='Type your message...' maxlength='200'>"
        "<button id='sendBtn'>Send</button>"
        "</div>"
        "</div>"
        "<script>"
        "let messageContainer = document.getElementById('messages');"
        "let messageInput = document.getElementById('messageInput');"
        "let sendBtn = document.getElementById('sendBtn');"
        "function addMessage(sender, content) {"
        "    let messageDiv = document.createElement('div');"
        "    messageDiv.className = 'message';"
        "    messageDiv.innerHTML = '<div class=\"message-sender\">' + sender + '</div><div class=\"message-content\">' + content + '</div>';"
        "    messageContainer.appendChild(messageDiv);"
        "    messageContainer.scrollTop = messageContainer.scrollHeight;"
        "}"
        "function sendMessage() {"
        "    let message = messageInput.value.trim();"
        "    if (message === '') return;"
        "    fetch('/send', {"
        "        method: 'POST',"
        "        headers: {'Content-Type': 'application/x-www-form-urlencoded'},"
        "        body: 'message=' + encodeURIComponent(message)"
        "    }).then(response => {"
        "        if (response.ok) {"
        "            messageInput.value = '';"
        "            addMessage('You', message);"
        "        }"
        "    });"
        "}"
        "function pollMessages() {"
        "    fetch('/messages')"
        "    .then(response => response.json())"
        "    .then(data => {"
        "        if (data.messages && data.messages.length > 0) {"
        "            data.messages.forEach(msg => {"
        "                addMessage(msg.sender, msg.content);"
        "            });"
        "        }"
        "    })"
        "    .catch(err => console.log('Poll error:', err));"
        "}"
        "sendBtn.addEventListener('click', sendMessage);"
        "messageInput.addEventListener('keypress', function(e) {"
        "    if (e.key === 'Enter') sendMessage();"
        "});"
        "setInterval(pollMessages, 1000);"
        "addMessage('System', 'Connected to ESP32 Mesh Network');"
        "</script>"
        "</body></html>";
    
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

static esp_err_t send_handler(httpd_req_t *req) {
    char content[512];
    int content_len = httpd_req_recv(req, content, sizeof(content) - 1);
    
    if (content_len > 0) {
        content[content_len] = '\0';
        
        // Parse message from POST data
        char *msg_start = strstr(content, "message=");
        if (msg_start) {
            msg_start += 8; // Skip "message="
            char *msg_end = strchr(msg_start, '&');
            if (msg_end) *msg_end = '\0';
            
            // URL decode (basic)
            char decoded[MAX_MSG_LEN];
            int len = 0;
            for (int i = 0; msg_start[i] && len < MAX_MSG_LEN - 1; i++) {
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
            
            // Create message
            message_t msg;
            strncpy(msg.message, decoded, MAX_MSG_LEN - 1);
            msg.message[MAX_MSG_LEN - 1] = '\0';
            esp_read_mac(msg.sender_mac, ESP_MAC_WIFI_STA);
            msg.timestamp = esp_timer_get_time() / 1000; // Convert to ms
            
            // Send to all peers
            if (peer_count > 0) {
                for (int i = 0; i < peer_count; i++) {
                    esp_now_send(peers[i].peer_addr, (uint8_t *)&msg, sizeof(message_t));
                }
                ESP_LOGI(TAG, "Sent message to %d peers: %s", peer_count, decoded);
            } else {
                ESP_LOGI(TAG, "No peers available, message not sent: %s", decoded);
            }
        }
    }
    
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

static esp_err_t messages_handler(httpd_req_t *req) {
    char json_response[2048];
    strcpy(json_response, "{\"messages\":[");
    
    message_t msg;
    int msg_count = 0;
    bool first = true;
    
    while (xQueueReceive(message_queue, &msg, 0) == pdTRUE && msg_count < 10) {
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

static void start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = HTTP_PORT;
    config.stack_size = 8192;
    
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t index_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = index_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &index_uri);
        
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
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server");
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Station connected: %02x:%02x:%02x:%02x:%02x:%02x", 
                 event->mac[0], event->mac[1], event->mac[2], event->mac[3], event->mac[4], event->mac[5]);
        add_peer(event->mac);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Station disconnected: %02x:%02x:%02x:%02x:%02x:%02x",
                 event->mac[0], event->mac[1], event->mac[2], event->mac[3], event->mac[4], event->mac[5]);
        remove_peer(event->mac);
    }
}

static void wifi_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .password = WIFI_PASS,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .channel = WIFI_CHANNEL,
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
    
    ESP_LOGI(TAG, "WiFi AP started. SSID: %s, Password: %s, Channel: %d", 
             WIFI_SSID, WIFI_PASS, WIFI_CHANNEL);
}

void app_main(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Create message queue
    message_queue = xQueueCreate(50, sizeof(message_t));
    if (message_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create message queue");
        return;
    }
    
    // Initialize WiFi
    wifi_init();
    
    // Initialize ESP-NOW
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(esp_now_send_cb));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(esp_now_recv_cb));
    
    // Add broadcast peer
    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(esp_now_peer_info_t));
    memcpy(peer.peer_addr, broadcast_mac, ESP_NOW_ETH_ALEN);
    peer.channel = ESPNOW_CHANNEL;
    peer.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));
    
    // Start HTTP server
    start_webserver();
    
    // Print device MAC address
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "Device MAC: %02x:%02x:%02x:%02x:%02x:%02x", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    ESP_LOGI(TAG, "ESP-NOW Mesh Chat initialized. Connect to WiFi AP '%s' and open http://192.168.4.1", WIFI_SSID);
    
    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}