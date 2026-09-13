#include "test.h"
#include "mesh_now_internal.h"

static uint8_t p1[6], p2[6], p3[6];

static void mocks_for_peer_tests(void)
{
    test_reset_all();
    fill_mac(p1, 0x2001);
    fill_mac(p2, 0x2002);
    fill_mac(p3, 0x2003);
}

static void add_records_and_registers(void)
{
    mocks_for_peer_tests();
    mesh_now_add_peer(p1);
    CHECK(mesh_now_get_peer_count() == 1);
    CHECK(g_add_peer_calls == 1);
    CHECK(mesh_now_peer_is_direct(p1));
    CHECK(!mesh_now_peer_is_direct(p2));
    CHECK(peers[0].active);
    CHECK(peers[0].last_seen == g_now_us);
}

static void self_mac_is_never_a_peer(void)
{
    mocks_for_peer_tests();
    mesh_now_add_peer(g_local_mac);
    CHECK(mesh_now_get_peer_count() == 0);
    CHECK(g_add_peer_calls == 0);
}

static void duplicate_reactivate_skips_re_register(void)
{
    mocks_for_peer_tests();
    mesh_now_add_peer(p1);
    peers[0].active = false; // peer expired earlier
    g_now_us += 5000;

    g_is_peer_exist = true; // still registered with ESP-NOW
    mesh_now_add_peer(p1);
    CHECK(mesh_now_get_peer_count() == 1);
    CHECK(peers[0].active);
    CHECK(peers[0].last_seen == g_now_us);
    CHECK(g_add_peer_calls == 1); // no second esp_now_add_peer
}

static void full_table_refuses_new_peers(void)
{
    mocks_for_peer_tests();
    for (int i = 0; i < MAX_PEERS; i++) {
        uint8_t mac[6];
        fill_mac(mac, 0x2100 + i);
        mesh_now_add_peer(mac);
    }
    CHECK(mesh_now_get_peer_count() == MAX_PEERS);

    mesh_now_add_peer(p1);
    CHECK(mesh_now_get_peer_count() == MAX_PEERS);
    CHECK(mesh_now_peer_is_direct(p1) == false);
}

static void esp_now_reject_prevents_registration(void)
{
    mocks_for_peer_tests();
    g_add_peer_rc = ESP_FAIL;
    mesh_now_add_peer(p1);
    CHECK(mesh_now_get_peer_count() == 0);
}

static void remove_drops_entry_and_keeps_order(void)
{
    mocks_for_peer_tests();
    mesh_now_add_peer(p1);
    mesh_now_add_peer(p2);
    mesh_now_add_peer(p3);

    mesh_now_remove_peer(p2);
    CHECK(mesh_now_get_peer_count() == 2);
    CHECK(macs_equal(peers[0].peer_addr, p1));
    CHECK(macs_equal(peers[1].peer_addr, p3));
    CHECK(macs_equal(g_last_del_peer, p2));
    CHECK(g_del_peer_calls == 1);
    CHECK(!mesh_now_peer_is_direct(p2));

    // Removing an unknown peer is a no-op.
    mesh_now_remove_peer(p2);
    CHECK(mesh_now_get_peer_count() == 2);
    CHECK(g_del_peer_calls == 1);
}

static void expire_deregisters_and_compacts(void)
{
    mocks_for_peer_tests();
    mesh_now_add_peer(p1);
    mesh_now_add_peer(p2);
    mesh_now_add_peer(p3);
    g_now_us += PEER_EXPIRY_US + 1;

    mesh_now_expire_peers(g_now_us);
    CHECK(mesh_now_get_peer_count() == 0);
    CHECK(g_del_peer_calls == 3);
    CHECK(!mesh_now_peer_is_direct(p1));
}

static void expire_invalidates_routes_and_sends_rerr(void)
{
    mocks_for_peer_tests();
    mesh_now_add_peer(p1);
    mesh_now_add_route(p2, p1, 1, 0);

    g_now_us += PEER_EXPIRY_US + 1;
    mesh_now_expire_peers(g_now_us);

    mesh_route_t out;
    CHECK(!mesh_now_get_route(p2, &out));
    CHECK(g_send_rerr_calls == 1);
}

static void snapshot_copies_up_to_max_out(void)
{
    mocks_for_peer_tests();
    mesh_now_add_peer(p1);
    mesh_now_add_peer(p2);

    mesh_peer_t out[1];
    CHECK(mesh_now_snapshot_peers(out, 1) == 1);
    CHECK(macs_equal(out[0].peer_addr, p1));

    mesh_peer_t full[2];
    CHECK(mesh_now_snapshot_peers(full, 2) == 2);
    CHECK(macs_equal(full[1].peer_addr, p2));
}

void run_peer_tests(void)
{
    printf("peer\n");
    add_records_and_registers();
    self_mac_is_never_a_peer();
    duplicate_reactivate_skips_re_register();
    full_table_refuses_new_peers();
    esp_now_reject_prevents_registration();
    remove_drops_entry_and_keeps_order();
    expire_deregisters_and_compacts();
    expire_invalidates_routes_and_sends_rerr();
    snapshot_copies_up_to_max_out();
    printf("%3d passed so far\n", g_checks - g_failures);
}