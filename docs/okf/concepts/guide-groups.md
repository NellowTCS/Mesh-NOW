---
type: concept
title: Groups
description: "Group messaging with configurable group IDs."
source: "https://NellowTCS.github.io/Mesh-NOW/guide/groups/"
path: /guide/groups/
updated: 2026-09-15
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-09-15T21:27:42.526Z"
---
---
title: "Groups"
description: "Group messaging with configurable group IDs."
---

Mesh-NOW supports group-scoped messaging. Only nodes that belong to the group receive the message; everyone else ignores it for delivery purposes.

## Setting a Group

```c
// Join group 42
mesh_now_set_group(42);

// Switch to group 0 (default / no group)
mesh_now_set_group(0);
```

The group ID is a single byte (`uint8_t`), so there are 255 usable groups. Group 0 is the default and means "no group filter."

## Sending Group Messages

```c
// Send to group 42
mesh_now_send_group(42, "Hello, group!");

// Any node with group_id == 42 will receive this
```

## Receiving Group Messages

When a `MSG_TYPE_GROUP` message arrives, the library checks the local group ID:

```c
else if (mesh_msg.type == MSG_TYPE_GROUP)
{
    if (local_group_id != 0 && mesh_msg.group_id == local_group_id) {
        deliver(&mesh_msg);  // callback or message queue
    }

    // Relay regardless of group membership.
    if (mesh_now_can_relay(&mesh_msg)) {
        mesh_now_relay_broadcast(&mesh_msg, true);
    }
}
```

::: callout warning title:"Relay Behavior"
All nodes relay group messages even when they are not in the group. Group filtering only applies to callback/queue delivery, not to forwarding. That is what lets a message reach its group through intermediate nodes that do not belong to it.
::: /callout

## Group ID Reference

| Value | Meaning                 |
| ----- | ----------------------- |
| 0     | No group (default)      |
| 1-255 | Active group membership |

## Use Cases

- Department channels: each department gets a group ID
- Event coordination: temporary groups for events
- Access control: nodes only process messages for their group
- Multi-tenant meshes: shared infrastructure, isolated communication

## Limitations

- A node can only be in one group at a time
- Group membership is local (not advertised to peers)
- There is no group discovery or registration protocol

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
