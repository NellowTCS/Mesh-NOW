#include "../core/mesh_now_internal.h"
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <string.h>

#define TAG "MESH_NOW"

// Elements expire after the RREP had time to traverse back; twice the RREQ
// timeout is plenty.
#define RREQ_CACHE_TTL_US (ROUTE_REQ_TIMEOUT_US * 2)

bool mesh_now_rreq_cache_hit(const uint8_t *origin_mac, uint32_t message_id)
{
    int64_t now_us = esp_timer_get_time();
    bool hit = false;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    rreq_cache_entry_t *cache = (rreq_cache_entry_t *)rreq_cache_pool.blocks;
    for (size_t i = 0; i < rreq_cache_pool.block_count; i++) {
        if (!cache[i].active || cache[i].expires_us < now_us) {
            continue;
        }
        if (cache[i].message_id == message_id &&
            memcmp(cache[i].origin_mac, origin_mac, ESP_NOW_ETH_ALEN) == 0) {
            hit = true;
            break;
        }
    }
    xSemaphoreGive(state_mutex);
    return hit;
}

void mesh_now_rreq_cache_record(const uint8_t *origin_mac, uint32_t message_id)
{
    int64_t now_us = esp_timer_get_time();
    int64_t expires_us = now_us + RREQ_CACHE_TTL_US;

    xSemaphoreTake(state_mutex, portMAX_DELAY);
    rreq_cache_entry_t *cache = (rreq_cache_entry_t *)rreq_cache_pool.blocks;

    // Reuse an expired or free slot first.
    rreq_cache_entry_t *slot = NULL;
    for (size_t i = 0; i < rreq_cache_pool.block_count; i++) {
        if (!cache[i].active || cache[i].expires_us < now_us) {
            cache[i].active = true;
            cache[i].message_id = message_id;
            cache[i].expires_us = expires_us;
            memcpy(cache[i].origin_mac, origin_mac, ESP_NOW_ETH_ALEN);
            slot = &cache[i];
            break;
        }
    }
    if (slot != NULL) {
        xSemaphoreGive(state_mutex);
        return;
    }

    // Cache full: evict the oldest entry via linear scan.
    size_t oldest = 0;
    for (size_t i = 1; i < rreq_cache_pool.block_count; i++) {
        if (cache[i].expires_us < cache[oldest].expires_us) {
            oldest = i;
        }
    }
    memcpy(cache[oldest].origin_mac, origin_mac, ESP_NOW_ETH_ALEN);
    cache[oldest].message_id = message_id;
    cache[oldest].expires_us = expires_us;
    cache[oldest].active = true;
    xSemaphoreGive(state_mutex);
}

static route_request_t *find_route_request_locked(const uint8_t *dest_mac)
{
    route_request_t *requests = (route_request_t *)route_request_pool.blocks;
    for (size_t i = 0; i < route_request_pool.block_count; i++) {
        if (requests[i].active &&
            memcmp(requests[i].dest_mac, dest_mac, ESP_NOW_ETH_ALEN) == 0) {
            return &requests[i];
        }
    }
    return NULL;
}

// Broadcast an RREQ for target; one in-flight entry covers the whole
// discovery lifecycle.
esp_err_t mesh_now_request_route(const uint8_t *target)
{
    if (target == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t self_mac[ESP_NOW_ETH_ALEN];
    esp_read_mac(self_mac, ESP_MAC_WIFI_STA);
    if (memcmp(target, self_mac, ESP_NOW_ETH_ALEN) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int64_t now_us = esp_timer_get_time();
    mesh_route_t to_target;
    memset(&to_target, 0, sizeof(to_target));
    bool known_last_seq = mesh_now_get_route(target, &to_target);

    xSemaphoreTake(state_mutex, portMAX_DELAY);

    route_request_t *req = find_route_request_locked(target);
    if (req == NULL) {
        if (route_request_count >= MAX_ROUTE_REQUESTS) {
            xSemaphoreGive(state_mutex);
            return ESP_ERR_NO_MEM;
        }
        req = (route_request_t *)mesh_now_pool_alloc(&route_request_pool);
        if (req == NULL) {
            xSemaphoreGive(state_mutex);
            return ESP_ERR_NO_MEM;
        }
        memcpy(req->dest_mac, target, ESP_NOW_ETH_ALEN);
        req->attempts = 1;
        req->active = true;
        route_request_count++;
    } else {
        // Retried: one more attempt. The retransmit task enforces the cap so
        // it can drop the buffered DM too.
        req->attempts++;
    }
    req->rreq_seq = mesh_now_generate_message_id();
    req->expires_us = now_us + ROUTE_REQ_TIMEOUT_US;
    uint32_t rreq_seq = req->rreq_seq;
    int new_attempts = req->attempts;

    xSemaphoreGive(state_mutex);

    mesh_message_t rreq;
    memset(&rreq, 0, sizeof(rreq));
    rreq.type = MSG_TYPE_ROUTE_REQUEST;
    rreq.message_id = rreq_seq;
    // Freshness hint for intermediates: the highest destination sequence we
    // know. Zero means "any route will do".
    rreq.reply_to = known_last_seq ? to_target.dest_seq : 0;
    rreq.hop_limit = ROUTE_REQ_TTL;
    rreq.hop_count = 0;
    esp_read_mac(rreq.sender_mac, ESP_MAC_WIFI_STA);
    memcpy(rreq.target_mac, target, ESP_NOW_ETH_ALEN);
    rreq.timestamp = mesh_now_get_network_time_ms();

    mesh_now_rreq_cache_record(self_mac, rreq_seq);
    ESP_LOGD(TAG, "Route request %u for %s (attempt %d)", rreq_seq,
             mesh_now_mac_str(target), new_attempts);
    return mesh_now_send_frame(&rreq, broadcast_mac, false);
}

int mesh_now_route_request_attempts(const uint8_t *target)
{
    int attempts = -1;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    route_request_t *req = find_route_request_locked(target);
    if (req != NULL) {
        attempts = req->attempts;
    }
    xSemaphoreGive(state_mutex);
    return attempts;
}

// Release the in-flight request entry once a reply has resolved the route.
static void clear_route_request(const uint8_t *dest_mac)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    route_request_t *req = find_route_request_locked(dest_mac);
    if (req != NULL) {
        mesh_now_pool_free(&route_request_pool, req);
        req->active =
            false; // pool_free overwrites the first bytes (incl. active)
        route_request_count--;
    }
    xSemaphoreGive(state_mutex);
}

// Drop stale in-flight requests; otherwise they block re-discovery of the
// same destination.
void mesh_now_expire_route_requests(int64_t now_us)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);

    route_request_t *requests = (route_request_t *)route_request_pool.blocks;
    int removed = 0;
    for (size_t i = 0; i < route_request_pool.block_count; i++) {
        if (requests[i].active && requests[i].expires_us < now_us) {
            mesh_now_pool_free(&route_request_pool, &requests[i]);
            requests[i].active = false; // pool_free overwrites the first bytes
            route_request_count--;
            removed++;
        }
    }

    xSemaphoreGive(state_mutex);
}

// Answer an RREQ as the target, or as an intermediate holding a fresh route,
// with an RREP sent back along the reverse route.
static void send_rrep_for(const mesh_message_t *rreq,
                          const mesh_route_t *route_to_target)
{
    mesh_message_t rrep;
    memset(&rrep, 0, sizeof(rrep));
    rrep.type = MSG_TYPE_ROUTE_REPLY;
    rrep.message_id = mesh_now_generate_message_id();
    // reply_to carries the RREQ sequence so the originator can match it.
    rrep.reply_to = rreq->message_id;
    // Distance to the target; grows on the way back so the originator sees
    // the true hop count.
    rrep.hop_count = route_to_target != NULL ? route_to_target->hop_count : 0;
    rrep.hop_limit = ROUTE_REQ_TTL;
    esp_read_mac(rrep.sender_mac, ESP_MAC_WIFI_STA);
    memcpy(rrep.target_mac, rreq->sender_mac, ESP_NOW_ETH_ALEN);
    rrep.timestamp = mesh_now_get_network_time_ms();

    mesh_route_t reverse;
    bool have_reverse =
        mesh_now_get_route(rreq->sender_mac, &reverse) && reverse.active;
    const uint8_t *next_hop = have_reverse ? reverse.next_hop : broadcast_mac;
    esp_err_t ret = mesh_now_send_frame(&rrep, next_hop, false);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send RREP for %u: %s", rrep.reply_to,
                 esp_err_to_name(ret));
    }
}

void mesh_now_handle_rreq(const mesh_message_t *msg, const uint8_t *src_addr)
{
    if (mesh_now_rreq_cache_hit(msg->sender_mac, msg->message_id)) {
        ESP_LOGD(TAG, "Duplicate RREQ %u from %s", msg->message_id,
                 mesh_now_mac_str(msg->sender_mac));
        return;
    }
    mesh_now_rreq_cache_record(msg->sender_mac, msg->message_id);

    // Reverse route to the originator, used to deliver the RREP.
    mesh_now_add_route(msg->sender_mac, src_addr, msg->hop_count + 1,
                       msg->message_id);

    uint8_t self_mac[ESP_NOW_ETH_ALEN];
    esp_read_mac(self_mac, ESP_MAC_WIFI_STA);
    bool is_target = memcmp(msg->target_mac, self_mac, ESP_NOW_ETH_ALEN) == 0;
    mesh_route_t to_target;
    memset(&to_target, 0, sizeof(to_target));
    bool have_route =
        mesh_now_get_route(msg->target_mac, &to_target) && to_target.active;

    bool can_answer =
        is_target || (have_route && (to_target.pinned ||
                                     to_target.dest_seq >= msg->reply_to));
    if (can_answer) {
        send_rrep_for(msg, is_target ? NULL : &to_target);
        return;
    }

    mesh_now_relay_broadcast(msg, false);
}

void mesh_now_handle_rrep(const mesh_message_t *msg, const uint8_t *src_addr)
{
    // Forward route toward the answering node.
    mesh_now_add_route(msg->sender_mac, src_addr, msg->hop_count + 1,
                       msg->message_id);
    if (msg->node_name[0] != '\0') {
        mesh_now_route_set_name(msg->sender_mac, msg->node_name);
    }

    uint8_t self_mac[ESP_NOW_ETH_ALEN];
    esp_read_mac(self_mac, ESP_MAC_WIFI_STA);
    if (memcmp(msg->target_mac, self_mac, ESP_NOW_ETH_ALEN) == 0) {
        // We are the originator: adopt the route and flush buffered DMs.
        ESP_LOGI(TAG, "Route to %s found (%u hop)",
                 mesh_now_mac_str(msg->sender_mac), msg->hop_count + 1);
        mesh_now_flush_route_wait(msg->sender_mac);
        clear_route_request(msg->sender_mac);
        return;
    }

    // Relay the reply toward the originator; flood if the reverse path is gone.
    mesh_route_t reverse;
    bool have_reverse =
        mesh_now_get_route(msg->target_mac, &reverse) && reverse.active;
    const uint8_t *next_hop = have_reverse ? reverse.next_hop : broadcast_mac;
    mesh_now_relay_unicast(msg, next_hop, false);
}

// Broadcast a route error; receivers invalidate routes through us.
void mesh_now_send_rerr(void)
{
    mesh_message_t rerr;
    memset(&rerr, 0, sizeof(rerr));
    rerr.type = MSG_TYPE_ROUTE_ERROR;
    rerr.message_id = mesh_now_generate_message_id();
    rerr.hop_limit = ROUTE_REQ_TTL;
    rerr.hop_count = 0;
    esp_read_mac(rerr.sender_mac, ESP_MAC_WIFI_STA);
    rerr.timestamp = mesh_now_get_network_time_ms();
    strncpy(rerr.message, "BREAK", sizeof(rerr.message) - 1);

    mesh_now_send_frame(&rerr, broadcast_mac, false);
}

void mesh_now_handle_rerr(const mesh_message_t *msg)
{
    if (mesh_now_rreq_cache_hit(msg->sender_mac, msg->message_id)) {
        return;
    }
    mesh_now_rreq_cache_record(msg->sender_mac, msg->message_id);

    int invalidated = mesh_now_invalidate_routes_through(msg->sender_mac);
    ESP_LOGI(TAG, "Route error from %s (invalidated %d)",
             mesh_now_mac_str(msg->sender_mac), invalidated);

    // Relay the error one more segment so it reaches senders beyond two hops.
    mesh_now_relay_broadcast(msg, false);
}
