#include "../core/mesh_now_internal.h"
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <string.h>

#define TAG "MESH_NOW"

// Number of active entries in the route_requests table.
static int route_request_count = 0;

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
        ESP_LOGW(TAG,
                 "Route table full, cannot add "
                 "%02x:%02x:%02x:%02x:%02x:%02x",
                 dest_mac[0], dest_mac[1], dest_mac[2], dest_mac[3],
                 dest_mac[4], dest_mac[5]);
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

    ESP_LOGD(TAG,
             "Route to %02x:%02x:%02x:%02x:%02x:%02x via "
             "%02x:%02x:%02x:%02x:%02x:%02x (%u hop)",
             dest_mac[0], dest_mac[1], dest_mac[2], dest_mac[3], dest_mac[4],
             dest_mac[5], next_hop[0], next_hop[1], next_hop[2], next_hop[3],
             next_hop[4], next_hop[5], hop_count);
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

// A broken next_hop can no longer carry traffic: invalidate every route that
// uses it so senders re-discover. Pinned routes are kept.
void mesh_now_invalidate_routes_through(const uint8_t *next_hop)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    for (int i = 0; i < route_count; i++) {
        if (routes[i].active && !routes[i].pinned &&
            memcmp(routes[i].next_hop, next_hop, ESP_NOW_ETH_ALEN) == 0) {
            routes[i].active = false;
        }
    }

    int write = 0;
    for (int i = 0; i < route_count; i++) {
        if (routes[i].active || routes[i].pinned) {
            if (write != i) {
                routes[write] = routes[i];
            }
            write++;
        }
    }
    int expired = route_count - write;
    route_count = write;
    xSemaphoreGive(state_mutex);

    if (expired > 0) {
        ESP_LOGD(TAG,
                 "Invalidated %d route(s) via "
                 "%02x:%02x:%02x:%02x:%02x:%02x",
                 expired, next_hop[0], next_hop[1], next_hop[2], next_hop[3],
                 next_hop[4], next_hop[5]);
    }
}

// Mark routes inactive after ROUTE_LIFETIME_US without use, then compact the
// table so route_count reflects active+pinned entries. Pinned routes never
// expire.
void mesh_now_expire_routes(int64_t now_us)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);

    for (int i = 0; i < route_count; i++) {
        if (routes[i].active && !routes[i].pinned &&
            (now_us - routes[i].last_used_us) > ROUTE_LIFETIME_US) {
            ESP_LOGD(TAG, "Route to %02x:%02x:%02x:%02x:%02x:%02x expired",
                     routes[i].dest_mac[0], routes[i].dest_mac[1],
                     routes[i].dest_mac[2], routes[i].dest_mac[3],
                     routes[i].dest_mac[4], routes[i].dest_mac[5]);
            routes[i].active = false;
        }
    }

    int write = 0;
    for (int i = 0; i < route_count; i++) {
        if (routes[i].active || routes[i].pinned) {
            if (write != i) {
                routes[write] = routes[i];
            }
            write++;
        }
    }
    route_count = write;

    xSemaphoreGive(state_mutex);
}

// Forward-path route to the RREP originator is still valid long enough for
// the reply to traverse back; twice the request timeout is generous
#define RREQ_CACHE_TTL_US (ROUTE_REQ_TIMEOUT_US * 2)

bool mesh_now_rreq_cache_hit(const uint8_t *origin_mac, uint32_t message_id)
{
    int64_t now_us = esp_timer_get_time();
    bool hit = false;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_RREQ_CACHE; i++) {
        rreq_cache_entry_t *entry = &rreq_cache[i];
        if (!entry->active || entry->expires_us < now_us) {
            continue;
        }
        if (entry->message_id == message_id &&
            memcmp(entry->origin_mac, origin_mac, ESP_NOW_ETH_ALEN) == 0) {
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
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_RREQ_CACHE; i++) {
        if (!rreq_cache[i].active || rreq_cache[i].expires_us < now_us) {
            memcpy(rreq_cache[i].origin_mac, origin_mac, ESP_NOW_ETH_ALEN);
            rreq_cache[i].message_id = message_id;
            rreq_cache[i].expires_us = now_us + RREQ_CACHE_TTL_US;
            rreq_cache[i].active = true;
            xSemaphoreGive(state_mutex);
            return;
        }
    }
    // Cache full: replace the oldest entry via linear scan.
    int oldest = 0;
    for (int i = 1; i < MAX_RREQ_CACHE; i++) {
        if (rreq_cache[i].expires_us < rreq_cache[oldest].expires_us) {
            oldest = i;
        }
    }
    memcpy(rreq_cache[oldest].origin_mac, origin_mac, ESP_NOW_ETH_ALEN);
    rreq_cache[oldest].message_id = message_id;
    rreq_cache[oldest].expires_us = now_us + RREQ_CACHE_TTL_US;
    rreq_cache[oldest].active = true;
    xSemaphoreGive(state_mutex);
}

static int find_route_request(const uint8_t *dest_mac)
{
    for (int i = 0; i < route_request_count; i++) {
        if (route_requests[i].active &&
            memcmp(route_requests[i].dest_mac, dest_mac, ESP_NOW_ETH_ALEN) ==
                0) {
            return i;
        }
    }
    return -1;
}

// Broadcast an RREQ for target. A single in-flight entry serves the whole
// discovery lifecycle.
esp_err_t mesh_now_request_route(const uint8_t *target)
{
    if (target == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t my_mac[ESP_NOW_ETH_ALEN];
    esp_read_mac(my_mac, ESP_MAC_WIFI_STA);
    if (memcmp(target, my_mac, ESP_NOW_ETH_ALEN) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int64_t now_us = esp_timer_get_time();
    mesh_route_t to_target;
    memset(&to_target, 0, sizeof(to_target));
    bool known_last_seq = mesh_now_get_route(target, &to_target);

    xSemaphoreTake(state_mutex, portMAX_DELAY);

    int idx = find_route_request(target);
    if (idx < 0) {
        if (route_request_count >= MAX_ROUTE_REQUESTS) {
            xSemaphoreGive(state_mutex);
            return ESP_ERR_NO_MEM;
        }
        idx = route_request_count++;
        // New request, first attempt.
        memcpy(route_requests[idx].dest_mac, target, ESP_NOW_ETH_ALEN);
        route_requests[idx].attempts = 1;
        route_requests[idx].active = true;
    } else {
        // Retried request, one more attempt. Attempt limit enforcement happens
        // in the retransmit task so it can drop the buffered message too.
        route_requests[idx].attempts++;
    }
    route_requests[idx].rreq_seq = mesh_now_generate_message_id();
    route_requests[idx].expires_us = now_us + ROUTE_REQ_TIMEOUT_US;
    uint32_t rreq_seq = route_requests[idx].rreq_seq;
    int new_attempts = route_requests[idx].attempts;

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

    mesh_now_rreq_cache_record(my_mac, rreq_seq);

    uint8_t wire[WIRE_BUF_SIZE];
    size_t wire_len = 0;
    esp_err_t err = mesh_now_prepare_wire(&rreq, wire, &wire_len, false);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGD(TAG,
             "Route request %u for %02x:%02x:%02x:%02x:%02x:%02x "
             "(attempt %d)",
             rreq_seq, target[0], target[1], target[2], target[3], target[4],
             target[5], new_attempts);
    return esp_now_send(broadcast_mac, wire, wire_len);
}

int mesh_now_route_request_attempts(const uint8_t *target)
{
    int attempts = -1;
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    int idx = find_route_request(target);
    if (idx >= 0) {
        attempts = route_requests[idx].attempts;
    }
    xSemaphoreGive(state_mutex);
    return attempts;
}

// Release the in-flight request entry once a reply has resolved the route.
static void clear_route_request(const uint8_t *dest_mac)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    int idx = find_route_request(dest_mac);
    if (idx >= 0) {
        route_requests[idx].active = false;
        int write = idx;
        for (int i = idx + 1; i < route_request_count; i++) {
            route_requests[write++] = route_requests[i];
        }
        route_request_count = write;
    }
    xSemaphoreGive(state_mutex);
}

// Retired in-flight requests (no reply, stale) must not block later
// discovery for the same destination.
void mesh_now_expire_route_requests(int64_t now_us)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    for (int i = 0; i < route_request_count; i++) {
        if (route_requests[i].active && route_requests[i].expires_us < now_us) {
            route_requests[i].active = false;
        }
    }
    int write = 0;
    for (int i = 0; i < route_request_count; i++) {
        if (route_requests[i].active) {
            if (write != i) {
                route_requests[write] = route_requests[i];
            }
            write++;
        }
    }
    route_request_count = write;
    xSemaphoreGive(state_mutex);
}

// Store the route entry's node_name (RREP carries the answering node's name).
static void route_set_name(const uint8_t *dest_mac, const char *name)
{
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    int idx = find_route(dest_mac);
    if (idx >= 0) {
        strncpy(routes[idx].node_name, name, MESH_NOW_NODE_NAME_MAX);
        routes[idx].node_name[MESH_NOW_NODE_NAME_MAX] = '\0';
    }
    xSemaphoreGive(state_mutex);
}

static bool check_originator(const uint8_t *mac)
{
    uint8_t my_mac[ESP_NOW_ETH_ALEN];
    esp_read_mac(my_mac, ESP_MAC_WIFI_STA);
    return memcmp(mac, my_mac, ESP_NOW_ETH_ALEN) == 0;
}

// The node that can answer the RREQ (either the target itself, or an
// intermediate with an active route to it) turns its distance into an RREP
// and sends it back toward the originator along the reverse route.
static void send_rrep_for(const mesh_message_t *rreq,
                          const mesh_route_t *route_to_target)
{
    mesh_message_t rrep;
    memset(&rrep, 0, sizeof(rrep));
    rrep.type = MSG_TYPE_ROUTE_REPLY;
    rrep.message_id = mesh_now_generate_message_id();
    // reply_to re-uses the RREQ sequence so the originator can match this
    // reply to its in-flight request.
    rrep.reply_to = rreq->message_id;
    // Distance from this node to the target, growing as the reply travels
    // back so the originator sees the true hop count.
    rrep.hop_count = route_to_target != NULL ? route_to_target->hop_count : 0;
    rrep.hop_limit = ROUTE_REQ_TTL;
    esp_read_mac(rrep.sender_mac, ESP_MAC_WIFI_STA);
    memcpy(rrep.target_mac, rreq->sender_mac, ESP_NOW_ETH_ALEN);
    rrep.timestamp = mesh_now_get_network_time_ms();

    uint8_t wire[WIRE_BUF_SIZE];
    size_t wire_len = 0;
    esp_err_t err = mesh_now_prepare_wire(&rrep, wire, &wire_len, false);
    if (err != ESP_OK) {
        return;
    }

    mesh_route_t reverse;
    bool have_reverse =
        mesh_now_get_route(rreq->sender_mac, &reverse) && reverse.active;
    const uint8_t *next_hop = have_reverse ? reverse.next_hop : broadcast_mac;
    esp_err_t ret = esp_now_send(next_hop, wire, wire_len);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send RREP for %u: %s", rrep.reply_to,
                 esp_err_to_name(ret));
    }
}

void mesh_now_handle_rreq(const mesh_message_t *msg, const uint8_t *src_addr)
{
    if (mesh_now_rreq_cache_hit(msg->sender_mac, msg->message_id)) {
        ESP_LOGD(TAG, "Duplicate RREQ %u from %02x:%02x:%02x:%02x:%02x:%02x",
                 msg->message_id, msg->sender_mac[0], msg->sender_mac[1],
                 msg->sender_mac[2], msg->sender_mac[3], msg->sender_mac[4],
                 msg->sender_mac[5]);
        return;
    }
    mesh_now_rreq_cache_record(msg->sender_mac, msg->message_id);

    // Every RREQ hop carries the originator's identity, so each node learns a
    // reverse route it can use to deliver the RREP.
    mesh_now_add_route(msg->sender_mac, src_addr, msg->hop_count + 1,
                       msg->message_id);

    bool is_target = check_originator(msg->target_mac);
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

    if (msg->hop_count + 1 < msg->hop_limit) {
        mesh_message_t forward = *msg;
        forward.hop_count++;
        uint8_t wire[WIRE_BUF_SIZE];
        size_t wire_len = 0;
        esp_err_t err = mesh_now_prepare_wire(&forward, wire, &wire_len, false);
        if (err != ESP_OK) {
            return;
        }
        esp_now_send(broadcast_mac, wire, wire_len);
    }
}

void mesh_now_handle_rrep(const mesh_message_t *msg, const uint8_t *src_addr)
{
    // Forward route toward the answering node.
    mesh_now_add_route(msg->sender_mac, src_addr, msg->hop_count + 1,
                       msg->message_id);
    if (msg->node_name[0] != '\0') {
        route_set_name(msg->sender_mac, msg->node_name);
    }

    uint8_t my_mac[ESP_NOW_ETH_ALEN];
    esp_read_mac(my_mac, ESP_MAC_WIFI_STA);
    if (memcmp(msg->target_mac, my_mac, ESP_NOW_ETH_ALEN) == 0) {
        // We originated the request: adopt the route and release any DM
        // buffered while waiting for it.
        ESP_LOGI(TAG, "Route to %02x:%02x:%02x:%02x:%02x:%02x found (%u hop)",
                 msg->sender_mac[0], msg->sender_mac[1], msg->sender_mac[2],
                 msg->sender_mac[3], msg->sender_mac[4], msg->sender_mac[5],
                 msg->hop_count + 1);
        mesh_now_flush_route_wait(msg->sender_mac);
        clear_route_request(msg->sender_mac);
        return;
    }

    // Relay the reply along the reverse route to the originator; fall back to
    // a bounded flood if the reverse path evaporated.
    mesh_route_t reverse;
    bool have_reverse =
        mesh_now_get_route(msg->target_mac, &reverse) && reverse.active;
    const uint8_t *next_hop = have_reverse ? reverse.next_hop : broadcast_mac;

    mesh_message_t forward = *msg;
    forward.hop_count++;
    uint8_t wire[WIRE_BUF_SIZE];
    size_t wire_len = 0;
    esp_err_t err = mesh_now_prepare_wire(&forward, wire, &wire_len, false);
    if (err != ESP_OK) {
        return;
    }
    esp_now_send(next_hop, wire, wire_len);
}