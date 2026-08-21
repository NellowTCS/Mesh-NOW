---
title: "Groups"
description: "Group messaging with configurable group IDs."
---

Mesh-NOW supports group-scoped messaging where only nodes belonging to a specific group receive messages.

## Setting a Group

```c
// Join group 42
mesh_now_set_group(42);

// Switch to group 0 (default / no group)
mesh_now_set_group(0);
```

The group ID is a single byte (`uint8_t`), supporting groups 0-255. Group 0 is the default and means "no group filter."

## Sending Group Messages

```c
// Send to group 42
mesh_now_send_group(42, "Hello, group!");

// Any node with group_id == 42 will receive this
```

## Receiving Group Messages

When a `MSG_TYPE_GROUP` message arrives, the library checks the group ID:

```c
else if (mesh_msg.type == MSG_TYPE_GROUP)
{
    mesh_now_add_peer(mesh_msg.sender_mac);

    if (mesh_msg.group_id == local_group_id) {
        if (receive_callback) {
            receive_callback(&mesh_msg);
        } else {
            message_queue_send(&msg);
        }
    }

    // Relay regardless of group membership
    if (mesh_msg.hop_count > 0) {
        mesh_now_route_message(&mesh_msg);
    }
}
```

::: callout warning title:"Relay Behavior"
All nodes relay group messages regardless of their own group membership. Only the callback/queue delivery is filtered by group ID. This ensures messages reach their intended group even through non-group intermediate nodes.
::: /callout

## Group ID Reference

| Value | Meaning |
| :---- | :------ |
| 0 | No group (default) |
| 1-255 | Active group membership |

## Use Cases

- **Department channels**: Each department gets a group ID
- **Event coordination**: Temporary groups for events
- **Access control**: Nodes only process messages for their group
- **Multi-tenant meshes**: Shared infrastructure, isolated communication

## Limitations

- A node can only be in **one group at a time**
- Group membership is **local** (not advertised to peers)
- No group discovery or registration protocol

::: grids
::: grid
::: button "Message Format" ./message-format.md icon:file-text
::: /grid

::: grid
::: button "Encryption" ./encryption.md icon:lock
::: /grid

::: grid
::: button "Configuration" ./configuration.md icon:settings
::: /grid
::: /grids
