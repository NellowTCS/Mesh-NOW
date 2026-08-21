---
title: "Mesh NOW API"
description: "Complete C API reference for the mesh_now library."
---

Full API reference for the Mesh-NOW library. All functions are declared in `mesh_now.h`.

## Initialization

### `mesh_now_init`

Initialize the ESP-NOW mesh networking subsystem. Registers callbacks, adds the broadcast peer, and starts the beacon and retransmit tasks.

```c
esp_err_t mesh_now_init(void);
```

**Returns:** `ESP_OK` on success, appropriate `esp_err_t` on failure.

**Side effects:**
- Initializes ESP-NOW
- Registers send and receive callbacks
- Adds broadcast peer (`FF:FF:FF:FF:FF:FF`)
- Creates `beacon_task` pinned to core 0
- Creates `retransmit_task` pinned to core 0

### `mesh_now_deinit`

Deinitialize the mesh. Stops tasks, removes peers, unregisters callbacks.

```c
esp_err_t mesh_now_deinit(void);
```

**Returns:** `ESP_OK` on success.

**Side effects:**
- Deletes beacon and retransmit tasks
- Removes broadcast peer
- Unregisters ESP-NOW callbacks
- Clears peer table

## Sending

### `mesh_now_send_broadcast`

Send a chat message to all nodes in range.

```c
esp_err_t mesh_now_send_broadcast(const char *message);
```

| Parameter | Type | Description |
| :-------- | :--- | :---------- |
| `message` | `const char*` | Null-terminated message string (truncated to 127 chars) |

**Returns:** `ESP_OK` on success.

**Message type:** `MSG_TYPE_CHAT`
**ACK:** No
**Relay:** Yes

### `mesh_now_send_message`

Alias for `mesh_now_send_broadcast`.

```c
esp_err_t mesh_now_send_message(const char *message);
```

### `mesh_now_send_direct`

Send a message to a specific peer. Requires ACK, will retransmit up to 3 times.

```c
esp_err_t mesh_now_send_direct(const uint8_t *target_mac, const char *message);
```

| Parameter | Type | Description |
| :-------- | :--- | :---------- |
| `target_mac` | `const uint8_t*` | 6-byte MAC address of the target |
| `message` | `const char*` | Null-terminated message string |

**Returns:** `ESP_OK` on success, `ESP_ERR_INVALID_ARG` if parameters are NULL.

**Message type:** `MSG_TYPE_DIRECT`
**ACK:** Yes
**Relay:** Yes

### `mesh_now_send_group`

Send a message to all nodes in a specific group.

```c
esp_err_t mesh_now_send_group(uint8_t group_id, const char *message);
```

| Parameter | Type | Description |
| :-------- | :--- | :---------- |
| `group_id` | `uint8_t` | Group identifier (0-255) |
| `message` | `const char*` | Null-terminated message string |

**Returns:** `ESP_OK` on success.

**Message type:** `MSG_TYPE_GROUP`
**ACK:** No
**Relay:** Yes

### `mesh_now_send_presence`

Broadcast a presence/status announcement.

```c
esp_err_t mesh_now_send_presence(const char *status);
```

| Parameter | Type | Description |
| :-------- | :--- | :---------- |
| `status` | `const char*` | Status string (e.g., "online", "away") |

**Returns:** `ESP_OK` on success.

**Message type:** `MSG_TYPE_PRESENCE`
**ACK:** No
**Relay:** Yes

### `mesh_now_send_typing`

Send a typing indicator to a specific peer.

```c
esp_err_t mesh_now_send_typing(const uint8_t *target_mac, bool typing);
```

| Parameter | Type | Description |
| :-------- | :--- | :---------- |
| `target_mac` | `const uint8_t*` | 6-byte MAC address of the target |
| `typing` | `bool` | `true` if typing, `false` if stopped |

**Returns:** `ESP_OK` on success, `ESP_ERR_INVALID_ARG` if `target_mac` is NULL.

**Message type:** `MSG_TYPE_TYPING`
**ACK:** No
**Relay:** Conditional (only if target != self)

## Peer Management

### `mesh_now_add_peer`

Add a peer to the mesh. Called automatically when receiving beacons and messages. Ignores the local MAC address.

```c
void mesh_now_add_peer(const uint8_t *mac);
```

| Parameter | Type | Description |
| :-------- | :--- | :---------- |
| `mac` | `const uint8_t*` | 6-byte MAC address |

### `mesh_now_remove_peer`

Remove a peer from the mesh.

```c
void mesh_now_remove_peer(const uint8_t *mac);
```

| Parameter | Type | Description |
| :-------- | :--- | :---------- |
| `mac` | `const uint8_t*` | 6-byte MAC address |

### `mesh_now_get_peer_count`

Get the number of known peers.

```c
int mesh_now_get_peer_count(void);
```

**Returns:** Current peer count.

### `mesh_now_get_peers`

Get a pointer to the peer table.

```c
mesh_peer_t* mesh_now_get_peers(void);
```

**Returns:** Pointer to the internal `mesh_peer_t` array (max `MAX_PEERS` entries).

::: callout warning title:"Thread Safety"
The returned pointer references internal state. Do not free it. Access is not thread-safe -- do not read while another task is modifying the peer table.
::: /callout

## Configuration

### `mesh_now_set_receive_callback`

Set the callback function for received messages.

```c
void mesh_now_set_receive_callback(mesh_now_receive_callback_t callback);
```

| Parameter | Type | Description |
| :-------- | :--- | :---------- |
| `callback` | `mesh_now_receive_callback_t` | Function pointer: `void (*)(const mesh_message_t*)` |

If no callback is set, messages are queued in the internal message queue (see [Message Queue](./message-queue)).

### `mesh_now_set_group`

Set the local node's group membership.

```c
esp_err_t mesh_now_set_group(uint8_t group_id);
```

| Parameter | Type | Description |
| :-------- | :--- | :---------- |
| `group_id` | `uint8_t` | Group identifier (0 = no group) |

**Returns:** Always `ESP_OK`.

### `mesh_now_set_encryption_key`

Set the payload encryption key.

```c
esp_err_t mesh_now_set_encryption_key(const uint8_t *key, size_t len);
```

| Parameter | Type | Description |
| :-------- | :--- | :---------- |
| `key` | `const uint8_t*` | Encryption key bytes |
| `len` | `size_t` | Key length (1-32 bytes) |

**Returns:** `ESP_OK` on success, `ESP_ERR_INVALID_ARG` if key is NULL, length is 0, or length > 32.

## Types

### `mesh_message_t`

```c
typedef struct {
    uint8_t type;
    uint8_t flags;
    uint8_t group_id;
    uint8_t hop_count;
    uint32_t message_id;
    uint8_t sender_mac[6];
    uint8_t target_mac[6];
    uint32_t timestamp;
    char message[128];
} mesh_message_t;
```

### `mesh_peer_t`

```c
typedef struct {
    uint8_t peer_addr[6];
    bool active;
} mesh_peer_t;
```

### `mesh_now_receive_callback_t`

```c
typedef void (*mesh_now_receive_callback_t)(const mesh_message_t *message);
```

## Constants

| Constant | Value | Description |
| :------- | :---- | :---------- |
| `MAX_MESH_MESSAGE_LEN` | 128 | Max payload length |
| `DEFAULT_ROUTE_TTL` | 3 | Default hop count |
| `MAX_PEERS` | 20 | Max peer count |
| `MSG_FLAG_REQUIRES_ACK` | `0x01` | ACK requested flag |
| `MSG_FLAG_ENCRYPTED` | `0x02` | Encrypted payload flag |
| `MSG_TYPE_BEACON` | 0 | Discovery beacon |
| `MSG_TYPE_CHAT` | 1 | Broadcast chat |
| `MSG_TYPE_DIRECT` | 2 | Point-to-point |
| `MSG_TYPE_ACK` | 3 | Acknowledgment |
| `MSG_TYPE_GROUP` | 4 | Group message |
| `MSG_TYPE_PRESENCE` | 5 | Status announcement |
| `MSG_TYPE_TYPING` | 6 | Typing indicator |

## Next Steps

::: grids
::: grid
::: button "Message Queue" ./message-queue.md icon:layers
::: /grid

::: grid
::: button "Wire Format" ../protocol/wire-format.md icon:file-code
::: /grid

::: grid
::: button "Configuration" ../guide/configuration.md icon:settings
::: /grid
::: /grids
