---
title: "Encryption"
description: "AES-128-GCM payload encryption with per-message nonces."
---

Mesh-NOW provides optional AES-128-GCM encryption for message payloads.

## Setting a Key

```c
uint8_t key[16] = { /* 16 bytes */ };
mesh_now_set_encryption_key(key, sizeof(key));
```

The key must be exactly 16 bytes (AES-128). Once set, all subsequent messages (except beacons and ACKs) are encrypted before transmission.

## How It Works

Encryption uses AES-128-GCM via mbedtls (bundled in ESP-IDF, hardware-accelerated on ESP32).

### Encrypt Path

1. The message is serialized to MessagePack (plaintext).
2. A 12-byte nonce is built from `message_id` (4 bytes LE), `sender_mac` (6 bytes), and a fixed 2-byte pad.
3. AES-128-GCM encrypts the plaintext with the key and nonce.
4. AAD (additional authenticated data) covers: `msg_type`, `sender_mac`, `target_mac`, `group_id`, `timestamp`.
5. The 16-byte auth tag is appended after the ciphertext.

### Decrypt Path

1. The nonce is extracted from the first 12 bytes of the encrypted payload.
2. The auth tag is extracted from the last 16 bytes.
3. AES-128-GCM decrypts and verifies authenticity.
4. If authentication fails (tampered data), the message is dropped.
5. The plaintext is deserialized from MessagePack.

## What Gets Encrypted

| Message Type | Encrypted? | Reason |
| :----------- | :--------- | :----- |
| `MSG_TYPE_BEACON` | No | Must be readable for peer discovery |
| `MSG_TYPE_ACK` | No | Must be readable for delivery confirmation |
| `MSG_TYPE_CHAT` | Yes | User content |
| `MSG_TYPE_DIRECT` | Yes | User content |
| `MSG_TYPE_GROUP` | Yes | User content |
| `MSG_TYPE_PRESENCE` | Yes | Status content |
| `MSG_TYPE_TYPING` | Yes | Status content |

## Nonce Construction

Each message gets a unique 12-byte nonce:

```
Bytes 0-3:  message_id (little-endian)
Bytes 4-9:  sender_mac (6 bytes)
Bytes 10-11: fixed 0x00 pad
```

This guarantees nonce uniqueness as long as message IDs are unique per sender. Message IDs are seeded from `esp_random()` and increment monotonically. The fixed last two bytes are a domain separator; uniqueness holds because message IDs increment and each sender's MAC is unique per shared network key.

## Key Requirements

| Requirement | Value |
| :---------- | :---- |
| Key length | Exactly 16 bytes (AES-128) |
| Key storage | Static (set once, used until reboot) |
| Key sharing | Manual (out-of-band) |

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
