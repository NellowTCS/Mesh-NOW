---
title: "Mesh-NOW"
description: "Lightweight mesh networking protocol for ESP32 using ESP-NOW."
---

::: hero layout:split glow:true

# Mesh-NOW

Serverless mesh networking for ESP32. Device-to-device communication over ESP-NOW with automatic peer discovery, multi-hop routing, and payload encryption.

::: tag "ESP32"
::: tag "ESP-NOW"
::: tag "Mesh Network"

::: button "Quick Start" ./getting-started/quickstart.md icon:play
::: button "GitHub" external:<https://github.com/NellowTCS/Mesh-NOW> icon:github

== side

```c
#include "mesh_now.h"

// Initialize the mesh
mesh_now_init();

// Set receive callback
mesh_now_set_receive_callback(on_message);

// Send a broadcast message
mesh_now_send_broadcast("Hello, mesh!");

// Set encryption
uint8_t key[] = {0x01, 0x02, 0x03, 0x04};
mesh_now_set_encryption_key(key, sizeof(key));
```

::: /hero

## Features

::: grids
::: grid
::: card "Serverless" icon:wifi
Direct ESP32-to-ESP-NOW communication. No router, no internet, no infrastructure.
::: /card
::: /grid
::: grid
::: card "Auto Discovery" icon:search
Nodes broadcast beacons every 5 seconds. Peers are discovered and tracked automatically.
::: /card
::: /grid
::: grid
::: card "Multi-Hop Routing" icon:radio
Messages propagate through the mesh with configurable TTL (default 3 hops).
::: /card
::: /grid
::: grid
::: card "Reliable Delivery" icon:refresh-cw
ACK-based retransmission with 3 retries and 2-second timeout for direct messages.
::: /card
::: /grid
::: grid
::: card "Encrypted Payloads" icon:lock
Optional AES-128-GCM payload encryption with a 16-byte key.
::: /card
::: /grid
::: grid
::: card "7 Message Types" icon:layers
Beacon, chat, direct, ACK, group, presence, and typing indicators.
::: /card
::: /grid
::: /grids

## Supported Targets

| Target | Architecture | RAM | Status |
| :----- | :----------- | :-- | :----- |
| ESP32 | Dual-core Xtensa | 520KB | ::: tag "Supported" color:#22c55e |
| ESP32-S2 | Single-core Xtensa | 320KB | ::: tag "Supported" color:#22c55e |
| ESP32-S3 | Dual-core Xtensa | 512KB | ::: tag "Supported" color:#22c55e |
| ESP32-C3 | Single-core RISC-V | 400KB | ::: tag "Supported" color:#22c55e |
| ESP32-C6 | Single-core RISC-V | 512KB | ::: tag "Supported" color:#22c55e |

## Architecture

```mermaid
flowchart LR
    subgraph Node1["ESP32 Node 1"]
        A1[Application] --> B1[mesh_now library]
        B1 --> C1[ESP-NOW]
    end
    subgraph Node2["ESP32 Node 2"]
        A2[Application] --> B2[mesh_now library]
        B2 --> C2[ESP-NOW]
    end
    subgraph Node3["ESP32 Node 3"]
        A3[Application] --> B3[mesh_now library]
        B3 --> C3[ESP-NOW]
    end
    C1 <--> C2
    C2 <--> C3
    C1 <--> C3
```

::: grids
::: grid
::: button "Quick Start" ./getting-started/quickstart.md icon:play
::: /grid
::: grid
::: button "API Reference" ./api/ icon:code
::: /grid
::: grid
::: button "Protocol Spec" ./protocol/wire-format.md icon:file-code
::: /grid
::: /grids
