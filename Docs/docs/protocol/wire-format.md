---
title: "Wire Format"
description: "Binary frame layout, field sizes, byte order, and encoding specification."
---

Mesh-NOW messages are transmitted as raw `mesh_message_t` structs over ESP-NOW.

## Frame Layout

```
Offset  Size  Field
------  ----  -----
0       1     type
1       1     flags
2       1     group_id
3       1     hop_count
4       4     message_id
8       6     sender_mac
14      6     target_mac
20      4     timestamp
24      128   message
------  ----  -----
Total:  152 bytes
```

::: callout info title:"ESP-NOW Compatibility"
ESP-NOW supports frames up to 250 bytes. The `mesh_message_t` struct is 152 bytes (or 154 with padding), well within the limit.
::: /callout

## Field Encoding

All multi-byte fields use **little-endian** byte order (native ESP32/Xtensa/RISC-V format).

### `type` (1 byte)

Message type identifier. Values 0-6 are defined. Values 7-255 are reserved for future use.

| Value | Name | Description |
| :---- | :--- | :---------- |
| 0 | BEACON | Peer discovery broadcast |
| 1 | CHAT | Broadcast chat message |
| 2 | DIRECT | Point-to-point message |
| 3 | ACK | Acknowledgment |
| 4 | GROUP | Group-scoped broadcast |
| 5 | PRESENCE | Status announcement |
| 6 | TYPING | Typing indicator |
| 7-255 | Reserved | -- |

### `flags` (1 byte)

Bitfield encoding:

```
Bit 0 (0x01): MSG_FLAG_REQUIRES_ACK
Bit 1 (0x02): MSG_FLAG_ENCRYPTED
Bits 2-7:     Reserved (must be 0)
```

### `group_id` (1 byte)

Unsigned integer 0-255. Value 0 means no group filter.

### `hop_count` (1 byte)

Unsigned integer representing remaining relay count. Set to `DEFAULT_ROUTE_TTL` (3) on send, decremented at each hop. Message is dropped when reaching 0.

### `message_id` (4 bytes)

Monotonically increasing `uint32_t`. Assigned by the sender. Wraps to 1 after `UINT32_MAX`. Used for:
- Duplicate detection at receivers
- ACK matching for direct messages

### `sender_mac` (6 bytes)

IEEE 802.11 MAC address of the originating node. Set automatically by the library.

### `target_mac` (6 bytes)

MAC address of the intended recipient. For broadcast messages, this is `FF:FF:FF:FF:FF:FF`. For direct messages, this is the specific peer's MAC.

### `timestamp` (4 bytes)

Milliseconds since `mesh_now_init()` was called. Set automatically by the library.

### `message` (128 bytes)

Null-terminated character payload. Maximum 127 characters plus null terminator. When encrypted, the XOR cipher is applied to all 128 bytes (including trailing null bytes).

## Byte Order Diagram

```
Byte:  0     1     2     3     4          8          14         20         24            152
      +-----+-----+-----+-----+----------+----------+----------+----------+-------------+
      |type |flags|grp  |hop  | message_id (LE 32b)  | sender_mac (6B)   | target_mac  |
      +-----+-----+-----+-----+----------+----------+----------+----------+-------------+
      | timestamp (LE 32b)   | message (128B, null-terminated, XOR-encrypted if flag set)  |
      +----------------------+--------------------------------------------------------------+
```

## Alignment and Padding

The struct may contain padding bytes depending on the compiler and target architecture. The ESP-IDF GCC toolchain for Xtensa and RISC-V typically does not pad this struct because all fields are 1 byte or naturally aligned.

::: callout warning title:"Do Not Serialize"
Never serialize `mesh_message_t` to a file or network stream and deserialize on a different architecture. The struct layout is target-specific. Use it only as an ESP-NOW frame payload between compatible ESP32 nodes.
::: /callout

## Encryption Layer

When `MSG_FLAG_ENCRYPTED` is set, the `message` field (all 128 bytes) is XOR-encrypted with the configured key. The key repeats cyclically:

```
encrypted[i] = message[i] ^ key[i % key_length]
```

The flag, type, MAC addresses, and other header fields are **never** encrypted.

## Next Steps

::: grids
::: grid
::: button "State Machine" ./state-machine.md icon:cpu
::: /grid

::: grid
::: button "Message Format" ../guide/message-format.md icon:file-text
::: /grid

::: grid
::: button "Encryption" ../guide/encryption.md icon:lock
::: /grid
::: /grids
