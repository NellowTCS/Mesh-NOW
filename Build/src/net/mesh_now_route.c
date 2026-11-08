#include "../core/mesh_now_internal.h"
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <string.h>

#define TAG "MESH_NOW"

// Find an active route block by destination MAC. Returns the block pointer
// or NULL.  Caller must hold state_mutex.
static mesh_route_t *find_route_locked(const uint8_t *dest_mac)
{
    mesh_route_t *routes = (mesh_route_t *)route_pool.blocks;
    for (size_t i = 0; i < route_pool.block_count; i++) {
        if (routes[i].active &&
            memcmp(routes[i].dest_mac, dest_mac, ESP_NOW_ETH_ALEN) == 0) {
            return &routes[i];
        }
    }
    return NULL;
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

    mesh_route_t *r = find_route_locked(dest_mac);
    if (r != NULL) {
        if (!r->pinned) {
            memcpy(r->next_hop, next_hop, ESP_NOW_ETH_ALEN);
            r->hop_count = hop_count;
            r->dest_seq = dest_seq;
            r->active = true;
            r->last_used_us = esp_timer_get_time();
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

    r = (mesh_route_t *)mesh_now_pool_alloc(&route_pool);
    if (r == NULL) {
        ESP_LOGW(TAG, "Route pool exhausted");
        xSemaphoreGive(state_mutex);
        return;
    }

    memcpy(r->dest_mac, dest_mac, ESP_NOW_ETH_ALEN);
    memcpy(r->next_hop, next_hop, ESP_NOW_ETH_ALEN);
    r->hop_count = hop_count;
    r->dest_seq = dest_seq;
    r->last_used_us = esp_timer_get_time();
    r->active = true;
    r->pinned = false;
    r->node_name[0] = '\0';
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

    mesh_route_t *r = find_route_locked(dest_mac);
    if (r == NULL) {
        if (route_count >= MAX_ROUTES) {
            xSemaphoreGive(state_mutex);
            return ESP_ERR_NO_MEM;
        }
        r = (mesh_route_t *)mesh_now_pool_alloc(&route_pool);
        if (r == NULL) {
            xSemaphoreGive(state_mutex);
            return ESP_ERR_NO_MEM;
        }
        memset(r, 0, sizeof(*r));
        memcpy(r->dest_mac, dest_mac, ESP_NOW_ETH_ALEN);
        route_count++;
    }

    r->pinned = true;
    r->active = true;
    memcpy(r->next_hop, proxy_mac, ESP_NOW_ETH_ALEN);
    r->hop_count = 1;
    r->last_used_us = esp_timer_get_time();

    xSemaphoreGive(state_mutex);
    return ESP_OK;
}

esp_err_t mesh_now_unpin_route(const uint8_t *dest_mac)
{
    if (dest_mac == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(state_mutex, portMAX_DELAY);
    mesh_route_t *r = find_route_locked(dest_mac);
    if (r == NULL) {
        xSemaphoreGive(state_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    r->pinned = false;
    xSemaphoreGive(state_mutex);
    return ESP_OK;
}

bool mesh_now_get_route(const uint8_t *dest_mac, mesh_route_t *out)
{
    bool found = false;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    mesh_route_t *r = find_route_locked(dest_mac);
    if (r != NULL) {
        if (out != NULL) {
            *out = *r;
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
    mesh_route_t *routes = (mesh_route_t *)route_pool.blocks;
    size_t copied = 0;
    for (size_t i = 0; i < route_pool.block_count && copied < max_out; i++) {
        if (routes[i].active) {
            out[copied++] = routes[i];
        }
    }
    int count = (int)copied;
    xSemaphoreGive(state_mutex);
    return count;
}

// Drop routes through a dead next hop so senders re-discover.  Pinned routes
// are kept; their pool blocks are never freed here. Returns how many were
// invalidated.
int mesh_now_invalidate_routes_through(const uint8_t *next_hop)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);

    mesh_route_t *routes = (mesh_route_t *)route_pool.blocks;
    int removed = 0;
    for (size_t i = 0; i < route_pool.block_count; i++) {
        if (routes[i].active && !routes[i].pinned &&
            memcmp(routes[i].next_hop, next_hop, ESP_NOW_ETH_ALEN) == 0) {
            routes[i].active = false;
            mesh_now_pool_free(&route_pool, &routes[i]);
            route_count--;
            removed++;
        }
    }

    xSemaphoreGive(state_mutex);

    if (removed > 0) {
        ESP_LOGD(TAG, "Invalidated %d route(s) via %s", removed,
                 mesh_now_mac_str(next_hop));
    }
    return removed;
}

// Invalidate routes untouched for ROUTE_LIFETIME_US. Pinned routes never
// expire.
void mesh_now_expire_routes(int64_t now_us)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);

    mesh_route_t *routes = (mesh_route_t *)route_pool.blocks;
    int removed = 0;
    for (size_t i = 0; i < route_pool.block_count; i++) {
        if (routes[i].active && !routes[i].pinned &&
            (now_us - routes[i].last_used_us) > ROUTE_LIFETIME_US) {
            ESP_LOGD(TAG, "Route to %s expired",
                     mesh_now_mac_str(routes[i].dest_mac));
            mesh_now_pool_free(&route_pool, &routes[i]);
            routes[i].active = false;
            route_count--;
            removed++;
        }
    }

    xSemaphoreGive(state_mutex);

    if (removed > 0) {
        ESP_LOGD(TAG, "Expired %d route(s)", removed);
    }
}

// Store the route entry's node_name (RREP carries the answering node's name).
void mesh_now_route_set_name(const uint8_t *dest_mac, const char *name)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    mesh_route_t *r = find_route_locked(dest_mac);
    if (r != NULL) {
        strncpy(r->node_name, name, MESH_NOW_NODE_NAME_MAX);
        r->node_name[MESH_NOW_NODE_NAME_MAX] = '\0';
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
