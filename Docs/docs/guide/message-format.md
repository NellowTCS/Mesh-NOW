---
title: "Message Format"
description: "The mesh_message_t structure, message types, flags, and fields."
---

Every Mesh-NOW message is serialized to MessagePack and sent as a binary ESP-NOW frame.

## Wire Format

Messages use a binary envelope with a 31-byte header followed by a MessagePack-encoded payload. See [Wire Format](../protocol/wire-format.md) for the full binary layout.

The canonical protocol specification is [`mesh_now.ksy`](/mesh_now.ksy) at the repository root.

## Internal Structure

The library uses this structure internally:

```c
typedef struct {
    uint8_t type;              // Message type (0-6)
    uint8_t flags;             // Bitfield: REQUIRES_ACK, ENCRYPTED, HAS_NODE_NAME
    uint8_t group_id;          // Group identifier (0-255)
    uint8_t hop_count;         // Remaining hops before drop
    uint32_t message_id;       // Unique message identifier
    uint32_t reply_to;         // Message id being acked (ACK frames only)
    uint8_t sender_mac[6];     // Sender MAC address
    uint8_t target_mac[6];     // Target MAC (direct messages)
    uint32_t timestamp;        // Milliseconds since init
    char message[128];         // Payload (null-terminated)
    char node_name[17];        // Node name (beacons only)
} mesh_message_t;
```

::: callout warning title:"Internal Only"
`mesh_message_t` is an internal representation. On the wire, messages are serialized to MessagePack with variable-length fields, not sent as raw structs.
::: /callout

## Field Reference

| Field | Size | Description |
| :---- | :--- | :---------- |
| `type` | 1 byte | Message type identifier (see below) |
| `flags` | 1 byte | Bitfield: ACK required, encrypted, has node name |
| `group_id` | 1 byte | Group membership filter (0 = no group) |
| `hop_count` | 1 byte | Remaining relay count; decremented at each hop |
| `message_id` | 4 bytes | Monotonically increasing unique ID (random seed) |
| `reply_to` | 4 bytes | For ACK frames, the `message_id` being acknowledged; 0 otherwise |
| `sender_mac` | 6 bytes | MAC address of the originating node |
| `target_mac` | 6 bytes | MAC address of the intended recipient |
| `timestamp` | 4 bytes | Milliseconds since `mesh_now_init()` was called |
| `message` | variable | Payload string (null-terminated) |
| `node_name` | variable | Node name (beacons with HAS_NODE_NAME flag) |

## Message Types

| Value | Name | Description | ACK | Relay |
| :---- | :--- | :---------- | :-- | :---- |
| 0 | `MSG_TYPE_BEACON` | Peer discovery broadcast | No | No |
| 1 | `MSG_TYPE_CHAT` | Broadcast chat message | No | Yes |
| 2 | `MSG_TYPE_DIRECT` | Point-to-point message | Yes | Yes |
| 3 | `MSG_TYPE_ACK` | Acknowledgment response | No | Yes |
| 4 | `MSG_TYPE_GROUP` | Group-scoped broadcast | No | Yes |
| 5 | `MSG_TYPE_PRESENCE` | Status announcement | No | Yes |
| 6 | `MSG_TYPE_TYPING` | Typing indicator | No | Conditional |

## Flags

| Flag | Value | Meaning |
| :--- | :---- | :------ |
| `MSG_FLAG_REQUIRES_ACK` | `0x01` | Sender expects an ACK response |
| `MSG_FLAG_ENCRYPTED` | `0x02` | Payload is AES-128-GCM encrypted |
| `MSG_FLAG_HAS_NODE_NAME` | `0x04` | Beacon includes a node name string |

## Sending Functions

```c
// Broadcast to all peers
esp_err_t mesh_now_send_broadcast(const char *message);

// Alias for broadcast
esp_err_t mesh_now_send_message(const char *message);

// Direct to a specific peer (with ACK + retransmit)
esp_err_t mesh_now_send_direct(const uint8_t *target_mac, const char *message);

// Group-scoped broadcast
esp_err_t mesh_now_send_group(uint8_t group_id, const char *message);

// Presence announcement
esp_err_t mesh_now_send_presence(const char *status);

// Typing indicator to a specific peer
esp_err_t mesh_now_send_typing(const uint8_t *target_mac, bool typing);
```

## Node Naming

Nodes can set a human-readable name that is broadcast in beacons:

```c
mesh_now_set_name("Sensor-01");
```

When a beacon with `MSG_FLAG_HAS_NODE_NAME` is received, the name is stored in the peer table and accessible via `mesh_peer_t.node_name`.

::: callout info title:"Message ID Assignment"
The library assigns `message_id` automatically via an internal counter seeded from `esp_random()`. You do not set it manually. IDs wrap to 1 after `UINT32_MAX`.
::: /callout

## Next Steps

::: grids
::: grid
::: button "Peer Discovery" ./peer-discovery.md icon:search
::: /grid

::: grid
::: button "Routing" ./routing.md icon:radio
::: /grid

::: grid
::: button "Wire Format" ../protocol/wire-format.md icon:file-code
::: /grid
::: /grids
