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
**Relay:** Yes (route, or flood if no route is known)

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
**Relay:** Conditional (forwarded toward target like direct traffic)

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

Get the number of active, non-expired peers.

```c
int mesh_now_get_peer_count(void);
```

**Returns:** Current active peer count. Expired peers are compacted out of the table, so this reflects only peers tracked as online.

### `mesh_now_get_peers`

Get a pointer to the peer table.

```c
mesh_peer_t* mesh_now_get_peers(void);
```

**Returns:** Pointer to the internal `mesh_peer_t` array (max `MAX_PEERS` entries).

::: callout warning title:"Thread Safety"
The returned pointer references internal state. Do not free it. Access is not thread-safe -- do not read while another task is modifying the peer table. Prefer `mesh_now_snapshot_peers()` (below) from application threads like a UI streaming loop.
::: /callout

### `mesh_now_snapshot_peers`

Thread-safe copy of the peer table. Best used when the caller does not own the internal mutex (e.g. a UI thread serializing peers over a stream).

```c
int mesh_now_snapshot_peers(mesh_peer_t *out, size_t max_out);
```

| Parameter | Type | Description |
| :-------- | :--- | :---------- |
| `out` | `mesh_peer_t*` | Destination buffer caller must allocate |
| `max_out` | `size_t` | Capacity of `out` (entries) |

**Returns:** Number of entries written, at most `max_out`. Entries are a point-in-time copy taken under the mutex.

## Routing

### `mesh_now_get_route`

Copy the route to a virtual peer into `out`, if one exists.

```c
bool mesh_now_get_route(const uint8_t *dest_mac, mesh_route_t *out);
```

| Parameter | Type | Description |
| :-------- | :--- | :---------- |
| `dest_mac` | `const uint8_t*` | 6-byte MAC of the virtual peer |
| `out` | `mesh_route_t*` | Destination for the route entry |

**Returns:** `true` if a route to `dest_mac` exists and was copied.

### `mesh_now_get_route_count`

Number of active route entries (routes to virtual peers).

```c
int mesh_now_get_route_count(void);
```

### `mesh_now_snapshot_routes`

Thread-safe copy of the route table.

```c
int mesh_now_snapshot_routes(mesh_route_t *out, size_t max_out);
```

| Parameter | Type | Description |
| :-------- | :--- | :---------- |
| `out` | `mesh_route_t*` | Destination buffer caller must allocate |
| `max_out` | `size_t` | Capacity of `out` (entries) |

**Returns:** Number of entries written, at most `max_out`.

### `mesh_now_pin_route`

Pin a fixed route to `dest_mac` via `proxy_mac`. Bypasses discovery and never expires.

```c
esp_err_t mesh_now_pin_route(const uint8_t *dest_mac, const uint8_t *proxy_mac);
```

| Parameter | Type | Description |
| :-------- | :--- | :---------- |
| `dest_mac` | `const uint8_t*` | 6-byte MAC of the destination |
| `proxy_mac` | `const uint8_t*` | 6-byte MAC of the relaying next hop |

**Returns:** `ESP_OK` on success, `ESP_ERR_INVALID_ARG` if a parameter is NULL.

### `mesh_now_unpin_route`

Remove a previously pinned route. Fails if none exists for `dest_mac`.

```c
esp_err_t mesh_now_unpin_route(const uint8_t *dest_mac);
```

### `mesh_now_set_route_failure_callback`

Install the handler fired when a route to a destination could not be found.

```c
void mesh_now_set_route_failure_callback(mesh_now_route_failure_callback_t cb);
```

| Parameter | Type | Description |
| :-------- | :--- | :---------- |
| `cb` | `mesh_now_route_failure_callback_t` | Function pointer: `void (*)(const uint8_t *dest_mac)` |

## Node Naming

### `mesh_now_set_name`

Set the local node name (truncated to `MESH_NOW_NODE_NAME_MAX` = 16 chars). Announced in beacons.

```c
esp_err_t mesh_now_set_name(const char *name);
```

| Parameter | Type | Description |
| :-------- | :--- | :---------- |
| `name` | `const char*` | Null-terminated name string |

**Returns:** `ESP_OK` on success, `ESP_ERR_INVALID_ARG` if `name` is NULL.

### `mesh_now_get_name`

Return the current local node name (empty until `mesh_now_set_name()`).

```c
const char *mesh_now_get_name(void);
```

### `mesh_now_announce_name`

Push the current name out immediately with an extra beacon instead of waiting for the next interval.

```c
esp_err_t mesh_now_announce_name(void);
```

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
| `len` | `size_t` | Key length (must be exactly 16 bytes) |

**Returns:** `ESP_OK` on success, `ESP_ERR_INVALID_ARG` if key is NULL or `len` is not 16.

### `mesh_now_is_encrypted`

True once encryption is configured (after `mesh_now_set_encryption_key()`).

```c
bool mesh_now_is_encrypted(void);
```

### `mesh_now_peer_is_online`

True if a peer entry was contacted within the peer expiry window.

```c
bool mesh_now_peer_is_online(const mesh_peer_t *peer);
```

### `mesh_now_get_group_id`

Current local group id (0 = not in a group).

```c
uint8_t mesh_now_get_group_id(void);
```

## Types

### `mesh_message_t`

```c
typedef struct {
    uint8_t type;
    uint8_t flags;
    uint8_t group_id;
    uint8_t hop_limit;
    uint8_t hop_count;
    uint32_t message_id;
    uint32_t reply_to;
    uint8_t sender_mac[6];
    uint8_t target_mac[6];
    uint32_t timestamp;
    char message[128];
    char node_name[17];
    uint8_t neighbor_count;
    uint8_t neighbor_macs[8][6];
    char neighbor_names[8][17];
} mesh_message_t;
```

### `mesh_peer_t`

```c
typedef struct {
    uint8_t peer_addr[6];
    bool active;
    int64_t last_seen;
    char node_name[17];
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
| `DEFAULT_ROUTE_TTL` | 3 | Default hop limit |
| `MESH_NOW_WIRE_VERSION` | 1 | Wire format version |
| `MESH_NOW_HEADER_LEN` | 32 | Wire header size |
| `MAX_PEERS` | 20 | Max one-hop peer count |
| `MSG_FLAG_REQUIRES_ACK` | `0x01` | ACK requested flag |
| `MSG_FLAG_ENCRYPTED` | `0x02` | Encrypted payload flag |
| `MSG_FLAG_HAS_NODE_NAME` | `0x04` | Beacon carries a name |
| `MSG_TYPE_BEACON` | 0 | Discovery beacon |
| `MSG_TYPE_CHAT` | 1 | Broadcast chat |
| `MSG_TYPE_DIRECT` | 2 | Point-to-point |
| `MSG_TYPE_ACK` | 3 | Acknowledgment |
| `MSG_TYPE_GROUP` | 4 | Group message |
| `MSG_TYPE_PRESENCE` | 5 | Status announcement |
| `MSG_TYPE_TYPING` | 6 | Typing indicator |
| `MSG_TYPE_ROUTE_REQUEST` | 7 | Route discovery flood |
| `MSG_TYPE_ROUTE_REPLY` | 8 | Route discovery reply |
| `MSG_TYPE_ROUTE_ERROR` | 9 | Broken next-hop announcement |

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
