/*
 * MeshChat
 *
 * Every node that flashes this sketch joins the same mesh. Type a line in the
 * Serial Monitor and press Enter to broadcast it to the whole mesh; every
 * node prints the messages it receives.
 */

#include <Arduino.h>
#include <mesh_now.h>

static void on_message(const mesh_message_t *message) {
    Serial.print("[mesh] ");
    if (message->node_name[0] != '\0') {
        Serial.print(message->node_name);
        Serial.print(": ");
    }
    Serial.println(message->message);
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    mesh_now_set_name("MeshChat");
    mesh_now_set_receive_callback(on_message);

    if (mesh_now_init() != ESP_OK) {
        Serial.println("mesh_now_init() failed");
    }
}

void loop() {
    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) {
            mesh_now_send_broadcast(line.c_str());
        }
    }
    delay(10);
}