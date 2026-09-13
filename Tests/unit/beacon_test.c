#include "test.h"
#include "mesh_now_internal.h"

#define NAME_16 "abcdefghijklmnop"

static void register_dense_peers(void)
{
    for (int i = 0; i < MAX_BEACON_NEIGHBORS; i++) {
        uint8_t mac[6];
        fill_mac(mac, 0x7000 + i);
        mesh_now_add_peer(mac);
        strncpy(peers[i].node_name, NAME_16, MESH_NOW_NODE_NAME_MAX);
        peers[i].node_name[MESH_NOW_NODE_NAME_MAX] = '\0';
    }
    CHECK(mesh_now_get_peer_count() == MAX_BEACON_NEIGHBORS);
}

static void uncompressed_zone_announce_overflows_frame_budget(void)
{
    test_reset_all();
    mesh_now_set_name(NAME_16);

    mesh_message_t beacon;
    memset(&beacon, 0, sizeof(beacon));
    beacon.type = MSG_TYPE_BEACON;
    beacon.neighbor_count = MAX_BEACON_NEIGHBORS;
    strncpy(beacon.message, "MESH-NOW-BEACON", sizeof(beacon.message));
    register_dense_peers();
    for (int i = 0; i < MAX_BEACON_NEIGHBORS; i++) {
        uint8_t mac[6];
        fill_mac(mac, 0x7000 + i);
        memcpy(beacon.neighbor_macs[i], mac, ESP_NOW_ETH_ALEN);
        strncpy(beacon.neighbor_names[i], NAME_16,
                sizeof(beacon.neighbor_names[i]));
    }

    // Payload budget after the 32-byte header
    size_t payload_budget = ESP_NOW_MAX_DATA_LEN - MESH_NOW_HEADER_LEN;
    uint8_t wire[WIRE_BUF_SIZE];
    size_t len = mesh_now_encode(&beacon, wire, sizeof(wire));
    CHECK(len > payload_budget);

    // Trimming must drop the count until the encode fits.
    bool fits = false;
    for (int n = MAX_BEACON_NEIGHBORS; n >= 0 && !fits; n--) {
        beacon.neighbor_count = (uint8_t)n;
        len = mesh_now_encode(&beacon, wire, sizeof(wire));
        fits = (len != 0 && len <= payload_budget);
    }
    CHECK(fits);
}

static void announce_dense_mesh_fits_in_one_frame(void)
{
    test_reset_all();
    mesh_now_set_name(NAME_16);
    register_dense_peers();

    CHECK(mesh_now_announce_name() == ESP_OK);
    CHECK(g_send.called);
    CHECK(g_send.len <= ESP_NOW_MAX_DATA_LEN);

    mesh_message_t decoded;
    CHECK(mesh_now_decode_wire(g_send.data, g_send.len, &decoded));
    CHECK(decoded.type == MSG_TYPE_BEACON);
    CHECK(decoded.hop_limit == 1);
    CHECK(decoded.hop_count == 0);
    CHECK_STREQ(decoded.message, "MESH-NOW-BEACON");
    CHECK_STREQ(decoded.node_name, NAME_16);
    CHECK(decoded.neighbor_count > 0);
    CHECK(decoded.neighbor_count < MAX_BEACON_NEIGHBORS);

    // The announce marks its own id as seen so relays ignore the echo.
    CHECK(mesh_now_is_message_seen(decoded.message_id));
    CHECK(macs_equal(decoded.sender_mac, g_local_mac));
}

static void announce_empty_zone_is_still_valid_beacon(void)
{
    test_reset_all();
    mesh_now_set_name("lonely");

    CHECK(mesh_now_announce_name() == ESP_OK);
    CHECK(g_send.called);
    CHECK(g_send.len <= ESP_NOW_MAX_DATA_LEN);

    mesh_message_t decoded;
    CHECK(mesh_now_decode_wire(g_send.data, g_send.len, &decoded));
    CHECK(decoded.type == MSG_TYPE_BEACON);
    CHECK(decoded.neighbor_count == 0);
}

void run_beacon_tests(void)
{
    printf("beacon\n");
    uncompressed_zone_announce_overflows_frame_budget();
    announce_dense_mesh_fits_in_one_frame();
    announce_empty_zone_is_still_valid_beacon();
    printf("%3d passed so far\n", g_checks - g_failures);
}