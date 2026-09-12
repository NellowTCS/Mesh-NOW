meta:
  id: mesh_now
  title: Mesh-NOW mesh networking protocol
  endian: le
  file-extension: bin

doc: |
  Binary mesh networking protocol using ESP-NOW.
  The header is always 32 bytes. The payload follows immediately.

  Multi-byte integer fields (message_id, reply_to, timestamp) are
  little-endian, matching native ESP32 byte order.

  hop_count is cumulative: the origin sends 0 and relays increment it,
  dropping the frame once it reaches hop_limit.

  The payload is either a raw MessagePack-encoded map (unencrypted) or
  an AES-128-GCM encrypted envelope. The inner MessagePack map carries
  only the message data (content and, for beacons, route requests, and
  route replies, node_name); all routing and identity metadata lives in
  the fixed header.

  Control frames (beacon, ack, rreq, rrep, rerr) are always unencrypted.

seq:
  - id: header
    type: header
  - id: payload
    type:
      switch-on: header.encrypted
      cases:
        true: encrypted_payload
        false: message

types:
  header:
    seq:
      - id: magic
        contents: [0x4d, 0x4e]
      - id: version
        type: u1
      - id: flags
        type: u1
      - id: msg_type
        type: u1
        enum: message_type
      - id: group_id
        type: u1
      - id: hop_limit
        type: u1
      - id: hop_count
        type: u1
      - id: message_id
        type: u4
      - id: reply_to
        type: u4
      - id: sender_mac
        size: 6
      - id: target_mac
        size: 6
      - id: timestamp
        type: u4
    instances:
      encrypted:
        value: (flags & 0x02) != 0
      has_node_name:
        value: (flags & 0x04) != 0

  encrypted_payload:
    doc: |
      AES-128-GCM encrypted envelope.
      Nonce: message_id (4 bytes LE) || sender_mac (6 bytes) ||
             fixed pad (2 bytes) = 12 bytes.
      Auth tag: 16 bytes appended after ciphertext.
      AAD covers: msg_type, sender_mac, target_mac, group_id, timestamp.
    seq:
      - id: nonce
        size: 12
      - id: ciphertext
        size: _io.size - 12 - 16
      - id: auth_tag
        size: 16

  message:
    doc: |
      MessagePack-encoded payload carrying only the message data.
      The inner map contains 'content' (str) and optionally 'node_name'
      (str, present in beacons when the HAS_NODE_NAME flag is set in the
      header).
    seq:
      - id: data
        size-eos: true

enums:
  message_type:
    0: beacon
    1: chat
    2: direct
    3: ack
    4: group
    5: presence
    6: typing
    7: rreq
    8: rrep
    9: rerr
