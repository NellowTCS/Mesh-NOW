#include "mesh_now_internal.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <string.h>

#define TAG "MESH_NOW"

int mesh_now_add_pending(const uint8_t *dest_mac, const uint8_t *remote_dest,
                         const uint8_t *wire, size_t wire_len,
                         uint32_t message_id, uint8_t flags, bool route_wait)
{
    // Init under one mutex hold so the retransmit task never sees a partial
    // slot.
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_PENDING_MESSAGES; ++i) {
        if (pending_messages[i].active) {
            continue;
        }
        pending_messages[i].message_id = message_id;
        pending_messages[i].flags = flags;
        memcpy(pending_messages[i].wire_buf, wire, wire_len);
        pending_messages[i].wire_len = wire_len;
        pending_messages[i].route_wait = route_wait;
        if (dest_mac != NULL) {
            memcpy(pending_messages[i].dest_mac, dest_mac, ESP_NOW_ETH_ALEN);
        }
        if (remote_dest != NULL) {
            memcpy(pending_messages[i].remote_dest, remote_dest,
                   ESP_NOW_ETH_ALEN);
        }
        pending_messages[i].retries = 0;
        pending_messages[i].last_send_time_ms = esp_timer_get_time() / 1000;
        pending_messages[i].active = true;
        xSemaphoreGive(state_mutex);
        return i;
    }
    xSemaphoreGive(state_mutex);
    return -1;
}

int mesh_now_find_pending(uint32_t message_id)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_PENDING_MESSAGES; ++i) {
        if (pending_messages[i].active &&
            pending_messages[i].message_id == message_id) {
            xSemaphoreGive(state_mutex);
            return i;
        }
    }
    xSemaphoreGive(state_mutex);
    return -1;
}

void mesh_now_release_pending(int index)
{
    if (index >= 0 && index < MAX_PENDING_MESSAGES) {
        xSemaphoreTake(state_mutex, portMAX_DELAY);
        pending_messages[index].active = false;
        xSemaphoreGive(state_mutex);
    }
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
    for (int i = 0;
         i < MAX_PENDING_MESSAGES && flush_count < MAX_PENDING_MESSAGES; ++i) {
        pending_message_t *pending = &pending_messages[i];
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
        int idx = -1;
        xSemaphoreTake(state_mutex, portMAX_DELAY);
        for (int p = 0; p < MAX_PENDING_MESSAGES; ++p) {
            if (pending_messages[p].active &&
                pending_messages[p].message_id == msg_id) {
                idx = p;
                break;
            }
        }
        if (idx >= 0) {
            memcpy(dest, pending_messages[idx].dest_mac, ESP_NOW_ETH_ALEN);
            wire_len = pending_messages[idx].wire_len;
            memcpy(wire, pending_messages[idx].wire_buf, wire_len);
        }
        xSemaphoreGive(state_mutex);
        if (idx < 0) {
            continue;
        }
        esp_err_t ret = esp_now_send(dest, wire, wire_len);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Route-ready send failed for %u: %s", msg_id,
                     esp_err_to_name(ret));
        }
    }
}