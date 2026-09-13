---
title: "Configuration"
description: "All compile-time constants, tuning knobs, and runtime settings."
---

Mesh-NOW is configured via compile-time tokens and runtime API calls. Runtime-tunable values (beacon interval, retry/ACK timings, peer expiry) are **Kconfig options** under the `Mesh-NOW` menu, exposed through `idf.py menuconfig` as `CONFIG_MESH_NOW_*`. Structural limits that affect the wire format or ABI are hard `#define`s in `mesh_now.h`.

## Compile-Time Constants

| Constant | Default | Defined | Description |
| :------- | :------ | :------ | :---------- |
| `CONFIG_MESH_NOW_BEACON_INTERVAL_MS` | 5000 | Kconfig | Beacon broadcast interval (ms) |
| `CONFIG_MESH_NOW_RETRANSMIT_TIMEOUT_MS` | 2000 | Kconfig | Retransmit timeout (ms) |
| `CONFIG_MESH_NOW_MAX_RETRIES` | 3 | Kconfig | Maximum retransmission attempts |
| `CONFIG_MESH_NOW_MAX_PENDING_MESSAGES` | 16 | Kconfig | Pending message table size |
| `CONFIG_MESH_NOW_MAX_SEEN_MESSAGE_IDS` | 128 | Kconfig | Duplicate detection buffer size |
| `CONFIG_MESH_NOW_DEFAULT_ROUTE_TTL` | 3 | Kconfig | Default hop limit for messages |
| `CONFIG_MESH_NOW_PEER_EXPIRY_SEC` | 30 | Kconfig | Seconds without contact before a peer is expired and compacted out of the active table |
| `CONFIG_MESH_NOW_WIFI_CHANNEL` | 1 | Kconfig | 802.11 channel all mesh nodes must share |
| `CONFIG_MESH_NOW_MAX_ROUTES` | 32 | Kconfig | Route table size (virtual peers) |
| `CONFIG_MESH_NOW_ROUTE_LIFETIME_SEC` | 30 | Kconfig | Seconds an unused route stays valid |
| `CONFIG_MESH_NOW_ROUTE_REQ_TTL` | 8 | Kconfig | Hop limit of RREQ discovery floods |
| `CONFIG_MESH_NOW_ROUTE_REQ_TIMEOUT_MS` | 3000 | Kconfig | Timeout before an RREQ is retried or dropped |
| `CONFIG_MESH_NOW_MAX_ROUTE_REQ_RETRIES` | 3 | Kconfig | RREQ re-broadcasts before giving up |
| `CONFIG_MESH_NOW_MAX_ROUTE_REQUESTS` | 8 | Kconfig | Simultaneous in-flight route requests |
| `CONFIG_MESH_NOW_RREQ_CACHE_SIZE` | 32 | Kconfig | RREQ dedup cache entries |
| `CONFIG_MESH_NOW_MAX_BEACON_NEIGHBORS` | 8 | Kconfig | One-hop peers advertised per beacon |
| `MAX_MESH_MESSAGE_LEN` | 128 | `mesh_now.h` | Maximum payload length in bytes |
| `MAX_PEERS` | 20 | `mesh_now.h` | Maximum number of tracked peers (one-hop) |
| `MAX_ENCRYPTION_KEY` | 16 | `mesh_now_internal.h` | Encryption key length (bytes) |

## FreeRTOS Task Configuration

| Parameter | Value |
| :-------- | :---- |
| Beacon task stack | 8192 bytes |
| Retransmit task stack | 8192 bytes |
| Task priority | 5 |
| Core affinity | Core 0 |

## Runtime Settings

### Encryption Key

```c
// Set at any time after init. The key must be exactly 16 bytes (AES-128).
uint8_t key[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                   0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
mesh_now_set_encryption_key(key, sizeof(key));
```

### Group Membership

```c
// Set at any time after init
mesh_now_set_group(42);
```

### Receive Callback

```c
// Set at any time after init
mesh_now_set_receive_callback(my_callback);
```

### Peer Management

```c
// Manual peer management (peers are auto-discovered via beacons)
mesh_now_add_peer(mac);
mesh_now_remove_peer(mac);
```

## Tuning Guide

All of these are Kconfig options, set in `idf.py menuconfig` under the `Mesh-NOW` menu (or via `sdkconfig.defaults`).

### Increase Range (Higher TTL)

```text
CONFIG_MESH_NOW_DEFAULT_ROUTE_TTL=5   # 5 hops instead of 3
```

::: callout warning title:"Trade-off"
Higher TTL increases range but also increases network load. Every hop re-broadcasts the message to all neighbors.
::: /callout

### Reduce Latency (Faster Beacons)

```text
CONFIG_MESH_NOW_BEACON_INTERVAL_MS=2000   # Beacon every 2 seconds
```

Faster beacons mean quicker peer discovery but more airtime usage.

### Increase Capacity (More Peers)

`MAX_PEERS` is a compile-time `#define` in `mesh_now.h`:

```c
#define MAX_PEERS 40  // Track up to 40 peers
```

Each peer entry uses ~32 bytes of RAM (address, activity flag, 8-byte timestamp, name). 40 peers = ~1280 bytes.

### Increase Reliability (More Retries)

```text
CONFIG_MESH_NOW_MAX_RETRIES=5   # 5 retries instead of 3
```

### Increase Throughput (Larger Payload)

::: callout warning title:"ESP-NOW Limit"
ESP-NOW frames are limited to 250 bytes. The `mesh_message_t` struct is ~172 bytes. Increasing `MAX_MESH_MESSAGE_LEN` beyond 128 bytes may exceed the ESP-NOW frame limit (the wire header is 32 bytes plus the MessagePack-typed payload).
::: /callout

## Platform-Specific Configs

Each ESP32 variant has optimized sdkconfig defaults in the chat app example:

| Target | Config File | Key Differences |
| :----- | :---------- | :-------------- |
| ESP32 | `sdkconfig.defaults.esp32` | Dual-core, 520KB RAM |
| ESP32-S2 | `sdkconfig.defaults.esp32s2` | Single-core, 320KB RAM |
| ESP32-S3 | `sdkconfig.defaults.esp32s3` | Dual-core, PSRAM support |
| ESP32-C3 | `sdkconfig.defaults.esp32c3` | RISC-V, BLE support |
| ESP32-C6 | `sdkconfig.defaults.esp32c6` | RISC-V, 802.15.4 support |

## Next Steps

::: grids
::: grid
::: button "API Reference" ../api/ icon:code
::: /grid

::: grid
::: button "Message Queue" ../api/message-queue.md icon:layers
::: /grid

::: grid
::: button "State Machine" ../protocol/state-machine.md icon:cpu
::: /grid
::: /grids
