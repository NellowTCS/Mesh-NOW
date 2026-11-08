#include "mesh_now_internal.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <string.h>

#define TAG "MESH_NOW"

// Find an active pending block by message ID. Returns the block pointer or
// NULL.  Caller must hold state_mutex.
static pending_message_t *find_pending_locked(uint32_t message_id)
{
    pending_message_t *pendings = (pending_message_t *)pending_pool.blocks;
    for (size_t i = 0; i < pending_pool.block_count; i++) {
        if (pendings[i].active && pendings[i].message_id == message_id) {
            return &pendings[i];
        }
    }
    return NULL;
}

int mesh_now_add_pending(const uint8_t *dest_mac, const uint8_t *remote_dest,
                         const uint8_t *wire, size_t wire_len,
                         uint32_t message_id, uint8_t flags, bool route_wait)
{
    // Init under one mutex hold so the retransmit task never sees a partial
    // slot.
    xSemaphoreTake(state_mutex, portMAX_DELAY);

    pending_message_t *p =
        (pending_message_t *)mesh_now_pool_alloc(&pending_pool);
    if (p == NULL) {
        xSemaphoreGive(state_mutex);
        return -1;
    }

    p->message_id = message_id;
    p->flags = flags;
    memcpy(p->wire_buf, wire, wire_len);
    p->wire_len = wire_len;
    p->route_wait = route_wait;
    if (dest_mac != NULL) {
        memcpy(p->dest_mac, dest_mac, ESP_NOW_ETH_ALEN);
    }
    if (remote_dest != NULL) {
        memcpy(p->remote_dest, remote_dest, ESP_NOW_ETH_ALEN);
    }
    p->retries = 0;
    p->last_send_time_ms = esp_timer_get_time() / 1000;
    p->active = true;

    xSemaphoreGive(state_mutex);
    return 0;
}

int mesh_now_find_pending(uint32_t message_id)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    pending_message_t *p = find_pending_locked(message_id);
    xSemaphoreGive(state_mutex);
    return p != NULL ? 0 : -1;
}

pending_message_t *mesh_now_find_pending_locked(uint32_t message_id)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    pending_message_t *p = find_pending_locked(message_id);
    xSemaphoreGive(state_mutex);
    return p;
}

void mesh_now_release_pending(uint32_t message_id)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    pending_message_t *p = find_pending_locked(message_id);
    if (p != NULL) {
        mesh_now_pool_free(&pending_pool, p);
        p->active =
            false; // pool_free overwrites the first bytes (incl. active)
    }
    xSemaphoreGive(state_mutex);
}

// RREP resolved a route to target: deliver buffered DMs to its next hop.
void mesh_now_flush_route_wait(const uint8_t *target)
{
    mesh_route_t rt;
    if (!mesh_now_get_route(target, &rt) || !rt.active) {
        return;
    }

    // Collect the buffered message ids, then send each one outside the mutex.
    uint32_t flush_ids[MAX_PENDING_MESSAGES];
    int flush_count = 0;

    int64_t now_ms = esp_timer_get_time() / 1000;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    pending_message_t *pendings = (pending_message_t *)pending_pool.blocks;
    for (size_t i = 0;
         i < pending_pool.block_count && flush_count < MAX_PENDING_MESSAGES;
         ++i) {
        pending_message_t *pending = &pendings[i];
        if (!pending->active || !pending->route_wait ||
            memcmp(pending->remote_dest, target, ESP_NOW_ETH_ALEN) != 0) {
            continue;
        }
        memcpy(pending->dest_mac, rt.next_hop, ESP_NOW_ETH_ALEN);
        pending->route_wait = false;
        pending->retries = 0;
        pending->last_send_time_ms = now_ms;
        flush_ids[flush_count++] = pending->message_id;
    }
    xSemaphoreGive(state_mutex);

    for (int i = 0; i < flush_count; ++i) {
        uint8_t dest[ESP_NOW_ETH_ALEN];
        uint8_t wire[WIRE_BUF_SIZE];
        size_t wire_len = 0;
        uint32_t msg_id = flush_ids[i];
        xSemaphoreTake(state_mutex, portMAX_DELAY);
        pending_message_t *pending = find_pending_locked(msg_id);
        if (pending != NULL) {
            memcpy(dest, pending->dest_mac, ESP_NOW_ETH_ALEN);
            wire_len = pending->wire_len;
            memcpy(wire, pending->wire_buf, wire_len);
        }
        xSemaphoreGive(state_mutex);
        if (pending == NULL) {
            continue;
        }
        esp_err_t ret = esp_now_send(dest, wire, wire_len);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Route-ready send failed for %u: %s", msg_id,
                     esp_err_to_name(ret));
        }
    }
}
