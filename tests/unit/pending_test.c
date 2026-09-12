#include "test.h"
#include "mesh_now_internal.h"

static uint8_t dest_hop[6];
static uint8_t remote_a[6];
static uint8_t remote_b[6];

static void mocks_for_pending_tests(void)
{
    test_reset_all();
    fill_mac(dest_hop, 0x4001);
    fill_mac(remote_a, 0x3001);
    fill_mac(remote_b, 0x3002);
}

// Encode a chat message into a real plaintext wire frame.
static size_t build_wire(uint32_t message_id, uint8_t *wire, size_t cap)
{
    mesh_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_TYPE_CHAT;
    msg.group_id = 0;
    msg.hop_limit = DEFAULT_ROUTE_TTL;
    msg.hop_count = 0;
    msg.message_id = message_id;
    memcpy(msg.sender_mac, g_local_mac, ESP_NOW_ETH_ALEN);
    memcpy(msg.target_mac, remote_a, ESP_NOW_ETH_ALEN);
    strncpy(msg.message, "payload", sizeof(msg.message));

    size_t wire_len = 0;
    CHECK(mesh_now_prepare_wire(&msg, wire, &wire_len, false) == ESP_OK);
    return wire_len;
}

static void add_find_release(void)
{
    mocks_for_pending_tests();
    uint8_t wire[WIRE_BUF_SIZE];
    size_t wire_len = build_wire(900, wire, sizeof(wire));

    int idx = mesh_now_add_pending(dest_hop, remote_a, wire, wire_len, 900,
                                   MSG_FLAG_REQUIRES_ACK, true);
    CHECK(idx >= 0);
    CHECK(idx < MAX_PENDING_MESSAGES);
    CHECK(mesh_now_find_pending(900) == idx);

    pending_message_t *slot = &pending_messages[idx];
    CHECK(slot->active);
    CHECK(slot->route_wait);
    CHECK(slot->retries == 0);
    CHECK(slot->wire_len == wire_len);
    CHECK(memcmp(slot->wire_buf, wire, wire_len) == 0);
    CHECK(macs_equal(slot->dest_mac, dest_hop));
    CHECK(macs_equal(slot->remote_dest, remote_a));
    CHECK(slot->last_send_time_ms == g_now_us / 1000);

    CHECK(mesh_now_find_pending(901) == -1);

    mesh_now_release_pending(idx);
    CHECK(mesh_now_find_pending(900) == -1);
    CHECK(!pending_messages[idx].active);

    mesh_now_release_pending(-1); // out of range is a no-op
}

static void full_buffer_refuses_extra_slots(void)
{
    mocks_for_pending_tests();
    uint8_t wire[WIRE_BUF_SIZE];
    for (uint32_t i = 0; i < MAX_PENDING_MESSAGES; i++) {
        size_t len = build_wire(1000 + i, wire, sizeof(wire));
        CHECK(mesh_now_add_pending(dest_hop, remote_a, wire, len, 1000 + i,
                                   MSG_FLAG_REQUIRES_ACK, true) >= 0);
    }
    size_t len = build_wire(9999, wire, sizeof(wire));
    CHECK(mesh_now_add_pending(dest_hop, remote_a, wire, len, 9999,
                               MSG_FLAG_REQUIRES_ACK, true) == -1);
}

static void flush_delivers_to_route_next_hop(void)
{
    mocks_for_pending_tests();
    mesh_now_add_route(remote_a, dest_hop, 1, 0);

    uint8_t wire[WIRE_BUF_SIZE];
    mesh_now_add_pending(dest_hop, remote_a, wire,
                         build_wire(1, wire, sizeof(wire)), 1,
                         MSG_FLAG_REQUIRES_ACK, true);
    mesh_now_add_pending(dest_hop, remote_a, wire,
                         build_wire(2, wire, sizeof(wire)), 2,
                         MSG_FLAG_REQUIRES_ACK, true);
    mesh_now_add_pending(dest_hop, remote_b, wire,
                         build_wire(3, wire, sizeof(wire)), 3,
                         MSG_FLAG_REQUIRES_ACK, true);

    mesh_now_flush_route_wait(remote_a);

    // Two frames toward the discovered next hop, none for the other target.
    CHECK(g_send_calls == 2);
    CHECK(macs_equal(g_send.dest, dest_hop));

    int idx1 = mesh_now_find_pending(1);
    int idx2 = mesh_now_find_pending(2);
    int idx3 = mesh_now_find_pending(3);
    CHECK(idx1 >= 0 && idx2 >= 0 && idx3 >= 0);
    CHECK(!pending_messages[idx1].route_wait);
    CHECK(!pending_messages[idx2].route_wait);
    CHECK(macs_equal(pending_messages[idx1].dest_mac, dest_hop));
    CHECK(macs_equal(pending_messages[idx2].dest_mac, dest_hop));
    CHECK(pending_messages[idx3].route_wait); // untouched
    CHECK(macs_equal(pending_messages[idx3].dest_mac, dest_hop));
}

static void flush_without_route_does_nothing(void)
{
    mocks_for_pending_tests();
    uint8_t wire[WIRE_BUF_SIZE];
    mesh_now_add_pending(dest_hop, remote_a, wire,
                         build_wire(5, wire, sizeof(wire)), 5,
                         MSG_FLAG_REQUIRES_ACK, true);

    mesh_now_flush_route_wait(remote_a);
    CHECK(g_send_calls == 0);
    CHECK(pending_messages[0].route_wait);
}

void run_pending_tests(void)
{
    printf("pending\n");
    add_find_release();
    full_buffer_refuses_extra_slots();
    flush_delivers_to_route_next_hop();
    flush_without_route_does_nothing();
    printf("%3d passed so far\n", g_checks - g_failures);
}