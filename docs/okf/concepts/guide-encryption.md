---
type: concept
title: Encryption
description: "AES-128-GCM payload encryption with per-message nonces."
source: "https://NellowTCS.github.io/Mesh-NOW/guide/encryption/"
path: /guide/encryption/
updated: 2026-09-15
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-09-15T06:11:24.834Z"
---
---
title: "Encryption"
description: "AES-128-GCM payload encryption with per-message nonces."
---

Mesh-NOW gives you optional AES-128-GCM encryption for message payloads.

## Setting a Key

```c
uint8_t key[16] = { /* 16 bytes */ };
mesh_now_set_encryption_key(key, sizeof(key));
```

The key must be exactly 16 bytes (AES-128), or the call returns `ESP_ERR_INVALID_ARG`. Once set, all data messages (chat, direct, group, presence, typing) are encrypted before transmission. Control frames (beacons, ACKs, and route messages) are always sent in the clear. See the [threat model](./security.md) for why, and what that boundary means.

## How It Works

Encryption uses AES-128-GCM via mbedtls (bundled in ESP-IDF, hardware-accelerated on ESP32).

### Encrypt Path

1. The message is serialized to MessagePack (plaintext).
2. A 12-byte nonce is built from `message_id` (4 bytes LE), `sender_mac` (6 bytes), and a fixed 2-byte pad.
3. AES-128-GCM encrypts the plaintext with the key and nonce.
4. AAD (additional authenticated data) covers `msg_type`, `sender_mac`, `target_mac`, `group_id`, `timestamp`.
5. The 16-byte auth tag is appended after the ciphertext.

### Decrypt Path

1. The nonce is extracted from the first 12 bytes of the encrypted payload.
2. The auth tag is extracted from the last 16 bytes.
3. AES-128-GCM decrypts and verifies authenticity.
4. If authentication fails (data tampered in transit or crafted), the message is dropped.
5. The plaintext is deserialized from MessagePack.

## What Gets Encrypted

| Message Type             | Encrypted? | Reason                                     |
| ------------------------ | ---------- | ------------------------------------------ |
| `MSG_TYPE_BEACON`        | No         | Peer discovery must stay keyless           |
| `MSG_TYPE_ACK`           | No         | Delivery confirmation is routing metadata  |
| `MSG_TYPE_ROUTE_REQUEST` | No         | Routing must work without the key          |
| `MSG_TYPE_ROUTE_REPLY`   | No         | Routing must work without the key          |
| `MSG_TYPE_ROUTE_ERROR`   | No         | Routing must work without the key          |
| `MSG_TYPE_CHAT`          | Yes        | User content                               |
| `MSG_TYPE_DIRECT`        | Yes        | User content                               |
| `MSG_TYPE_GROUP`         | Yes        | User content                               |
| `MSG_TYPE_PRESENCE`      | Yes        | Status content                             |
| `MSG_TYPE_TYPING`        | Yes        | Status content                             |

## Nonce Construction

Each message gets a unique 12-byte nonce:

```text
Bytes 0-3:  message_id (little-endian)
Bytes 4-9:  sender_mac (6 bytes)
Bytes 10-11: fixed 0x00 pad
```

Message IDs come from a counter seeded once from `esp_random()`, so they are unpredictable at boot and monotonic from there. This layout guarantees nonce uniqueness for as long as message IDs stay unique per sender. The fixed last two bytes are a domain separator, and each sender's MAC disambiguates nodes that share the same network key.

## Key Requirements

| Requirement | Value                                |
| ----------- | ------------------------------------ |
| Key length  | Exactly 16 bytes (AES-128)           |
| Key storage | Static (set once, used until reboot) |
| Key sharing | Manual (out-of-band)                 |

::: grids
::: grid
::: button "Wire Format" ../protocol/wire-format.md icon:file-code
::: /grid

::: grid
::: button "Configuration" ./configuration.md icon:settings
::: /grid

::: grid
::: button "Reliability" ./reliability.md icon:refresh-cw
::: /grid
::: /grids
