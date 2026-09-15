# Mesh-NOW

Lightweight mesh networking protocol library for ESP32 using ESP-NOW.

Device-to-device communication over ESP-NOW with automatic peer discovery,
multi-hop routing, message queuing, and optional payload encryption. Runs on
ESP32, ESP32-S2, ESP32-S3, ESP32-C3, and ESP32-C6 with ESP-IDF v4.4 or newer.

## Features

- Serverless mesh with no router or gateway required
- Auto-discovery of peers via periodic beacons
- Multi-hop routing with configurable TTL
- 7 message types: beacon, chat, direct, ACK, group, presence, typing
- ACK-based retransmission for direct messages (3 retries, 2s timeout)
- Group messaging with configurable group IDs
- Optional AES-128-GCM payload encryption (16-byte key)
- FreeRTOS integration with a message queue and pinned tasks

## Usage

Add the dependency to your project's `idf_component.yml`:

```yaml
dependencies:
  nellowtcs/mesh_now: "^1.0.1"
```

Initialize the mesh and register a receive callback:

```c
#include "mesh_now.h"

static void on_message(const mesh_message_t *msg)
{
    if (msg->type == MSG_TYPE_CHAT) {
        ESP_LOGI("APP", "%s: %s", msg->peer_name, msg->message);
    }
}

void app_main(void)
{
    mesh_now_set_name("node-01");
    mesh_now_set_receive_callback(on_message);
    ESP_ERROR_CHECK(mesh_now_init());
    while (1) {
        const mesh_message_t *msg;
        if (message_queue_get(&msg, pdMS_TO_TICKS(1000)) == pdTRUE) {
            // Handle the queued message on the app task.
        }
    }
}
```

## Sending

```c
// Broadcast to all reachable nodes.
esp_err_t mesh_now_send_broadcast(const char *message);

// Direct message with ACK-based retry.
esp_err_t mesh_now_send_direct(const uint8_t *target_mac, const char *message);

// Broadcast limited to a group.
esp_err_t mesh_now_send_group(uint8_t group_id, const char *message);

// Typing and presence indicators.
esp_err_t mesh_now_send_typing(const uint8_t *target_mac, bool typing);
esp_err_t mesh_now_send_presence(const char *status);
```

## Optional encryption

Set a 16-byte AES-128-GCM key before calling `mesh_now_init()`. Nodes must
share the same key.

```c
const uint8_t key[16] = { ... };
mesh_now_set_encryption_key(key);
```

## Configuration

Component settings live in `Kconfig`, under `Component config -> ESP-NOW mesh`:

- Default route TTL (hop limit) for outgoing frames
- Beacon interval and neighbor advertisement count
- Message queue depth and task stack sizes

## API

Full reference in `include/mesh_now.h`. Key entry points:

| Function                                                   | Description                      |
| ---------------------------------------------------------- | -------------------------------- |
| `mesh_now_init()` / `mesh_now_deinit()`                    | Start/stop the mesh              |
| `mesh_now_set_receive_callback()`                          | Incoming message callback        |
| `mesh_now_set_route_failure_callback()`                    | Undeliverable route notification |
| `mesh_now_add_peer()` / `mesh_now_remove_peer()`           | Manage the one-hop peer table    |
| `mesh_now_snapshot_peers()` / `mesh_now_snapshot_routes()` | Topology snapshots               |
| `mesh_now_encode()` / `mesh_now_decode()`                  | Wire format helpers              |
| `mesh_now_get_peer_count()` / `mesh_now_get_route_count()` | Metrics                          |

## License

MIT
