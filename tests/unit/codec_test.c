#include "test.h"
#include "mesh_now_internal.h"

static uint8_t sender[6];
static uint8_t target[6];
static uint8_t neigh1[6];
static uint8_t neigh2[6];

static void mocks_for_codec_tests(void)
{
    test_reset_all();
    fill_mac(sender, 0x0101);
    fill_mac(target, 0x0202);
    fill_mac(neigh1, 0x0303);
    fill_mac(neigh2, 0x0404);
}

static mesh_message_t chat_fixture(void)
{
    mesh_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_TYPE_CHAT;
    msg.flags = MSG_FLAG_REQUIRES_ACK;
    msg.group_id = 3;
    msg.hop_limit = DEFAULT_ROUTE_TTL;
    msg.hop_count = 0;
    msg.message_id = 0xdeadbeef;
    msg.reply_to = 0x10203040;
    memcpy(msg.sender_mac, sender, ESP_NOW_ETH_ALEN);
    memcpy(msg.target_mac, target, ESP_NOW_ETH_ALEN);
    msg.timestamp = 123456;
    strncpy(msg.message, "hello mesh", sizeof(msg.message));
    return msg;
}

static void payload_round_trip_plain_message(void)
{
    mocks_for_codec_tests();
    mesh_message_t msg = chat_fixture();

    uint8_t buf[128];
    size_t len = mesh_now_encode(&msg, buf, sizeof(buf));
    CHECK(len > 0);

    mesh_message_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    CHECK(mesh_now_decode(buf, len, &decoded));
    CHECK_STREQ(decoded.message, "hello mesh");
    CHECK_STREQ(decoded.node_name, ""); // chat never carries a name
    CHECK(decoded.neighbor_count == 0);
}

static void encode_rejects_tiny_output(void)
{
    mocks_for_codec_tests();
    mesh_message_t msg = chat_fixture();
    uint8_t buf[4];
    CHECK(mesh_now_encode(&msg, buf, sizeof(buf)) == 0);
}

static void decode_rejects_garbage(void)
{
    mocks_for_codec_tests();
    uint8_t junk[3] = {0xc1, 0xc1, 0xff};
    mesh_message_t out;
    memset(&out, 0, sizeof(out));
    CHECK(!mesh_now_decode(junk, sizeof(junk), &out));
}

static void wire_round_trip_preserves_every_header_field(void)
{
    mocks_for_codec_tests();
    mesh_message_t msg = chat_fixture();

    uint8_t wire[WIRE_BUF_SIZE];
    size_t wire_len = 0;
    CHECK(mesh_now_prepare_wire(&msg, wire, &wire_len, false) == ESP_OK);

    uint8_t payload[WIRE_BUF_SIZE];
    size_t payload_len = mesh_now_encode(&msg, payload, sizeof(payload));
    CHECK(wire_len == MESH_NOW_HEADER_LEN + payload_len);

    mesh_message_t decoded;
    CHECK(mesh_now_decode_wire(wire, wire_len, &decoded));
    CHECK(decoded.type == MSG_TYPE_CHAT);
    CHECK(decoded.flags == MSG_FLAG_REQUIRES_ACK);
    CHECK(decoded.group_id == 3);
    CHECK(decoded.hop_limit == DEFAULT_ROUTE_TTL);
    CHECK(decoded.hop_count == 0);
    CHECK(decoded.message_id == msg.message_id);
    CHECK(decoded.reply_to == msg.reply_to);
    CHECK(decoded.timestamp == msg.timestamp);
    CHECK(macs_equal(decoded.sender_mac, sender));
    CHECK(macs_equal(decoded.target_mac, target));
    CHECK_STREQ(decoded.message, "hello mesh");
}

static void decode_wire_rejects_bad_frames(void)
{
    mocks_for_codec_tests();
    uint8_t wire[WIRE_BUF_SIZE];
    size_t wire_len = 0;
    mesh_message_t msg = chat_fixture();
    CHECK(mesh_now_prepare_wire(&msg, wire, &wire_len, false) == ESP_OK);

    mesh_message_t out;
    CHECK(mesh_now_decode_wire(wire, MESH_NOW_HEADER_LEN - 1, &out) == false);

    uint8_t bad_magic[64];
    memcpy(bad_magic, wire, 64);
    bad_magic[0] = 0x00;
    CHECK(mesh_now_decode_wire(bad_magic, 64, &out) == false);

    uint8_t bad_version[64];
    memcpy(bad_version, wire, 64);
    bad_version[2] = MESH_NOW_WIRE_VERSION + 1;
    CHECK(mesh_now_decode_wire(bad_version, 64, &out) == false);
}

static void beacon_neighbors_round_trip(void)
{
    mocks_for_codec_tests();
    mesh_now_set_name("alpha");

    mesh_message_t beacon;
    memset(&beacon, 0, sizeof(beacon));
    beacon.type = MSG_TYPE_BEACON;
    beacon.neighbor_count = 2;
    memcpy(beacon.neighbor_macs[0], neigh1, ESP_NOW_ETH_ALEN);
    memcpy(beacon.neighbor_macs[1], neigh2, ESP_NOW_ETH_ALEN);
    strncpy(beacon.neighbor_names[0], "north", sizeof(beacon.neighbor_names[0]));
    strncpy(beacon.neighbor_names[1], "south", sizeof(beacon.neighbor_names[1]));

    uint8_t buf[256];
    size_t len = mesh_now_encode(&beacon, buf, sizeof(buf));
    CHECK(len > 0);

    mesh_message_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    CHECK(mesh_now_decode(buf, len, &decoded));
    CHECK_STREQ(decoded.node_name, "alpha");
    CHECK(decoded.neighbor_count == 2);
    CHECK(macs_equal(decoded.neighbor_macs[0], neigh1));
    CHECK(macs_equal(decoded.neighbor_macs[1], neigh2));
    CHECK_STREQ(decoded.neighbor_names[0], "north");
    CHECK_STREQ(decoded.neighbor_names[1], "south");
}

static void type_classification(void)
{
    mocks_for_codec_tests();
    CHECK(mesh_now_is_control_type(MSG_TYPE_BEACON));
    CHECK(mesh_now_is_control_type(MSG_TYPE_ACK));
    CHECK(mesh_now_is_control_type(MSG_TYPE_ROUTE_REQUEST));
    CHECK(mesh_now_is_control_type(MSG_TYPE_ROUTE_REPLY));
    CHECK(mesh_now_is_control_type(MSG_TYPE_ROUTE_ERROR));
    CHECK(!mesh_now_is_control_type(MSG_TYPE_CHAT));
    CHECK(!mesh_now_is_control_type(MSG_TYPE_DIRECT));
    CHECK(!mesh_now_is_control_type(MSG_TYPE_GROUP));
    CHECK(!mesh_now_is_control_type(MSG_TYPE_PRESENCE));
    CHECK(!mesh_now_is_control_type(MSG_TYPE_TYPING));

    CHECK(mesh_now_carries_node_name(MSG_TYPE_BEACON));
    CHECK(mesh_now_carries_node_name(MSG_TYPE_ROUTE_REQUEST));
    CHECK(mesh_now_carries_node_name(MSG_TYPE_ROUTE_REPLY));
    CHECK(!mesh_now_carries_node_name(MSG_TYPE_CHAT));
    CHECK(!mesh_now_carries_node_name(MSG_TYPE_ACK));
}

static void encrypted_flag_round_trip(void)
{
    mocks_for_codec_tests();
    uint8_t key[AES_GCM_KEY_LEN] = {1, 2, 3, 4, 5, 6, 7, 8,
                                    9, 10, 11, 12, 13, 14, 15, 16};
    CHECK(mesh_now_set_encryption_key(key, sizeof(key)) == ESP_OK);

    mesh_message_t msg = chat_fixture();
    uint8_t wire[WIRE_BUF_SIZE];
    size_t wire_len = 0;
    CHECK(mesh_now_prepare_wire(&msg, wire, &wire_len, true) == ESP_OK);
    uint8_t payload[WIRE_BUF_SIZE];
    size_t payload_len = mesh_now_encode(&msg, payload, sizeof(payload));
    CHECK(wire_len == MESH_NOW_HEADER_LEN + AES_GCM_NONCE_LEN + payload_len +
                          AES_GCM_TAG_LEN);

    mesh_message_t decoded;
    CHECK(mesh_now_decode_wire(wire, wire_len, &decoded));
    CHECK_STREQ(decoded.message, "hello mesh");
    CHECK(decoded.flags == MSG_FLAG_REQUIRES_ACK); // ENCRYPTED is transmit-only

    // A receiver without the key must reject the same frame.
    encryption_enabled = false;
    encryption_key_len = 0;
    mesh_message_t out;
    CHECK(!mesh_now_decode_wire(wire, wire_len, &out));
}

void run_codec_tests(void)
{
    printf("codec\n");
    payload_round_trip_plain_message();
    encode_rejects_tiny_output();
    decode_rejects_garbage();
    wire_round_trip_preserves_every_header_field();
    decode_wire_rejects_bad_frames();
    beacon_neighbors_round_trip();
    type_classification();
    encrypted_flag_round_trip();
    printf("%3d passed so far\n", g_checks - g_failures);
}