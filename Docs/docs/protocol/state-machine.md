---
title: "State Machine"
description: "Node lifecycle, message states, and peer states with Mermaid diagrams."
---

Mesh-NOW's behavior can be modeled as a set of interacting state machines.

## Node Lifecycle

```mermaid
stateDiagram-v2
    [*] --> Off
    Off --> Initializing: mesh_now_init()
    Initializing --> Running: ESP-NOW ready, tasks started
    Initializing --> Error: init failed
    Error --> [*]
    Running --> Deinitializing: mesh_now_deinit()
    Deinitializing --> Off: cleanup complete
```

### States

| State | Description |
| :---- | :---------- |
| **Off** | No mesh activity. ESP-NOW not initialized. |
| **Initializing** | `mesh_now_init()` executing. Registering callbacks, adding broadcast peer, creating tasks. |
| **Running** | Normal operation. Beacon task broadcasting, retransmit task polling, receive callback registered. |
| **Error** | Initialization failed (ESP-NOW init, callback registration, or task creation). |
| **Deinitializing** | `mesh_now_deinit()` executing. Deleting tasks, removing peers, unregistering callbacks. |

## Message State Machine

### Broadcast Message (Chat, Group, Presence)

```mermaid
stateDiagram-v2
    [*] --> Created: application calls send
    Created --> Sent: esp_now_send() OK
    Sent --> Delivered: received by peer(s)
    Sent --> Lost: not received (out of range)
    Delivered --> [*]
    Lost --> [*]
```

### Direct Message (with ACK)

```mermaid
stateDiagram-v2
    [*] --> Created: application calls send_direct
    Created --> Pending: esp_now_send() OK, queued for retransmit
    Pending --> Retransmitting: timeout (2s) without ACK
    Retransmitting --> Pending: retry < 3, resend
    Retransmitting --> Dropped: retry >= 3
    Pending --> Cleared: ACK received
    Pending --> Cleared: ACK received via relay
    Cleared --> [*]
    Dropped --> [*]
```

### ACK Message

```mermaid
stateDiagram-v2
    [*] --> Generated: received direct message
    Generated --> Sent: esp_now_send() OK
    Sent --> Relaying: intermediate node, target != self
    Sent --> Received: target == self
    Relaying --> Received: reaches target
    Received --> [*]: clears pending message
```

## Peer States

```mermaid
stateDiagram-v2
    [*] --> Unknown
    Unknown --> Discovered: beacon or message received
    Discovered --> Active: peer_count incremented
    Active --> Removed: mesh_now_remove_peer()
    Removed --> [*]
    Active --> Lost: node reboots (implicit)
    Lost --> [*]
```

### Peer Lifecycle

| Transition | Trigger | Action |
| :--------- | :------ | :----- |
| Unknown -> Discovered | Beacon or message received from MAC | `mesh_now_add_peer()` called |
| Discovered -> Active | MAC not in peer table, not self, not duplicate | Added to ESP-NOW subsystem and local table |
| Active -> Removed | `mesh_now_remove_peer()` called | Removed from ESP-NOW subsystem and local table |
| Active -> Lost | Node reboots | Peer table cleared on boot |

## Message Processing Flow

```mermaid
flowchart TB
    A[ESP-NOW receive callback] --> B{Valid length?}
    B -->|No| C[Drop]
    B -->|Yes| D[Decrypt if encrypted]
    D --> E{Message type?}
    E -->|Beacon| F[Add peer]
    E -->|ACK| G{For us?}
    G -->|Yes| H[Clear pending message]
    G -->|No| I[Relay if hop > 0]
    E -->|Chat| J[Add peer, invoke callback/queue]
    J --> K[Relay if hop > 0]
    E -->|Direct| L{For us?}
    L -->|Yes| M[Add peer, send ACK, invoke callback]
    L -->|No| N[Relay if hop > 0]
    E -->|Group| O{Group match?}
    O -->|Yes| P[Invoke callback/queue]
    O -->|No| Q[Skip delivery]
    P --> R[Relay if hop > 0]
    Q --> R
    E -->|Presence| S[Add peer, invoke callback]
    S --> T[Relay if hop > 0]
    E -->|Typing| U{For us?}
    U -->|Yes| V[Invoke callback]
    U -->|No| W[Relay if hop > 0]
```

## Timing Constants

| Event | Interval | State Impact |
| :---- | :------- | :----------- |
| Beacon broadcast | Every 5000ms | Triggers peer discovery at receivers |
| Retransmit poll | Every 500ms | Checks pending messages for timeout |
| Retransmit timeout | 2000ms | Moves pending message to retransmit state |
| Max retries | 3 | Drops message after 3 failed retransmits |

## Next Steps

::: grids
::: grid
::: button "Wire Format" ./wire-format.md icon:file-code
::: /grid

::: grid
::: button "Routing" ../guide/routing.md icon:radio
::: /grid

::: grid
::: button "Reliability" ../guide/reliability.md icon:refresh-cw
::: /grid
::: /grids
