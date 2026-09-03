#ifndef SERIAL_API_H
#define SERIAL_API_H

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// JSON over the USB-Serial/JTAG port. The browser connects with Web Serial
// and exchanges one JSON object per line. The same frame format will later 
// be carried over Web Bluetooth GATT.
//
// Browser -> node ("cmd"):
//   {"cmd":"hello"}                                  self + status snapshot
//   {"cmd":"send","target":"<mac>","message":"..."}  direct message
//   {"cmd":"send","group":N,"message":"..."}         group message
//   {"cmd":"send","message":"..."}                   broadcast message
//   {"cmd":"typing","target":"<mac>","typing":true}  typing indicator
//   {"cmd":"name","name":"..."}                      rename this node
//   {"cmd":"group","group_id":N}                     join/leave group
//   {"cmd":"encryption","key":"..."}                 set encryption key
//
// Node -> browser (pushed events):
//   {"event":"hello","mac":"...","name":"...","group_id":N,"encrypted":bool}
//   {"event":"message","sender":"...","target":"...","type":N,"group_id":N,
//    "timestamp":N,"content":"..."}
//   {"event":"peers","peers":[{mac,name,online},...]}
//   {"event":"system","text":"..."}

typedef void (*serial_send_broadcast_cb)(const char *message);
typedef void (*serial_send_direct_cb)(const uint8_t *target_mac,
                                      const char *message);
typedef void (*serial_send_group_cb)(uint8_t group_id, const char *message);
typedef void (*serial_send_presence_cb)(const char *status);
typedef void (*serial_typing_cb)(const uint8_t *target_mac, bool typing);
typedef void (*serial_set_name_cb)(const char *name);
typedef void (*serial_set_group_cb)(uint8_t group_id);
typedef void (*serial_set_encryption_cb)(const uint8_t *key, size_t len);

typedef struct {
    serial_send_broadcast_cb send_broadcast;
    serial_send_direct_cb send_direct;
    serial_send_group_cb send_group;
    serial_send_presence_cb send_presence;
    serial_typing_cb send_typing;
    serial_set_name_cb set_name;
    serial_set_group_cb set_group;
    serial_set_encryption_cb set_encryption;
} serial_api_callbacks_t;

esp_err_t serial_api_init(QueueHandle_t message_queue,
                          const serial_api_callbacks_t *callbacks);

#endif // SERIAL_API_H