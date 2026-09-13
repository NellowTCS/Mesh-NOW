#include "../core/mesh_now_internal.h"
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <string.h>

#define TAG "MESH_NOW"

// Index of a virtual peer by destination MAC within the active region
// [0, route_count), or -1 if not present. Caller must hold state_mutex.
static int find_route(const uint8_t *dest_mac)
{
    for (int i = 0; i < route_count; i++) {
        if (routes[i].active &&
            memcmp(routes[i].dest_mac, dest_mac, ESP_NOW_ETH_ALEN) == 0) {
            return i;
        }
    }
    return -1;
}

void mesh_now_add_route(const uint8_t *dest_mac, const uint8_t *next_hop,
                        uint8_t hop_count, uint32_t dest_seq)
{
    uint8_t my_mac[ESP_NOW_ETH_ALEN];
    esp_read_mac(my_mac, ESP_MAC_WIFI_STA);
    if (memcmp(dest_mac, my_mac, ESP_NOW_ETH_ALEN) == 0) {
        return;
    }

    xSemaphoreTake(state_mutex, portMAX_DELAY);

    int idx = find_route(dest_mac);
    if (idx >= 0) {
        if (!routes[idx].pinned) {
            memcpy(routes[idx].next_hop, next_hop, ESP_NOW_ETH_ALEN);
            routes[idx].hop_count = hop_count;
            routes[idx].dest_seq = dest_seq;
            routes[idx].active = true;
            routes[idx].last_used_us = esp_timer_get_time();
        }
        xSemaphoreGive(state_mutex);
        return;
    }

    if (route_count >= MAX_ROUTES) {
        ESP_LOGW(TAG, "Route table full, cannot add %s",
                 mesh_now_mac_str(dest_mac));
        xSemaphoreGive(state_mutex);
        return;
    }

    memcpy(routes[route_count].dest_mac, dest_mac, ESP_NOW_ETH_ALEN);
    memcpy(routes[route_count].next_hop, next_hop, ESP_NOW_ETH_ALEN);
    routes[route_count].hop_count = hop_count;
    routes[route_count].dest_seq = dest_seq;
    routes[route_count].last_used_us = esp_timer_get_time();
    routes[route_count].active = true;
    routes[route_count].pinned = false;
    routes[route_count].node_name[0] = '\0';
    route_count++;

    xSemaphoreGive(state_mutex);

    ESP_LOGD(TAG, "Route to %s via %s (%u hop)", mesh_now_mac_str(dest_mac),
             mesh_now_mac_str(next_hop), hop_count);
}

esp_err_t mesh_now_pin_route(const uint8_t *dest_mac, const uint8_t *proxy_mac)
{
    if (dest_mac == NULL || proxy_mac == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t my_mac[ESP_NOW_ETH_ALEN];
    esp_read_mac(my_mac, ESP_MAC_WIFI_STA);
    if (memcmp(dest_mac, my_mac, ESP_NOW_ETH_ALEN) == 0 ||
        memcmp(proxy_mac, my_mac, ESP_NOW_ETH_ALEN) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(state_mutex, portMAX_DELAY);

    int idx = find_route(dest_mac);
    if (idx < 0) {
        if (route_count >= MAX_ROUTES) {
            xSemaphoreGive(state_mutex);
            return ESP_ERR_NO_MEM;
        }
        idx = route_count++;
        memset(&routes[idx], 0, sizeof(routes[idx]));
        memcpy(routes[idx].dest_mac, dest_mac, ESP_NOW_ETH_ALEN);
    }

    routes[idx].pinned = true;
    routes[idx].active = true;
    memcpy(routes[idx].next_hop, proxy_mac, ESP_NOW_ETH_ALEN);
    routes[idx].hop_count = 1;
    routes[idx].last_used_us = esp_timer_get_time();

    xSemaphoreGive(state_mutex);
    return ESP_OK;
}

esp_err_t mesh_now_unpin_route(const uint8_t *dest_mac)
{
    if (dest_mac == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(state_mutex, portMAX_DELAY);
    int idx = find_route(dest_mac);
    if (idx < 0) {
        xSemaphoreGive(state_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    routes[idx].pinned = false;
    xSemaphoreGive(state_mutex);
    return ESP_OK;
}

bool mesh_now_get_route(const uint8_t *dest_mac, mesh_route_t *out)
{
    bool found = false;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    int idx = find_route(dest_mac);
    if (idx >= 0) {
        if (out != NULL) {
            *out = routes[idx];
        }
        found = true;
    }
    xSemaphoreGive(state_mutex);
    return found;
}

int mesh_now_get_route_count(void)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    int count = route_count;
    xSemaphoreGive(state_mutex);
    return count;
}

int mesh_now_snapshot_routes(mesh_route_t *out, size_t max_out)
{
    if (out == NULL || max_out == 0) {
        return 0;
    }
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    size_t to_copy = route_count < (int)max_out ? route_count : max_out;
    for (size_t i = 0; i < to_copy; i++) {
        out[i] = routes[i];
    }
    int count = (int)to_copy;
    xSemaphoreGive(state_mutex);
    return count;
}

// Drop inbound routes through a dead next hop so senders re-discover.
// Pinned routes are kept; returns how many were invalidated.
int mesh_now_invalidate_routes_through(const uint8_t *next_hop)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);

    for (int i = 0; i < route_count; i++) {
        if (routes[i].active && !routes[i].pinned &&
            memcmp(routes[i].next_hop, next_hop, ESP_NOW_ETH_ALEN) == 0) {
            routes[i].active = false;
        }
    }

    bool active[MAX_ROUTES];
    for (int i = 0; i < route_count; i++) {
        active[i] = routes[i].active || routes[i].pinned;
    }
    size_t count = (size_t)route_count;
    size_t removed =
        mesh_now_compact_entries(routes, &count, sizeof(routes[0]), active);
    route_count = (int)count;
    xSemaphoreGive(state_mutex);

    if (removed > 0) {
        ESP_LOGD(TAG, "Invalidated %zu route(s) via %s", removed,
                 mesh_now_mac_str(next_hop));
    }
    return (int)removed;
}

// Invalidate routes untouched for ROUTE_LIFETIME_US, then compact. Pinned
// routes never expire.
void mesh_now_expire_routes(int64_t now_us)
{
    bool active[MAX_ROUTES];
    xSemaphoreTake(state_mutex, portMAX_DELAY);

    for (int i = 0; i < route_count; i++) {
        if (routes[i].active && !routes[i].pinned &&
            (now_us - routes[i].last_used_us) > ROUTE_LIFETIME_US) {
            ESP_LOGD(TAG, "Route to %s expired",
                     mesh_now_mac_str(routes[i].dest_mac));
            routes[i].active = false;
        }
        active[i] = routes[i].active || routes[i].pinned;
    }

    size_t count = (size_t)route_count;
    mesh_now_compact_entries(routes, &count, sizeof(routes[0]), active);
    route_count = (int)count;

    xSemaphoreGive(state_mutex);
}

// Store the route entry's node_name (RREP carries the answering node's name).
void mesh_now_route_set_name(const uint8_t *dest_mac, const char *name)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    int idx = find_route(dest_mac);
    if (idx >= 0) {
        strncpy(routes[idx].node_name, name, MESH_NOW_NODE_NAME_MAX);
        routes[idx].node_name[MESH_NOW_NODE_NAME_MAX] = '\0';
    }
    xSemaphoreGive(state_mutex);
}

// Zone route: via_mac can reach dest_mac. Zone announces are always 2 hops.
void mesh_now_add_virtual_peer(const uint8_t *dest_mac, const uint8_t *via_mac,
                               const char *name, uint8_t hop_count)
{
    if (dest_mac == NULL || via_mac == NULL ||
        memcmp(dest_mac, via_mac, ESP_NOW_ETH_ALEN) == 0) {
        return;
    }
    mesh_now_add_route(dest_mac, via_mac, hop_count, 0);
    if (name != NULL && name[0] != '\0') {
        mesh_now_route_set_name(dest_mac, name);
    }
}