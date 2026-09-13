#include "test.h"
#include "mesh_now_internal.h"

static uint8_t dest[6];

static void mocks_for_tx_tests(void)
{
    test_reset_all();
    fill_mac(dest, 0x4001);
}

static void can_relay_boundary(void)
{
    mocks_for_tx_tests();
    mesh_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.hop_limit = 3;
    msg.hop_count = 0;
    CHECK(mesh_now_can_relay(&msg)); // 1 < 3
    msg.hop_count = 1;
    CHECK(mesh_now_can_relay(&msg)); // 2 < 3
    msg.hop_count = 2;
    CHECK(!mesh_now_can_relay(&msg)); // 3 !< 3

    msg.hop_count = 0;
    msg.hop_limit = 1;
    CHECK(!mesh_now_can_relay(&msg));
    msg.hop_limit = 0;
    CHECK(!mesh_now_can_relay(&msg));
}

static void mac_str_formats_colon_hex(void)
{
    mocks_for_tx_tests();
    uint8_t mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    CHECK_STREQ(mesh_now_mac_str(mac), "01:02:03:04:05:06");
    CHECK_STREQ(mesh_now_mac_str(g_local_mac), "dc:fe:01:02:03:04");
}

static void send_frame_small_frame_ok(void)
{
    mocks_for_tx_tests();
    mesh_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_TYPE_CHAT;
    msg.hop_limit = 3;
    msg.message_id = 77;
    memcpy(msg.sender_mac, g_local_mac, ESP_NOW_ETH_ALEN);
    memcpy(msg.target_mac, dest, ESP_NOW_ETH_ALEN);
    strncpy(msg.message, "ping", sizeof(msg.message));

    CHECK(mesh_now_send_frame(&msg, dest, false) == ESP_OK);
    CHECK(g_send.called);
    CHECK(macs_equal(g_send.dest, dest));
    CHECK(g_send.len == MESH_NOW_HEADER_LEN + mesh_now_encode(&msg, g_send.data, sizeof(g_send.data)));
}

static void send_frame_drops_oversized(void)
{
    mocks_for_tx_tests();
    mesh_now_set_name("0123456789abcdef");

    mesh_message_t beacon;
    memset(&beacon, 0, sizeof(beacon));
    beacon.type = MSG_TYPE_BEACON;
    beacon.neighbor_count = MAX_BEACON_NEIGHBORS;
    strncpy(beacon.message, "MESH-NOW-BEACON", sizeof(beacon.message));
    for (int i = 0; i < MAX_BEACON_NEIGHBORS; i++) {
        uint8_t mac[6];
        fill_mac(mac, 0x7000 + i);
        memcpy(beacon.neighbor_macs[i], mac, ESP_NOW_ETH_ALEN);
        for (int c = 0; c < MESH_NOW_NODE_NAME_MAX; c++) {
            beacon.neighbor_names[i][c] = (char)('a' + (i + c) % 26);
        }
        beacon.neighbor_names[i][MESH_NOW_NODE_NAME_MAX] = '\0';
    }

    // The uncompressed zone announce is too big for one ESP-NOW frame.
    uint8_t wire[WIRE_BUF_SIZE];
    size_t wire_len = 0;
    CHECK(mesh_now_prepare_wire(&beacon, wire, &wire_len, false) == ESP_OK);
    CHECK(wire_len > ESP_NOW_MAX_DATA_LEN);

    CHECK(mesh_now_send_frame(&beacon, broadcast_mac, false) ==
          ESP_ERR_INVALID_SIZE);
    CHECK(!g_send.called);
}

static void relay_bumps_hop_count_toward_dest(void)
{
    mocks_for_tx_tests();
    mesh_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_TYPE_CHAT;
    msg.hop_limit = 3;
    msg.hop_count = 0;
    msg.message_id = 88;
    memcpy(msg.sender_mac, g_local_mac, ESP_NOW_ETH_ALEN);
    memcpy(msg.target_mac, dest, ESP_NOW_ETH_ALEN);
    strncpy(msg.message, "fwd", sizeof(msg.message));

    CHECK(mesh_now_relay_unicast(&msg, dest, false) == ESP_OK);
    CHECK(g_send.called);
    CHECK(macs_equal(g_send.dest, dest));
    CHECK(msg.hop_count == 0); // source copy untouched

    mesh_message_t decoded;
    CHECK(mesh_now_decode_wire(g_send.data, g_send.len, &decoded));
    CHECK(decoded.hop_count == 1);
    CHECK(decoded.message_id == 88);
}

static void relay_respects_ttl_boundary(void)
{
    mocks_for_tx_tests();
    mesh_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.hop_limit = 3;
    msg.hop_count = 2; // relaying would exceed the budget
    msg.message_id = 89;

    CHECK(mesh_now_relay_unicast(&msg, dest, false) == ESP_OK);
    CHECK(!g_send.called);

    CHECK(mesh_now_relay_broadcast(&msg, false) == ESP_OK);
    CHECK(!g_send.called);
}

static void broadcast_relay_targets_broadcast_mac(void)
{
    mocks_for_tx_tests();
    mesh_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.hop_limit = 3;
    msg.hop_count = 0;
    msg.message_id = 90;

    CHECK(mesh_now_relay_broadcast(&msg, false) == ESP_OK);
    CHECK(g_send.called);
    uint8_t ff[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    CHECK(macs_equal(g_send.dest, ff));
}

void run_tx_tests(void)
{
    printf("tx\n");
    can_relay_boundary();
    mac_str_formats_colon_hex();
    send_frame_small_frame_ok();
    send_frame_drops_oversized();
    relay_bumps_hop_count_toward_dest();
    relay_respects_ttl_boundary();
    broadcast_relay_targets_broadcast_mac();
    printf("%3d passed so far\n", g_checks - g_failures);
}