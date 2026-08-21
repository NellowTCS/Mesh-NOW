---
title: "Configuration"
description: "All compile-time constants, tuning knobs, and runtime settings."
---

Mesh-NOW is configured via compile-time `#define` constants and runtime API calls.

## Compile-Time Constants

Defined in `mesh_now.c` and `mesh_now.h`:

| Constant | Default | File | Description |
| :------- | :------ | :--- | :---------- |
| `MAX_MESH_MESSAGE_LEN` | 128 | `mesh_now.h` | Maximum payload length in bytes |
| `DEFAULT_ROUTE_TTL` | 3 | `mesh_now.h` | Default hop count for messages |
| `MAX_PEERS` | 20 | `mesh_now.h` | Maximum number of tracked peers |
| `BEACON_INTERVAL_MS` | 5000 | `mesh_now.c` | Beacon broadcast interval (ms) |
| `RETRANSMIT_TIMEOUT_MS` | 2000 | `mesh_now.c` | Retransmit timeout (ms) |
| `MAX_PENDING_MESSAGES` | 16 | `mesh_now.c` | Pending message table size |
| `MAX_SEEN_MESSAGE_IDS` | 128 | `mesh_now.c` | Duplicate detection buffer size |
| `MAX_ENCRYPTION_KEY` | 32 | `mesh_now.c` | Maximum encryption key length (bytes) |
| `MAX_GROUP_ID` | 255 | `mesh_now.c` | Maximum group identifier |

## FreeRTOS Task Configuration

| Parameter | Value |
| :-------- | :---- |
| Beacon task stack | 4096 bytes |
| Retransmit task stack | 4096 bytes |
| Task priority | 5 |
| Core affinity | Core 0 |

## Runtime Settings

### Encryption Key

```c
// Set at any time after init
uint8_t key[] = {0x01, 0x02, 0x03, 0x04};
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

### Increase Range (Higher TTL)

```c
#define DEFAULT_ROUTE_TTL 5  // 5 hops instead of 3
```

::: callout warning title:"Trade-off"
Higher TTL increases range but also increases network load. Every hop re-broadcasts the message to all neighbors.
::: /callout

### Reduce Latency (Faster Beacons)

```c
#define BEACON_INTERVAL_MS 2000  // Beacon every 2 seconds
```

Faster beacons mean quicker peer discovery but more airtime usage.

### Increase Capacity (More Peers)

```c
#define MAX_PEERS 40  // Track up to 40 peers
```

Each peer uses ~6.5 bytes of RAM. 40 peers = ~260 bytes.

### Increase Reliability (More Retries)

In `retransmit_task`, change:

```c
if (pending->retries >= 5) {  // 5 retries instead of 3
```

### Increase Throughput (Larger Payload)

::: callout warning title:"ESP-NOW Limit"
ESP-NOW frames are limited to 250 bytes. The `mesh_message_t` struct is 154 bytes. Increasing `MAX_MESH_MESSAGE_LEN` beyond 128 bytes may exceed the ESP-NOW frame limit.
::: /callout

## Platform-Specific Configs

Each ESP32 variant has optimized sdkconfig defaults in the chat app example:

| Target | Config File | Key Differences |
| :----- | :---------- | :-------------- |
| ESP32 | `sdkconfig.esp32` | Dual-core, 520KB RAM |
| ESP32-S2 | `sdkconfig.esp32s2` | Single-core, 320KB RAM |
| ESP32-S3 | `sdkconfig.esp32s3` | Dual-core, PSRAM support |
| ESP32-C3 | `sdkconfig.esp32c3` | RISC-V, BLE support |
| ESP32-C6 | `sdkconfig.esp32c6` | RISC-V, 802.15.4 support |

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
