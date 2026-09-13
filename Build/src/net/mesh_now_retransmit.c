#include "../core/mesh_now_internal.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#define TAG "MESH_NOW"

// Route-wait pendings drive RREQ retries; then ACK-based retransmission runs
// for the rest.
void mesh_now_retransmit_task_fn(void *pvParameters)
{
    while (1) {
        int64_t now_ms = esp_timer_get_time() / 1000;

        // Phase 1: buffered DMs awaiting a discovered route.
        for (int i = 0; i < MAX_PENDING_MESSAGES; ++i) {
            bool active = false;
            bool route_wait = false;
            uint8_t remote_dest[ESP_NOW_ETH_ALEN] = {0};
            int64_t last_send_ms = 0;
            xSemaphoreTake(state_mutex, portMAX_DELAY);
            active = pending_messages[i].active;
            route_wait = pending_messages[i].route_wait;
            if (active && route_wait) {
                memcpy(remote_dest, pending_messages[i].remote_dest,
                       ESP_NOW_ETH_ALEN);
                last_send_ms = pending_messages[i].last_send_time_ms;
            }
            xSemaphoreGive(state_mutex);

            if (!active || !route_wait) {
                continue;
            }
            if (now_ms - last_send_ms <
                (int64_t)(ROUTE_REQ_TIMEOUT_US / 1000)) {
                continue;
            }

            int attempts = mesh_now_route_request_attempts(remote_dest);
            if (attempts <= MAX_ROUTE_REQ_RETRIES) {
                esp_err_t rc = mesh_now_request_route(remote_dest);
                if (rc != ESP_OK) {
                    ESP_LOGW(TAG, "Route request failed: %s",
                             esp_err_to_name(rc));
                }
                xSemaphoreTake(state_mutex, portMAX_DELAY);
                pending_messages[i].last_send_time_ms = now_ms;
                xSemaphoreGive(state_mutex);
            } else {
                ESP_LOGW(TAG,
                         "No route found for %s after %d requests, "
                         "dropping message",
                         mesh_now_mac_str(remote_dest), attempts);
                xSemaphoreTake(state_mutex, portMAX_DELAY);
                pending_messages[i].active = false;
                xSemaphoreGive(state_mutex);
                if (route_failure_callback) {
                    route_failure_callback(remote_dest);
                }
            }
        }

        // Phase 2: normal ACK-based retransmission.
        for (int i = 0; i < MAX_PENDING_MESSAGES; ++i) {
            bool do_send = false;
            bool do_drop = false;
            uint8_t dest_mac[ESP_NOW_ETH_ALEN];
            uint32_t message_id = 0;
            xSemaphoreTake(state_mutex, portMAX_DELAY);

            pending_message_t *pending = &pending_messages[i];
            if (pending->active && !pending->route_wait) {
                if (pending->flags & MSG_FLAG_REQUIRES_ACK) {
                    if (now_ms - pending->last_send_time_ms >=
                        RETRANSMIT_TIMEOUT_MS) {
                        if (pending->retries >= MAX_RETRIES) {
                            do_drop = true;
                            ESP_LOGW(TAG,
                                     "Dropping pending message after %d "
                                     "retries",
                                     pending->retries);
                        } else {
                            pending->retries++;
                            pending->last_send_time_ms = now_ms;
                            do_send = true;
                        }
                    }
                } else {
                    do_drop = true;
                }
            }
            if (do_send || do_drop) {
                memcpy(dest_mac, pending->dest_mac, ESP_NOW_ETH_ALEN);
                message_id = pending->message_id;
            }
            xSemaphoreGive(state_mutex);

            if (do_drop) {
                mesh_now_release_pending(i);
                continue;
            }
            if (!do_send) {
                continue;
            }

            int idx = mesh_now_find_pending(message_id);
            if (idx < 0) {
                continue;
            }
            size_t wire_len = 0;
            uint8_t wire[WIRE_BUF_SIZE];
            int retries = 0;
            xSemaphoreTake(state_mutex, portMAX_DELAY);
            wire_len = pending_messages[idx].wire_len;
            memcpy(wire, pending_messages[idx].wire_buf, wire_len);
            retries = pending_messages[idx].retries;
            xSemaphoreGive(state_mutex);

            esp_err_t ret = esp_now_send(dest_mac, wire, wire_len);
            if (ret == ESP_OK) {
                ESP_LOGD(TAG, "Retransmitted pending message (retry %d)",
                         retries);
            } else {
                ESP_LOGW(TAG, "Retransmit failed: %s", esp_err_to_name(ret));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}