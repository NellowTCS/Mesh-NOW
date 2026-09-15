---
type: concept
title: "Message Format"
description: "The mesh_message_t structure, message types, flags, and fields."
source: "https://NellowTCS.github.io/Mesh-NOW/guide/message-format/"
path: /guide/message-format/
updated: 2026-09-15
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-09-15T13:25:00.608Z"
---
---
title: "Message Format"
description: "The mesh_message_t structure, message types, flags, and fields."
---

Every Mesh-NOW message is serialized to MessagePack and sent as a binary ESP-NOW frame.

## Wire Format

Messages use a binary envelope: a 32-byte header followed by a MessagePack-encoded payload. The full binary layout is in [Wire Format](../protocol/wire-format.md). The canonical protocol specification is [`mesh_now.ksy`](/mesh_now.ksy) at the repository root.

## Internal Structure

The library works with this structure internally:

```c
typedef struct {
    uint8_t type;              // Message type (0-9)
    uint8_t flags;             // Bitfield: REQUIRES_ACK, ENCRYPTED, HAS_NODE_NAME
    uint8_t group_id;          // Group identifier (0-255)
    uint8_t hop_limit;         // Max hops for this frame
    uint8_t hop_count;         // Hops taken so far
    uint32_t message_id;       // Unique message identifier
    uint32_t reply_to;         // Message id being acknowledged (ACK frames only)
    uint8_t sender_mac[6];     // Sender MAC address
    uint8_t target_mac[6];     // Target MAC (direct messages)
    uint32_t timestamp;        // Network time in ms
    char message[MAX_MESH_MESSAGE_LEN];      // Payload (null-terminated)
    char node_name[MESH_NOW_NODE_NAME_MAX + 1];  // Node name (beacons/RREQ/RREP)
    uint8_t neighbor_count;    // Zone announce: count of advertised one-hop peers
    uint8_t neighbor_macs[CONFIG_MESH_NOW_MAX_BEACON_NEIGHBORS][6];
    char neighbor_names[CONFIG_MESH_NOW_MAX_BEACON_NEIGHBORS][MESH_NOW_NODE_NAME_MAX + 1];
} mesh_message_t;
```

::: callout warning title:"Internal Only"
`mesh_message_t` is an internal representation. On the wire, messages are serialized to MessagePack with variable-length fields, never as raw struct bytes.
::: /callout

## Field Reference

| Field            | Size     | Description                                                      |
| ---------------- | -------- | ---------------------------------------------------------------- |
| `type`           | 1 byte   | Message type identifier (see below)                              |
| `flags`          | 1 byte   | Bitfield: ACK required, encrypted, has node name                 |
| `group_id`       | 1 byte   | Group membership filter (0 = no group)                           |
| `hop_limit`      | 1 byte   | Max hops, set at origin                                          |
| `hop_count`      | 1 byte   | Hops taken; dropped when it reaches `hop_limit`                  |
| `message_id`     | 4 bytes  | Unique ID from a monotonic counter (random seed)                 |
| `reply_to`       | 4 bytes  | For ACK frames, the `message_id` being acknowledged; 0 otherwise |
| `sender_mac`     | 6 bytes  | MAC address of the originating node                              |
| `target_mac`     | 6 bytes  | MAC address of the intended recipient                            |
| `timestamp`      | 4 bytes  | Network time in ms, synced from beacons                          |
| `message`        | variable | Payload string (null-terminated)                                 |
| `node_name`      | variable | Node name (beacons with HAS_NODE_NAME flag)                      |
| `neighbor_count` | 1 byte   | Number of one-hop peers advertised (beacons only)                |
| `neighbor_macs`  | variable | Advertised one-hop peer MACs (beacons only)                      |
| `neighbor_names` | variable | Advertised peer names (beacons only)                             |

## Message Types

| Value | Name                     | Description                  | ACK | Relay                         |
| ----- | ------------------------ | ---------------------------- | --- | ----------------------------- |
| 0     | `MSG_TYPE_BEACON`        | Peer discovery broadcast     | No  | No (single hop, not relayed)  |
| 1     | `MSG_TYPE_CHAT`          | Broadcast chat message       | No  | Yes                           |
| 2     | `MSG_TYPE_DIRECT`        | Point-to-point message       | Yes | Yes (toward target)           |
| 3     | `MSG_TYPE_ACK`           | Acknowledgment response      | No  | Yes (toward target)           |
| 4     | `MSG_TYPE_GROUP`         | Group-scoped broadcast       | No  | Yes                           |
| 5     | `MSG_TYPE_PRESENCE`      | Status announcement          | No  | Yes                           |
| 6     | `MSG_TYPE_TYPING`        | Typing indicator             | No  | Conditional (toward target)   |
| 7     | `MSG_TYPE_ROUTE_REQUEST` | Route discovery flood        | No  | Yes (flood while TTL remains) |
| 8     | `MSG_TYPE_ROUTE_REPLY`   | Route discovery reply        | No  | Yes (toward originator)       |
| 9     | `MSG_TYPE_ROUTE_ERROR`   | Broken next-hop announcement | No  | Yes                           |

## Flags

| Flag                     | Value  | Meaning                            |
| ------------------------ | ------ | ---------------------------------- |
| `MSG_FLAG_REQUIRES_ACK`  | `0x01` | Sender expects an ACK response     |
| `MSG_FLAG_ENCRYPTED`     | `0x02` | Payload is AES-128-GCM encrypted   |
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

When a beacon with `MSG_FLAG_HAS_NODE_NAME` arrives, the name is stored in the peer table and shows up in `mesh_peer_t.node_name`.

::: callout info title:"Message ID Assignment"
The library assigns `message_id` itself from a counter seeded once with `esp_random()`. You never set it by hand. After the counter goes through `UINT32_MAX` and wraps to zero, the next allocation re-seeds from `esp_random()`, so IDs stay unpredictable across runs.
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
