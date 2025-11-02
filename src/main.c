#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/socket.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdio.h>

// ESP-IDF includes
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <esp_mac.h>
#include <nvs_flash.h>
#include <esp_netif.h>

LOG_MODULE_REGISTER(zephyr_now, LOG_LEVEL_INF);

// MAC address length constant
#define MAC_ADDR_LEN 6

// ESP-NOW configuration
#define ESPNOW_CHANNEL 1
#define ESPNOW_PMK "pmk1234567890123"
#define ESPNOW_LMK "lmk1234567890123"

// WiFi AP configuration
#define WIFI_SSID "ESP32-Chat"
#define WIFI_PASS "password"
#define WIFI_CHANNEL 1

// UDP broadcast configuration
#define UDP_PORT 8888
#define BROADCAST_ADDR "255.255.255.255"

// Message buffer
#define MAX_MSG_LEN 256
char rx_buffer[MAX_MSG_LEN];
char tx_buffer[MAX_MSG_LEN];

// Peer list for discovery
esp_now_peer_info_t peers[ESP_NOW_MAX_ENCRYPT_PEER_NUM];
int peer_count = 0;

// Message queue for dynamic updates
struct message_node {
    void *fifo_reserved;
    char msg[MAX_MSG_LEN];
    uint8_t mac[6];
};
K_FIFO_DEFINE(message_fifo);

// Thread stacks and definitions
#define UDP_THREAD_STACK_SIZE 4096
#define HTTP_THREAD_STACK_SIZE 4096
K_THREAD_STACK_DEFINE(udp_thread_stack, UDP_THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(http_thread_stack, HTTP_THREAD_STACK_SIZE);
struct k_thread udp_thread_data;
struct k_thread http_thread_data;
k_tid_t udp_thread_tid;
k_tid_t http_thread_tid;

// Function prototypes
void esp_now_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);
void esp_now_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status);
void udp_discovery_thread(void *p1, void *p2, void *p3);
void http_server_thread(void *p1, void *p2, void *p3);
void add_peer(const uint8_t *mac);
void remove_peer(const uint8_t *mac);

int main(void) {
    LOG_INF("Zephyr-NOW ESP Mesh Chat Starting...");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize network interface
    ESP_ERROR_CHECK(esp_netif_init());
    
    // Initialize WiFi
    esp_netif_t *wifi_ap = esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    
    // Configure WiFi AP
    wifi_config_t ap_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .password = WIFI_PASS,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .channel = WIFI_CHANNEL,
        }
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

    // Initialize ESP-NOW
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_set_pmk((uint8_t *)ESPNOW_PMK));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(esp_now_recv_cb));
    ESP_ERROR_CHECK(esp_now_register_send_cb(esp_now_send_cb));

    LOG_INF("WiFi AP started: SSID=%s, Channel=%d", WIFI_SSID, WIFI_CHANNEL);
    LOG_INF("ESP-NOW initialized");

    // Start UDP discovery thread
    udp_thread_tid = k_thread_create(&udp_thread_data, udp_thread_stack,
                                     K_THREAD_STACK_SIZEOF(udp_thread_stack),
                                     udp_discovery_thread,
                                     NULL, NULL, NULL,
                                     5, 0, K_NO_WAIT);
    k_thread_name_set(udp_thread_tid, "udp_discovery");

    // Start HTTP server thread
    http_thread_tid = k_thread_create(&http_thread_data, http_thread_stack,
                                      K_THREAD_STACK_SIZEOF(http_thread_stack),
                                      http_server_thread,
                                      NULL, NULL, NULL,
                                      5, 0, K_NO_WAIT);
    k_thread_name_set(http_thread_tid, "http_server");

    LOG_INF("Zephyr-NOW initialized successfully");
    LOG_INF("Connect to WiFi: %s / %s", WIFI_SSID, WIFI_PASS);
    LOG_INF("Then browse to: http://192.168.4.1");

    // Main loop - process messages from queue
    while (1) {
        struct message_node *msg = k_fifo_get(&message_fifo, K_FOREVER);
        if (msg) {
            LOG_INF("Message from %02x:%02x:%02x:%02x:%02x:%02x: %s",
                   msg->mac[0], msg->mac[1], msg->mac[2], 
                   msg->mac[3], msg->mac[4], msg->mac[5], msg->msg);
            k_free(msg);
        }
    }

    return 0;
}

// ESP-NOW receive callback
void esp_now_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    if (len > MAX_MSG_LEN - 1) len = MAX_MSG_LEN - 1;
    
    // Add sender as peer if not already known
    add_peer(recv_info->src_addr);
    
    // Allocate message node and add to queue
    struct message_node *msg_node = k_malloc(sizeof(struct message_node));
    if (msg_node) {
        memcpy(msg_node->msg, data, len);
        msg_node->msg[len] = '\0';
        memcpy(msg_node->mac, recv_info->src_addr, MAC_ADDR_LEN);
        k_fifo_put(&message_fifo, msg_node);
        
        LOG_INF("Received ESP-NOW message from %02x:%02x:%02x:%02x:%02x:%02x",
               recv_info->src_addr[0], recv_info->src_addr[1], recv_info->src_addr[2],
               recv_info->src_addr[3], recv_info->src_addr[4], recv_info->src_addr[5]);
    }
}

// ESP-NOW send callback
void esp_now_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status) {
    LOG_INF("ESP-NOW send status: %s", status == ESP_NOW_SEND_SUCCESS ? "SUCCESS" : "FAIL");
}

// Add peer dynamically
void add_peer(const uint8_t *mac) {
    // Check if peer already exists
    for (int i = 0; i < peer_count; i++) {
        if (memcmp(peers[i].peer_addr, mac, MAC_ADDR_LEN) == 0) {
            return; // Already exists
        }
    }
    
    // Add new peer
    if (peer_count < ESP_NOW_MAX_ENCRYPT_PEER_NUM) {
        memset(&peers[peer_count], 0, sizeof(esp_now_peer_info_t));
        memcpy(peers[peer_count].peer_addr, mac, MAC_ADDR_LEN);
        peers[peer_count].channel = ESPNOW_CHANNEL;
        peers[peer_count].ifidx = WIFI_IF_AP;
        peers[peer_count].encrypt = false;
        
        esp_err_t ret = esp_now_add_peer(&peers[peer_count]);
        if (ret == ESP_OK) {
            peer_count++;
            LOG_INF("Added peer: %02x:%02x:%02x:%02x:%02x:%02x", 
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        } else {
            LOG_ERR("Failed to add peer: %d", ret);
        }
    }
}

// Remove peer (if needed, e.g., on timeout)
void remove_peer(const uint8_t *mac) {
    for (int i = 0; i < peer_count; i++) {
        if (memcmp(peers[i].peer_addr, mac, MAC_ADDR_LEN) == 0) {
            esp_now_del_peer(mac);
            memmove(&peers[i], &peers[i+1], (peer_count - i - 1) * sizeof(esp_now_peer_info_t));
            peer_count--;
            LOG_INF("Removed peer: %02x:%02x:%02x:%02x:%02x:%02x", 
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            break;
        }
    }
}

// UDP discovery thread
void udp_discovery_thread(void *p1, void *p2, void *p3) {
    int sock;
    struct sockaddr_in broadcast_addr;
    struct sockaddr_in recv_addr;
    socklen_t addr_len;
    int recv_len;
    uint8_t mac[6];
    char discovery_msg[64];
    int broadcast_opt = 1;
    
    // Wait for network to be ready
    k_sleep(K_SECONDS(2));
    
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        LOG_ERR("Failed to create UDP socket: %d", errno);
        return;
    }

    // Enable broadcast
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast_opt, sizeof(broadcast_opt)) < 0) {
        LOG_ERR("Failed to set broadcast option: %d", errno);
        close(sock);
        return;
    }

    // Bind to port for receiving
    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port = htons(UDP_PORT);
    
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        LOG_ERR("Failed to bind UDP socket: %d", errno);
        close(sock);
        return;
    }

    // Setup broadcast address
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(UDP_PORT);
    broadcast_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    // Get own MAC for broadcasting
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(discovery_msg, sizeof(discovery_msg), 
            "ZEPHYR-NOW:%02x%02x%02x%02x%02x%02x", 
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    LOG_INF("UDP discovery started on port %d", UDP_PORT);

    while (1) {
        // Broadcast discovery message
        int sent = sendto(sock, discovery_msg, strlen(discovery_msg), 0,
                         (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
        if (sent < 0) {
            LOG_ERR("Failed to send broadcast: %d", errno);
        }

        // Try to receive discovery messages (non-blocking with timeout)
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 500000; // 500ms timeout
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        
        addr_len = sizeof(recv_addr);
        recv_len = recvfrom(sock, rx_buffer, MAX_MSG_LEN - 1, 0,
                           (struct sockaddr *)&recv_addr, &addr_len);
        
        if (recv_len > 0) {
            rx_buffer[recv_len] = '\0';
            if (strncmp(rx_buffer, "ZEPHYR-NOW:", 11) == 0) {
                // Parse MAC from message
                uint8_t peer_mac[6];
                if (sscanf(rx_buffer + 11, "%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx",
                          &peer_mac[0], &peer_mac[1], &peer_mac[2],
                          &peer_mac[3], &peer_mac[4], &peer_mac[5]) == 6) {
                    // Don't add ourselves
                    if (memcmp(peer_mac, mac, MAC_ADDR_LEN) != 0) {
                        add_peer(peer_mac);
                    }
                }
            }
        }
        
        // Wait before next broadcast
        k_sleep(K_SECONDS(5));
    }
    
    close(sock);
}

// Simple HTTP server thread
void http_server_thread(void *p1, void *p2, void *p3) {
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len;
    char http_buffer[2048];
    
    // Wait for network to be ready
    k_sleep(K_SECONDS(2));
    
    server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_sock < 0) {
        LOG_ERR("Failed to create TCP socket: %d", errno);
        return;
    }

    // Allow address reuse
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind to port 80
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(80);
    
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        LOG_ERR("Failed to bind HTTP server: %d", errno);
        close(server_sock);
        return;
    }

    if (listen(server_sock, 2) < 0) {
        LOG_ERR("Failed to listen on HTTP server: %d", errno);
        close(server_sock);
        return;
    }

    LOG_INF("HTTP server started on port 80");

    while (1) {
        client_addr_len = sizeof(client_addr);
        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_len);
        
        if (client_sock < 0) {
            LOG_ERR("Failed to accept connection: %d", errno);
            continue;
        }

        // Receive HTTP request
        int recv_len = recv(client_sock, http_buffer, sizeof(http_buffer) - 1, 0);
        if (recv_len <= 0) {
            close(client_sock);
            continue;
        }
        http_buffer[recv_len] = '\0';

        // Parse request method and path
        char method[16] = {0};
        char path[128] = {0};
        sscanf(http_buffer, "%s %s", method, path);

        LOG_INF("HTTP %s %s", method, path);

        if (strcmp(method, "GET") == 0 && strcmp(path, "/") == 0) {
            // Serve main HTML page
            const char *html_header = "HTTP/1.1 200 OK\r\n"
                                     "Content-Type: text/html\r\n"
                                     "Connection: close\r\n\r\n";
            const char *html_body = 
                "<!DOCTYPE html><html><head><title>Zephyr-NOW</title>"
                "<style>body{font-family:Arial;max-width:800px;margin:50px auto;padding:20px;}"
                "#messages{border:1px solid #ccc;height:300px;overflow-y:auto;padding:10px;margin:20px 0;}"
                ".msg{margin:5px 0;padding:5px;background:#f0f0f0;border-radius:3px;}"
                "input[type=text]{width:70%;padding:10px;}"
                "button{padding:10px 20px;background:#007bff;color:white;border:none;cursor:pointer;}"
                "button:hover{background:#0056b3;}</style></head><body>"
                "<h1>Zephyr-NOW Mesh Chat</h1>"
                "<div id='messages'></div>"
                "<input type='text' id='msg' placeholder='Type a message...'>"
                "<button onclick='send()'>Send</button>"
                "<script>"
                "function send(){"
                "var msg=document.getElementById('msg').value;"
                "if(msg.trim()){"
                "fetch('/send',{method:'POST',body:'msg='+encodeURIComponent(msg)})"
                ".then(()=>{document.getElementById('msg').value='';});"
                "}}"
                "setInterval(()=>{"
                "fetch('/messages').then(r=>r.text()).then(d=>{"
                "if(d)document.getElementById('messages').innerHTML=d;"
                "});},2000);"
                "</script></body></html>";
            
            send(client_sock, html_header, strlen(html_header), 0);
            send(client_sock, html_body, strlen(html_body), 0);
            
        } else if (strcmp(method, "POST") == 0 && strcmp(path, "/send") == 0) {
            // Extract message from POST body
            char *body = strstr(http_buffer, "\r\n\r\n");
            if (body) {
                body += 4; // Skip past \r\n\r\n
                char *msg_start = strstr(body, "msg=");
                if (msg_start) {
                    msg_start += 4;
                    char *msg_end = strchr(msg_start, '&');
                    int msg_len = msg_end ? (msg_end - msg_start) : strlen(msg_start);
                    if (msg_len > 0 && msg_len < MAX_MSG_LEN) {
                        memcpy(tx_buffer, msg_start, msg_len);
                        tx_buffer[msg_len] = '\0';
                        
                        // URL decode (simple, just handle spaces)
                        for (int i = 0; i < msg_len; i++) {
                            if (tx_buffer[i] == '+') tx_buffer[i] = ' ';
                        }
                        
                        LOG_INF("Sending message via ESP-NOW: %s", tx_buffer);
                        
                        // Broadcast to all peers
                        for (int i = 0; i < peer_count; i++) {
                            esp_now_send(peers[i].peer_addr, (uint8_t *)tx_buffer, strlen(tx_buffer));
                        }
                        
                        // Also broadcast to all
                        uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
                        esp_now_send(broadcast_mac, (uint8_t *)tx_buffer, strlen(tx_buffer));
                    }
                }
            }
            
            const char *response = "HTTP/1.1 200 OK\r\n"
                                  "Content-Type: text/plain\r\n"
                                  "Connection: close\r\n\r\nOK";
            send(client_sock, response, strlen(response), 0);
            
        } else if (strcmp(method, "GET") == 0 && strcmp(path, "/messages") == 0) {
            // Return recent messages (simplified - in production, maintain a message history)
            const char *header = "HTTP/1.1 200 OK\r\n"
                                "Content-Type: text/html\r\n"
                                "Connection: close\r\n\r\n";
            send(client_sock, header, strlen(header), 0);
            // For now, just return empty - proper implementation would maintain message history
        } else {
            // 404 Not Found
            const char *response = "HTTP/1.1 404 Not Found\r\n"
                                  "Content-Type: text/plain\r\n"
                                  "Connection: close\r\n\r\n404 Not Found";
            send(client_sock, response, strlen(response), 0);
        }

        close(client_sock);
    }
    
    close(server_sock);
}