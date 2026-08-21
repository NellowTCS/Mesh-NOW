---
title: "Encryption"
description: "Payload encryption with configurable keys."
---

Mesh-NOW provides optional XOR-based payload encryption for message content.

## Setting a Key

```c
uint8_t key[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
mesh_now_set_encryption_key(key, sizeof(key));
```

The key can be up to 32 bytes. Once set, all subsequent messages (except beacons and ACKs) are encrypted before transmission.

## How It Works

Encryption is a symmetric XOR cipher applied to the `message` field:

```c
static void mesh_now_crypt_payload(uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        data[i] ^= encryption_key[i % encryption_key_len];
    }
}
```

The same function encrypts and decrypts -- XOR is its own inverse.

### Encrypt Path

```c
static void mesh_now_maybe_encrypt_message(mesh_message_t *msg)
{
    if (encryption_enabled && encryption_key_len > 0
        && msg->type != MSG_TYPE_ACK
        && msg->type != MSG_TYPE_BEACON)
    {
        msg->flags |= MSG_FLAG_ENCRYPTED;
        mesh_now_crypt_payload((uint8_t *)msg->message, sizeof(msg->message));
    }
}
```

### Decrypt Path

```c
if (mesh_msg.flags & MSG_FLAG_ENCRYPTED) {
    mesh_now_crypt_payload((uint8_t *)mesh_msg.message, sizeof(mesh_msg.message));
    mesh_msg.flags &= ~MSG_FLAG_ENCRYPTED;
}
```

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

## The `MSG_FLAG_ENCRYPTED` Flag

When a message is encrypted, the library sets bit 1 of the `flags` field:

```c
msg->flags |= MSG_FLAG_ENCRYPTED;
```

Receiving nodes check this flag and decrypt before processing:

```c
if (mesh_msg.flags & MSG_FLAG_ENCRYPTED) {
    mesh_now_crypt_payload((uint8_t *)mesh_msg.message, sizeof(mesh_msg.message));
    mesh_msg.flags &= ~MSG_FLAG_ENCRYPTED;
}
```

::: callout warning title:"Security Limitation"
XOR encryption with a repeating key is **not cryptographically secure**. It provides obfuscation against casual observation but is trivially breakable. For production use, consider integrating AES or ChaCha20. ESP-NOW provides its own 128-bit link-layer encryption when PMK is configured.
::: /callout

## Key Requirements

| Requirement | Value |
| :---------- | :---- |
| Maximum key length | 32 bytes |
| Minimum key length | 1 byte |
| Key storage | Static (set once, used until reboot) |
| Key sharing | Manual (out-of-band) |

::: grids
::: grid
::: button "Reliability" ./reliability.md icon:refresh-cw
::: /grid

::: grid
::: button "Configuration" ./configuration.md icon:settings
::: /grid

::: grid
::: button "Wire Format" ../protocol/wire-format.md icon:file-code
::: /grid
::: /grids
