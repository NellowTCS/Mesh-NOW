#include "serial_api.h"
#include "mesh_now.h"
#include "message_queue.h"

#include <cJSON.h>
#if SOC_USB_SERIAL_JTAG_SUPPORTED
#include <driver/usb_serial_jtag.h>
#else
#include <driver/uart.h>
#include <driver/gpio.h>
#endif
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <freertos/semphr.h>
#include <stdlib.h>
#include <string.h>

#define TAG "SERIAL_API"

#define RX_BUF_SIZE            128
#define LINE_BUF_SIZE          1024
#define PEERS_PUSH_INTERVAL_MS 3000
#define ENCRYPTION_KEY_MAX     32

#if !SOC_USB_SERIAL_JTAG_SUPPORTED
// No USB-Serial/JTAG on classic ESP32/ESP32-S2: Web Serial rides UART0.
// Pins used only when we install our own UART0 driver instead of the console.
#define SERIAL_UART      UART_NUM_0
#define SERIAL_UART_BAUD 115200
#if CONFIG_IDF_TARGET_ESP32S2
#define SERIAL_UART_TX GPIO_NUM_43
#define SERIAL_UART_RX GPIO_NUM_44
#else
#define SERIAL_UART_TX GPIO_NUM_1
#define SERIAL_UART_RX GPIO_NUM_3
#endif
#endif

static QueueHandle_t incoming_queue = NULL;
static serial_api_callbacks_t callbacks = {0};
static SemaphoreHandle_t tx_lock = NULL;
static esp_err_t serial_io_init(void)
{
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    usb_serial_jtag_driver_config_t cfg = {
        .tx_buffer_size = 1024,
        .rx_buffer_size = 1024,
    };
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    // ESP_ERR_INVALID_STATE: console driver (ESP_CONSOLE_USB_SERIAL_JTAG)
    // owns the port; we read and write through that driver.
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "usb_serial_jtag_driver_install failed: %s",
                 esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
#else
    uart_config_t cfg = {
        .baud_rate = SERIAL_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err =
        uart_driver_install(SERIAL_UART, RX_BUF_SIZE, RX_BUF_SIZE, 0, NULL, 0);
    if (err == ESP_ERR_INVALID_STATE) {
        // Console driver owns the port; reconfiguring baud/pins would error.
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }
    err = uart_param_config(SERIAL_UART, &cfg);
    if (err != ESP_OK) {
        return err;
    }
    return uart_set_pin(SERIAL_UART, SERIAL_UART_TX, SERIAL_UART_RX,
                        UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
#endif
}

static int serial_io_read(char *buf, size_t len, TickType_t timeout)
{
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    return usb_serial_jtag_read_bytes((uint8_t *)buf, len, timeout);
#else
    return uart_read_bytes(SERIAL_UART, (uint8_t *)buf, len, timeout);
#endif
}

static void serial_io_write(const char *buf, size_t len, TickType_t timeout)
{
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    usb_serial_jtag_write_bytes(buf, len, timeout);
#else
    uart_write_bytes(SERIAL_UART, buf, len);
#endif
}

static char *mac_to_str(const uint8_t mac[6], char out[18])
{
    snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);
    return out;
}

static bool parse_mac(uint8_t out[6], const char *str)
{
    unsigned int mac[6];
    if (sscanf(str, "%02x:%02x:%02x:%02x:%02x:%02x", &mac[0], &mac[1], &mac[2],
               &mac[3], &mac[4], &mac[5]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        out[i] = (uint8_t)mac[i];
    }
    return true;
}

static void send_json(cJSON *root)
{
    if (!root) {
        return;
    }
    char *str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!str) {
        return;
    }

    size_t len = strlen(str);
    // Frame with a trailing newline so the browser can split on '\n'.
    char *frame = malloc(len + 2);
    if (frame) {
        memcpy(frame, str, len);
        frame[len] = '\n';
        frame[len + 1] = '\0';
        xSemaphoreTake(tx_lock, portMAX_DELAY);
        serial_io_write(frame, len + 1, pdMS_TO_TICKS(100));
        xSemaphoreGive(tx_lock);
        free(frame);
    }
    cJSON_free(str);
}

static void push_error(const char *text)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "event", "system");
    cJSON_AddStringToObject(root, "text", text);
    send_json(root);
}

static void push_hello(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "event", "hello");

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char mac_str[18];
    cJSON_AddStringToObject(root, "mac", mac_to_str(mac, mac_str));

    const char *name = mesh_now_get_name();
    cJSON_AddStringToObject(root, "name", (name && name[0]) ? name : "unknown");
    cJSON_AddNumberToObject(root, "group_id", mesh_now_get_group_id());
    cJSON_AddBoolToObject(root, "encrypted", mesh_now_is_encrypted());
    send_json(root);
}

static void push_peers(void)
{
    // Snapshot under the mutex; direct reads race the beacon expiry task.
    mesh_peer_t peers[MAX_PEERS];
    int count = mesh_now_snapshot_peers(peers, MAX_PEERS);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "event", "peers");
    cJSON *arr = cJSON_AddArrayToObject(root, "peers");

    for (int i = 0; i < count; i++) {
        if (!peers[i].active) {
            continue;
        }

        cJSON *item = cJSON_CreateObject();
        char mac[18];
        cJSON_AddStringToObject(item, "mac",
                                mac_to_str(peers[i].peer_addr, mac));
        if (peers[i].node_name[0] != '\0') {
            cJSON_AddStringToObject(item, "name", peers[i].node_name);
        }
        cJSON_AddBoolToObject(item, "online",
                              mesh_now_peer_is_online(&peers[i]));
        cJSON_AddItemToArray(arr, item);
    }

    send_json(root);
}

static void push_routes(void)
{
    // Virtual peers (beyond one hop) rendered so DMs can target the whole mesh.
    int count = mesh_now_get_route_count();
    if (count <= 0) {
        return;
    }
    mesh_route_t *routes = malloc((size_t)count * sizeof(mesh_route_t));
    if (!routes) {
        return;
    }
    count = mesh_now_snapshot_routes(routes, (size_t)count);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "event", "routes");
    cJSON *arr = cJSON_AddArrayToObject(root, "routes");

    for (int i = 0; i < count; i++) {
        if (!routes[i].active) {
            continue;
        }
        char mac[18];
        char via[18];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "mac",
                                mac_to_str(routes[i].dest_mac, mac));
        if (routes[i].node_name[0] != '\0') {
            cJSON_AddStringToObject(item, "name", routes[i].node_name);
        }
        cJSON_AddNumberToObject(item, "hops", routes[i].hop_count);
        cJSON_AddBoolToObject(item, "pinned", routes[i].pinned);
        cJSON_AddStringToObject(item, "via",
                                mac_to_str(routes[i].next_hop, via));
        cJSON_AddItemToArray(arr, item);
    }
    free(routes);

    send_json(root);
}

static void handle_route_failure(const uint8_t dest_mac[ESP_NOW_ETH_ALEN])
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "event", "route_failure");
    char mac[18];
    cJSON_AddStringToObject(root, "mac", mac_to_str(dest_mac, mac));
    send_json(root);
}

static void push_message(const message_t *msg)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "event", "message");

    char sender[18];
    cJSON_AddStringToObject(root, "sender",
                            mac_to_str(msg->sender_mac, sender));
    char target[18];
    cJSON_AddStringToObject(root, "target",
                            mac_to_str(msg->target_mac, target));
    cJSON_AddStringToObject(root, "content", msg->message);
    cJSON_AddNumberToObject(root, "timestamp", msg->timestamp);
    cJSON_AddNumberToObject(root, "type", msg->type);
    cJSON_AddNumberToObject(root, "group_id", msg->group_id);
    send_json(root);
}

static void handle_send(cJSON *root)
{
    const cJSON *msg_json = cJSON_GetObjectItemCaseSensitive(root, "message");
    if (!cJSON_IsString(msg_json) || !msg_json->valuestring) {
        return;
    }

    const cJSON *target_json = cJSON_GetObjectItemCaseSensitive(root, "target");
    if (cJSON_IsString(target_json) && target_json->valuestring &&
        target_json->valuestring[0]) {
        uint8_t target_mac[6];
        if (parse_mac(target_mac, target_json->valuestring) &&
            callbacks.send_direct) {
            callbacks.send_direct(target_mac, msg_json->valuestring);
            return;
        }
        push_error("Invalid direct target");
        return;
    }

    const cJSON *group_json = cJSON_GetObjectItemCaseSensitive(root, "group");
    if (cJSON_IsNumber(group_json) && group_json->valueint > 0) {
        if (callbacks.send_group) {
            callbacks.send_group((uint8_t)group_json->valueint,
                                 msg_json->valuestring);
        }
        return;
    }

    if (callbacks.send_broadcast) {
        callbacks.send_broadcast(msg_json->valuestring);
    }
}

static void handle_typing(cJSON *root)
{
    const cJSON *target_json = cJSON_GetObjectItemCaseSensitive(root, "target");
    const cJSON *typing_json = cJSON_GetObjectItemCaseSensitive(root, "typing");
    if (!cJSON_IsString(target_json) || !target_json->valuestring) {
        return;
    }
    uint8_t target_mac[6];
    if (!parse_mac(target_mac, target_json->valuestring) ||
        !callbacks.send_typing) {
        return;
    }
    callbacks.send_typing(target_mac, cJSON_IsTrue(typing_json));
}

static void handle_name(cJSON *root)
{
    const cJSON *name_json = cJSON_GetObjectItemCaseSensitive(root, "name");
    if (!cJSON_IsString(name_json) || !name_json->valuestring) {
        return;
    }
    if (callbacks.set_name) {
        callbacks.set_name(name_json->valuestring);
    }
}

static void handle_group(cJSON *root)
{
    const cJSON *id_json = cJSON_GetObjectItemCaseSensitive(root, "group_id");
    if (!cJSON_IsNumber(id_json) || !callbacks.set_group) {
        return;
    }
    callbacks.set_group((uint8_t)(id_json->valueint & 0xff));
}

static void handle_encryption(cJSON *root)
{
    const cJSON *key_json = cJSON_GetObjectItemCaseSensitive(root, "key");
    if (!cJSON_IsString(key_json) || !key_json->valuestring) {
        return;
    }
    size_t key_len = strlen(key_json->valuestring);
    if (key_len == 0) {
        return;
    }
    if (key_len > ENCRYPTION_KEY_MAX) {
        key_len = ENCRYPTION_KEY_MAX;
    }
    if (callbacks.set_encryption) {
        callbacks.set_encryption((const uint8_t *)key_json->valuestring,
                                 key_len);
    }
}

static void handle_line(const char *line)
{
    cJSON *root = cJSON_Parse(line);
    if (!root) {
        return;
    }

    const cJSON *cmd = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    if (!cJSON_IsString(cmd) || !cmd->valuestring) {
        cJSON_Delete(root);
        return;
    }

    if (strcmp(cmd->valuestring, "hello") == 0) {
        push_hello();
    } else if (strcmp(cmd->valuestring, "send") == 0) {
        handle_send(root);
    } else if (strcmp(cmd->valuestring, "typing") == 0) {
        handle_typing(root);
    } else if (strcmp(cmd->valuestring, "name") == 0) {
        handle_name(root);
    } else if (strcmp(cmd->valuestring, "group") == 0) {
        handle_group(root);
    } else if (strcmp(cmd->valuestring, "encryption") == 0) {
        handle_encryption(root);
    }

    cJSON_Delete(root);
}

static void serial_task(void *arg)
{
    char rx_buf[RX_BUF_SIZE];
    char line[LINE_BUF_SIZE];
    int line_len = 0;
    int64_t last_peers_us = 0;

    ESP_LOGI(TAG, "Serial API task started");

    while (1) {
        int n = serial_io_read(rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(50));
        for (int i = 0; i < n; i++) {
            char c = rx_buf[i];
            if (c == '\n') {
                if (line_len > 0) {
                    line[line_len] = '\0';
                    handle_line(line);
                }
                line_len = 0;
            } else if (c == '\r') {
                // Strip CR so bare-LF and CRLF hosts both work.
            } else if (line_len < (int)sizeof(line) - 1) {
                line[line_len++] = c;
            }
        }

        message_t msg;
        while (incoming_queue &&
               xQueueReceive(incoming_queue, &msg, 0) == pdTRUE) {
            push_message(&msg);
        }

        int64_t now_us = esp_timer_get_time();
        if (now_us - last_peers_us >= PEERS_PUSH_INTERVAL_MS * 1000) {
            last_peers_us = now_us;
            push_peers();
            push_routes();
        }
    }
}

esp_err_t serial_api_init(QueueHandle_t queue,
                          const serial_api_callbacks_t *callbacks_in)
{
    incoming_queue = queue;
    if (callbacks_in) {
        callbacks = *callbacks_in;
    }

    mesh_now_set_route_failure_callback(handle_route_failure);

    tx_lock = xSemaphoreCreateMutex();
    if (!tx_lock) {
        ESP_LOGE(TAG, "Failed to create tx lock");
        return ESP_FAIL;
    }

    esp_err_t err = serial_io_init();
    if (err != ESP_OK) {
        vSemaphoreDelete(tx_lock);
        tx_lock = NULL;
        return err;
    }

    BaseType_t task_ret =
        xTaskCreate(serial_task, "serial_api", 8192, NULL, 5, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create serial task");
        return ESP_FAIL;
    }

#if SOC_USB_SERIAL_JTAG_SUPPORTED
    ESP_LOGI(TAG, "Serial API ready (USB-Serial/JTAG, JSON lines)");
#else
    ESP_LOGI(TAG, "Serial API ready (UART%d @ %d, JSON lines)", SERIAL_UART,
             SERIAL_UART_BAUD);
#endif
    return ESP_OK;
}