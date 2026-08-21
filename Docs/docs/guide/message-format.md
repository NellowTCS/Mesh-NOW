---
title: "Message Format"
description: "The mesh_message_t structure, message types, flags, and fields."
---

Every Mesh-NOW message is a fixed-size `mesh_message_t` struct sent as a raw ESP-NOW frame.

## Structure

```c
typedef struct {
    uint8_t type;              // Message type (0-6)
    uint8_t flags;             // Bitfield: REQUIRES_ACK, ENCRYPTED
    uint8_t group_id;          // Group identifier (0-255)
    uint8_t hop_count;         // Remaining hops before drop
    uint32_t message_id;       // Unique message identifier
    uint8_t sender_mac[6];     // Sender MAC address
    uint8_t target_mac[6];     // Target MAC (direct messages)
    uint32_t timestamp;        // Milliseconds since init
    char message[128];         // Payload (null-terminated)
} mesh_message_t;
```

**Total frame size:** 154 bytes (fits within ESP-NOW's 250-byte limit).

## Field Reference

| Field | Size | Description |
| :---- | :--- | :---------- |
| `type` | 1 byte | Message type identifier (see below) |
| `flags` | 1 byte | Bitfield: bit 0 = requires ACK, bit 1 = encrypted |
| `group_id` | 1 byte | Group membership filter (0 = no group) |
| `hop_count` | 1 byte | Remaining relay count; decremented at each hop |
| `message_id` | 4 bytes | Monotonically increasing unique ID |
| `sender_mac` | 6 bytes | MAC address of the originating node |
| `target_mac` | 6 bytes | MAC address of the intended recipient (broadcast = `FF:FF:FF:FF:FF:FF`) |
| `timestamp` | 4 bytes | Milliseconds since `mesh_now_init()` was called |
| `message` | 128 bytes | Null-terminated payload string |

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
| `MSG_FLAG_ENCRYPTED` | `0x02` | Payload is XOR-encrypted |

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

::: callout info title:"Message ID Assignment"
The library assigns `message_id` automatically via an internal counter. You do not set it manually. IDs wrap to 1 after `UINT32_MAX`.
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
