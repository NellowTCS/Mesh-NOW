#include "test.h"
#include "mesh_now_internal.h"

static uint8_t dest_a[6], dest_b[6], dest_c[6], dest_d[6];
static uint8_t hop_x[6], hop_y[6];

static void mocks_for_route_tests(void)
{
    test_reset_all();
    fill_mac(dest_a, 0x3001);
    fill_mac(dest_b, 0x3002);
    fill_mac(dest_c, 0x3003);
    fill_mac(dest_d, 0x3004);
    fill_mac(hop_x, 0x4001);
    fill_mac(hop_y, 0x4002);
}

static void add_read_and_update(void)
{
    mocks_for_route_tests();
    CHECK(mesh_now_get_route_count() == 0);

    mesh_now_add_route(dest_a, hop_x, 2, 11);
    mesh_route_t out;
    CHECK(mesh_now_get_route(dest_a, &out));
    CHECK(out.active);
    CHECK(!out.pinned);
    CHECK(out.hop_count == 2);
    CHECK(out.dest_seq == 11);
    CHECK(macs_equal(out.next_hop, hop_x));
    CHECK_STREQ(out.node_name, "");

    // Same destination updates in place instead of appending.
    mesh_now_add_route(dest_a, hop_y, 1, 12);
    CHECK(mesh_now_get_route_count() == 1);
    CHECK(mesh_now_get_route(dest_a, &out));
    CHECK(out.hop_count == 1);
    CHECK(macs_equal(out.next_hop, hop_y));

    CHECK(!mesh_now_get_route(dest_b, &out));
}

static void self_mac_is_never_a_route(void)
{
    mocks_for_route_tests();
    // g_local_mac is dc:fe:01:02:03:04; fill_mac seeds never collide with it.
    mesh_now_add_route(g_local_mac, hop_x, 1, 0);
    CHECK(mesh_now_get_route_count() == 0);
}

static void full_table_refuses_new_routes(void)
{
    mocks_for_route_tests();
    for (int i = 0; i < MAX_ROUTES; i++) {
        uint8_t dest[6];
        uint8_t hop[6];
        fill_mac(dest, 0x5000 + i);
        fill_mac(hop, 0x6000 + i);
        mesh_now_add_route(dest, hop, 1, 0);
    }
    CHECK(mesh_now_get_route_count() == MAX_ROUTES);

    uint8_t extra[6];
    fill_mac(extra, 0x5999);
    mesh_now_add_route(extra, hop_x, 1, 0);
    CHECK(mesh_now_get_route_count() == MAX_ROUTES);
    mesh_route_t out;
    CHECK(!mesh_now_get_route(extra, &out));
}

static void pin_and_unpin_lifecycle(void)
{
    mocks_for_route_tests();
    CHECK(mesh_now_pin_route(dest_a, hop_x) == ESP_OK);
    mesh_route_t out;
    CHECK(mesh_now_get_route(dest_a, &out));
    CHECK(out.active);
    CHECK(out.pinned);
    CHECK(out.hop_count == 1);
    CHECK(macs_equal(out.next_hop, hop_x));

    // self-referential routes are invalid
    CHECK(mesh_now_pin_route(g_local_mac, hop_x) == ESP_ERR_INVALID_ARG);
    CHECK(mesh_now_pin_route(dest_b, g_local_mac) == ESP_ERR_INVALID_ARG);

    CHECK(mesh_now_unpin_route(dest_a) == ESP_OK);
    CHECK(mesh_now_get_route(dest_a, &out));
    CHECK(!out.pinned);

    // Re-unpinning an existing (non-pinned) route is idempotent.
    CHECK(mesh_now_unpin_route(dest_a) == ESP_OK);
    CHECK(mesh_now_unpin_route(dest_b) == ESP_ERR_NOT_FOUND);
}

static void pin_route_when_table_full_returns_no_mem(void)
{
    mocks_for_route_tests();
    for (int i = 0; i < MAX_ROUTES; i++) {
        uint8_t dest[6];
        uint8_t hop[6];
        fill_mac(dest, 0x7000 + i);
        fill_mac(hop, 0x8000 + i);
        mesh_now_add_route(dest, hop, 1, 0);
    }
    CHECK(mesh_now_pin_route(dest_a, hop_x) == ESP_ERR_NO_MEM);
}

static void expire_drops_stale_keeps_pinned(void)
{
    mocks_for_route_tests();
    mesh_now_add_route(dest_a, hop_x, 1, 0);
    mesh_now_add_route(dest_b, hop_x, 1, 0);
    mesh_now_pin_route(dest_c, hop_y);

    g_now_us += ROUTE_LIFETIME_US + 1;
    mesh_now_expire_routes(g_now_us);

    mesh_route_t out;
    CHECK(!mesh_now_get_route(dest_a, &out));
    CHECK(!mesh_now_get_route(dest_b, &out));
    CHECK(mesh_now_get_route(dest_c, &out));
    CHECK(out.pinned);
    CHECK(mesh_now_get_route_count() == 1);
}

static void invalidate_drops_thru_hop_except_pinned(void)
{
    mocks_for_route_tests();
    mesh_now_add_route(dest_a, hop_x, 1, 0);
    mesh_now_add_route(dest_b, hop_x, 1, 0);
    mesh_now_add_route(dest_c, hop_y, 1, 0);
    mesh_now_pin_route(dest_d, hop_x);

    int removed = mesh_now_invalidate_routes_through(hop_x);

    mesh_route_t out;
    CHECK(removed == 2);
    CHECK(!mesh_now_get_route(dest_a, &out));
    CHECK(!mesh_now_get_route(dest_b, &out));
    CHECK(mesh_now_get_route(dest_c, &out));
    CHECK(mesh_now_get_route(dest_d, &out));
    CHECK(mesh_now_get_route_count() == 2);
}

static void snapshot_caps_at_max_out(void)
{
    mocks_for_route_tests();
    mesh_now_add_route(dest_a, hop_x, 1, 0);
    mesh_now_add_route(dest_b, hop_y, 1, 0);
    mesh_now_add_route(dest_c, hop_x, 1, 0);

    mesh_route_t out[2];
    int n = mesh_now_snapshot_routes(out, 2);
    CHECK(n == 2);
    CHECK(macs_equal(out[0].dest_mac, dest_a));
    CHECK(macs_equal(out[1].dest_mac, dest_b));

    CHECK(mesh_now_snapshot_routes(NULL, 2) == 0);
    CHECK(mesh_now_snapshot_routes(out, 0) == 0);
}

static void virtual_peer_and_route_name(void)
{
    mocks_for_route_tests();
    // dest == via is rejected outright
    mesh_now_add_virtual_peer(dest_a, dest_a, "bad", 2);
    CHECK(mesh_now_get_route_count() == 0);

    mesh_now_add_virtual_peer(dest_a, hop_x, "zonepeer", 2);
    mesh_route_t out;
    CHECK(mesh_now_get_route(dest_a, &out));
    CHECK(out.hop_count == 2);
    CHECK_STREQ(out.node_name, "zonepeer");
    CHECK(macs_equal(out.next_hop, hop_x));

    mesh_now_route_set_name(dest_a, "renamed");
    CHECK(mesh_now_get_route(dest_a, &out));
    CHECK_STREQ(out.node_name, "renamed");

    // Unknown destination: name setter is a no-op.
    mesh_now_route_set_name(dest_b, "ghost");
}

void run_route_tests(void)
{
    printf("route\n");
    add_read_and_update();
    self_mac_is_never_a_route();
    full_table_refuses_new_routes();
    pin_and_unpin_lifecycle();
    pin_route_when_table_full_returns_no_mem();
    expire_drops_stale_keeps_pinned();
    invalidate_drops_thru_hop_except_pinned();
    snapshot_caps_at_max_out();
    virtual_peer_and_route_name();
    printf("%3d passed so far\n", g_checks - g_failures);
}