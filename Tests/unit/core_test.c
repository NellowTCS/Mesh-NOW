#include "mesh_now_internal.h"
#include "test.h"

static void init_deinit_round_trip(void)
{
    test_reset_all();
    CHECK(state_mutex == NULL);

    CHECK(mesh_now_init() == ESP_OK);
    CHECK(g_esp_now_init_calls == 1);
    CHECK(g_task_create_calls == 2);
    CHECK(g_last_broadcast_added);
    CHECK(beacon_task_handle != NULL);
    CHECK(retransmit_task_handle != NULL);
    CHECK(state_mutex != NULL);

    CHECK(mesh_now_deinit() == ESP_OK);
    CHECK(g_esp_now_deinit_calls == 1);
    CHECK(g_del_peer_calls >= 1);
    CHECK(state_mutex == NULL);
    CHECK(beacon_task_handle == NULL);
    CHECK(retransmit_task_handle == NULL);
}

static void name_get_set_truncate(void)
{
    test_reset_all();
    CHECK(mesh_now_set_name(NULL) == ESP_ERR_INVALID_ARG);
    CHECK_STREQ(mesh_now_get_name(), "");

    CHECK(mesh_now_set_name("alpha") == ESP_OK);
    CHECK_STREQ(mesh_now_get_name(), "alpha");

    const char *long_name = "a-very-long-node-name-past-16";
    CHECK(mesh_now_set_name(long_name) == ESP_OK);
    CHECK(strlen(mesh_now_get_name()) == MESH_NOW_NODE_NAME_MAX);
}

static void group_and_encryption_keys(void)
{
    test_reset_all();
    CHECK(mesh_now_get_group_id() == 0);
    CHECK(mesh_now_set_group(7) == ESP_OK);
    CHECK(mesh_now_get_group_id() == 7);
    CHECK(mesh_now_set_group(0) == ESP_OK);
    CHECK(mesh_now_get_group_id() == 0);

    uint8_t key[16] = {0};

    CHECK(mesh_now_set_encryption_key(NULL, 16) == ESP_ERR_INVALID_ARG);
    CHECK(mesh_now_set_encryption_key(key, 8) == ESP_ERR_INVALID_ARG);
    CHECK(!mesh_now_is_encrypted());
    CHECK(mesh_now_set_encryption_key(key, sizeof(key)) == ESP_OK);
    CHECK(mesh_now_is_encrypted());
}

static void message_id_seeds_and_increments(void)
{
    test_reset_all();
    // next_message_id == 0 forces seeding from esp_random() (the LCG).
    uint32_t first = mesh_now_generate_message_id();
    CHECK(first == 0x12345678u);
    CHECK(mesh_now_generate_message_id() == first + 1);
    CHECK(mesh_now_generate_message_id() == first + 2);
}

static void seen_cache_round_trip_and_shift(void)
{
    test_reset_all();
    CHECK(!mesh_now_is_message_seen(42));
    mesh_now_mark_message_seen(42);
    mesh_now_mark_message_seen(43);
    CHECK(mesh_now_is_message_seen(42));
    CHECK(mesh_now_is_message_seen(43));
    CHECK(!mesh_now_is_message_seen(44));

    // Overfill: the oldest id is evicted, the newest survives.
    test_reset_all();
    for (int i = 0; i < MAX_SEEN_MESSAGE_IDS; i++) {
        mesh_now_mark_message_seen((uint32_t)(1000 + i));
    }
    CHECK(seen_message_count == MAX_SEEN_MESSAGE_IDS);
    CHECK(mesh_now_is_message_seen(1000u));
    CHECK(mesh_now_is_message_seen(1000u + MAX_SEEN_MESSAGE_IDS - 1));

    mesh_now_mark_message_seen(9999);
    CHECK(!mesh_now_is_message_seen(1000u));
    CHECK(mesh_now_is_message_seen(9999));
    CHECK(seen_message_count == MAX_SEEN_MESSAGE_IDS);
}

static void time_sync_offset_and_average(void)
{
    test_reset_all();
    g_now_us = 2000000;
    mesh_now_sync_time(1500);
    CHECK(mesh_now_get_network_time_ms() == 1500);

    // Identical second sample: the moving average stays put.
    mesh_now_sync_time(1500);
    CHECK(mesh_now_get_network_time_ms() == 1500);

    // Diverging sample pulls the offset halfway toward the new estimate.
    g_now_us = 1000000;
    mesh_now_sync_time(1000);
    CHECK(mesh_now_get_network_time_ms() == 750);
}

static void peer_online_respects_expiry_window(void)
{
    test_reset_all();
    CHECK(!mesh_now_peer_is_online(NULL));

    peers[0].active = true;
    peers[0].last_seen = g_now_us;
    CHECK(mesh_now_peer_is_online(&peers[0]));

    peers[0].last_seen = g_now_us - PEER_EXPIRY_US - 1;
    CHECK(!mesh_now_peer_is_online(&peers[0]));

    peers[1].active = false;
    peers[1].last_seen = g_now_us;
    CHECK(!mesh_now_peer_is_online(&peers[1]));
}

void run_core_tests(void)
{
    printf("core\n");
    init_deinit_round_trip();
    name_get_set_truncate();
    group_and_encryption_keys();
    message_id_seeds_and_increments();
    seen_cache_round_trip_and_shift();
    time_sync_offset_and_average();
    peer_online_respects_expiry_window();
    printf("%3d passed so far\n", g_checks - g_failures);
}