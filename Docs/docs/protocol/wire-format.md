---
title: "Wire Format"
description: "Binary frame layout, field sizes, byte order, and encoding specification."
---

Mesh-NOW messages are transmitted as binary frames over ESP-NOW. The wire format uses MessagePack serialization with optional AES-128-GCM encryption.

The canonical protocol specification is [`mesh_now.ksy`](/mesh_now.ksy) at the repository root.

## Frame Layout

```text
Offset  Size    Field
------  ------  -----
0       2       magic (0x4d 0x4e)
2       1       version (1)
3       1       flags
4       1       msg_type
5       1       group_id
6       1       hop_count
7       4       message_id (LE)
11      6       sender_mac
17      6       target_mac
23      4       timestamp (LE)
27      ...     payload (MessagePack or encrypted)
------  ------  -----
```

The header is always 27 bytes. The payload follows immediately after.

## Field Encoding

All multi-byte fields use **little-endian** byte order.

### `magic` (2 bytes)

Fixed bytes `0x4d 0x4e` ("MN"). Identifies a Mesh-NOW frame.

### `version` (1 byte)

Protocol version. Currently `1`.

### `flags` (1 byte)

Bitfield encoding:

```text
Bit 0 (0x01): MSG_FLAG_REQUIRES_ACK
Bit 1 (0x02): MSG_FLAG_ENCRYPTED
Bit 2 (0x04): MSG_FLAG_HAS_NODE_NAME
Bits 3-7:     Reserved (must be 0)
```

### `msg_type` (1 byte)

Message type identifier. Values 0-6 are defined.

| Value | Name     | Description              |
|:------|:---------|:-------------------------|
| 0     | BEACON   | Peer discovery broadcast |
| 1     | CHAT     | Broadcast chat message   |
| 2     | DIRECT   | Point-to-point message   |
| 3     | ACK      | Acknowledgment           |
| 4     | GROUP    | Group-scoped broadcast   |
| 5     | PRESENCE | Status announcement      |
| 6     | TYPING   | Typing indicator         |

### `group_id` (1 byte)

Unsigned integer 0-255. Value 0 means no group filter.

### `hop_count` (1 byte)

Remaining relay count. Set to `DEFAULT_ROUTE_TTL` (3) on send, decremented at each hop. Dropped when reaching 0.

### `message_id` (4 bytes)

Monotonically increasing `uint32_t`. Assigned by the sender, seeded from `esp_random()` on first use. Used for duplicate detection and ACK matching.

### `sender_mac` / `target_mac` (6 bytes each)

IEEE 802.11 MAC addresses. Set automatically by the library.

### `timestamp` (4 bytes)

Milliseconds since `mesh_now_init()` was called.

## Payload (unencrypted)

When `MSG_FLAG_ENCRYPTED` is not set, the payload is a raw MessagePack-encoded map:

```yaml
type: map
fields:
  content:    str         # message text (variable length)
  node_name:  str         # optional, only in beacons when HAS_NODE_NAME flag set
```

## Payload (encrypted)

When `MSG_FLAG_ENCRYPTED` is set, the payload contains:

```text
Offset  Size    Field
------  ------  -----
0       12      nonce (IV)
12      ...     ciphertext (MessagePack-encoded plaintext)
...     16      AES-GCM auth tag
```

### Nonce Construction

The 12-byte nonce is built from:

- `message_id` (4 bytes, little-endian)
- `sender_mac` (8 bytes, first 8 of the 6-byte MAC padded to 8)

This ensures each message has a unique nonce as long as message IDs are unique per sender.

### Auth Tag

16-byte AES-GCM authentication tag appended after the ciphertext. Provides integrity and authenticity for both the ciphertext and the AAD (header fields).

### AAD (Additional Authenticated Data)

The following header fields are authenticated but not encrypted:

- `msg_type` (1 byte)
- `sender_mac` (6 bytes)
- `target_mac` (6 bytes)
- `group_id` (1 byte)
- `timestamp` (4 bytes)

Total AAD: 18 bytes.

## Wire Size Comparison

| Message       | Legacy (fixed 152B) | New (variable) | Savings |
|:--------------|:--------------------|:---------------|:--------|
| ACK           | 152 bytes           | ~30 bytes      | 80%     |
| Typing        | 152 bytes           | ~35 bytes      | 77%     |
| Short "ok"    | 152 bytes           | ~40 bytes      | 74%     |
| 100-char chat | 152 bytes           | ~130 bytes     | 14%     |
| Full 128B     | 152 bytes           | ~160 bytes     | -5%     |

::: callout info title:"ESP-NOW Compatibility"
ESP-NOW supports frames up to 250 bytes. The new wire format is variable-length, typically well within this limit for short messages.
::: /callout

## Next Steps

::: grids
::: grid
::: button "Encryption" ../guide/encryption.md icon:lock
::: /grid

::: grid
::: button "Message Format" ../guide/message-format.md icon:file-text
::: /grid

::: grid
::: button "State Machine" ./state-machine.md icon:cpu
::: /grid
::: /grids
