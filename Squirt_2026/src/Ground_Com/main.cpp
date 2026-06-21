// Ground_Com
// Topside ESP32. Communicates with Float_Cont over ESP-NOW.
//
// Responsibilities:
//   - Send commands (e.g. target depth / run profile) to Float_Cont.
//   - Receive telemetry rebroadcast from Float_Cont and surface it to the
//     operator (serial / future UI).

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include "messages.h"

// TODO: set this to the MAC address of the Float_Cont board.
// Print a board's MAC with readMacAddress() below.
static uint8_t floatContMac[6] = {0x80, 0xF1, 0xB2, 0x50, 0xE6, 0x4C};
void readMacAddress() {
    uint8_t baseMac[6];
    esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, baseMac);
    if (ret == ESP_OK) {
        Serial.printf("%02x:%02x:%02x:%02x:%02x:%02x\n",
                      baseMac[0], baseMac[1], baseMac[2],
                      baseMac[3], baseMac[4], baseMac[5]);
    } else {
        Serial.println("Failed to read MAC address");
    }
}

void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {
    Serial.printf("ESP-NOW send: %s\n",
                  status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

void onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
    if (len != sizeof(FloatToGround)) {
        Serial.printf("Bad FloatToGround size: %d\n", len);
        return;
    }
    FloatToGround tlm;
    memcpy(&tlm, data, sizeof(tlm));
    if (tlm.version != SQUIRT_PROTOCOL_VERSION) {
        Serial.printf("Protocol mismatch: %u\n", tlm.version);
        return;
    }
    Serial.printf("[%lu ms] depth=%.2f target=%.2f motor=%.3f status=%u\n",
                  (unsigned long)tlm.timestampMs, tlm.currentDepthM,
                  tlm.targetDepthM, tlm.motorPosition, tlm.status);
}

void sendDepthCommand(float depthM) {
    GroundToFloat cmd;
    cmd.version = SQUIRT_PROTOCOL_VERSION;
    cmd.command = CMD_SET_DEPTH;
    cmd.targetDepthM = depthM;
    esp_now_send(floatContMac, reinterpret_cast<uint8_t *>(&cmd), sizeof(cmd));
}

void setup() {
    Serial.begin(115200);

    WiFi.mode(WIFI_MODE_STA);
    WiFi.begin();

    Serial.print("[Ground_Com] MAC Address: ");
    readMacAddress();

    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        return;
    }

    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, floatContMac, 6);
    peer.channel = 0;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK) {
        Serial.println("Failed to add Float_Cont peer");
    }
}

void loop() {
    // TODO: replace with real operator input (UI / serial command parsing).
    // Example: sendDepthCommand(2.5f);
    delay(1000);
}
