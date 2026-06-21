// Float_Cont
// Float controller. Central node of the system.
//
// Responsibilities:
//   - ESP-NOW link to Ground_Com (receive commands, send/rebroadcast data).
//   - Compute desired depth / position.
//   - Store data (depth profile, telemetry).
//   - Send desired position to Motor_Cont over UART.

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include "messages.h"

// UART link to Motor_Cont. Use a free hardware UART on the XIAO ESP32-C3.
// TODO: confirm/assign the TX/RX GPIOs you wire to Motor_Cont.
static const int MOTOR_UART_TX = 21;
static const int MOTOR_UART_RX = 20;
static const uint32_t MOTOR_UART_BAUD = 115200;
HardwareSerial MotorSerial(1);

// TODO: set this to the MAC address of the Ground_Com board.
static uint8_t groundComMac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

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

static float targetDepthM = 0.0f;     // current setpoint from Ground_Com
static float currentDepthM = 0.0f;     // measured depth (TODO: read sensor)
static float lastMotorPosition = 0.0f; // last position sent to Motor_Cont

void onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
    if (len != sizeof(GroundToFloat)) {
        Serial.printf("Bad GroundToFloat size: %d\n", len);
        return;
    }
    GroundToFloat msg;
    memcpy(&msg, data, sizeof(msg));
    if (msg.version != SQUIRT_PROTOCOL_VERSION) {
        Serial.printf("Protocol mismatch: %u\n", msg.version);
        return;
    }

    switch (msg.command) {
        case CMD_SET_DEPTH:
            targetDepthM = msg.targetDepthM;
            Serial.printf("Target depth set: %.2f m\n", targetDepthM);
            break;
        case CMD_RUN_PROFILE:
            // TODO: kick off stored depth profile.
            break;
        case CMD_STOP:
            // TODO: hold / return to surface.
            break;
        default:
            break;
    }
}

void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {
    // Optional: track rebroadcast delivery to Ground_Com.
}

float computeDesiredPosition() {
    // TODO: depth control law mapping (targetDepthM - currentDepthM) error to a
    // desired motor position. Placeholder returns the current position (hold).
    (void)targetDepthM;
    (void)currentDepthM;
    return lastMotorPosition;
}

void setup() {
    Serial.begin(115200);

    WiFi.mode(WIFI_MODE_STA);
    WiFi.begin();

    Serial.print("[Float_Cont] MAC Address: ");
    readMacAddress();

    MotorSerial.begin(MOTOR_UART_BAUD, SERIAL_8N1, MOTOR_UART_RX, MOTOR_UART_TX);

    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        return;
    }

    esp_now_register_recv_cb(onDataRecv);
    esp_now_register_send_cb(onDataSent);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, groundComMac, 6);
    peer.channel = 0;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK) {
        Serial.println("Failed to add Ground_Com peer");
    }
}

void sendMotorCommand(float desiredPos) {
    FloatToMotor frame;
    frame.sync = MOTOR_FRAME_SYNC;
    frame.version = SQUIRT_PROTOCOL_VERSION;
    frame.desiredPosition = desiredPos;
    frame.checksum = squirtChecksum(reinterpret_cast<uint8_t *>(&frame), sizeof(frame));
    MotorSerial.write(reinterpret_cast<uint8_t *>(&frame), sizeof(frame));
}

void rebroadcastTelemetry() {
    FloatToGround tlm;
    tlm.version = SQUIRT_PROTOCOL_VERSION;
    tlm.timestampMs = millis();
    tlm.currentDepthM = currentDepthM;
    tlm.targetDepthM = targetDepthM;
    tlm.motorPosition = lastMotorPosition;
    tlm.status = 0;  // TODO: pack status flags.
    esp_now_send(groundComMac, reinterpret_cast<uint8_t *>(&tlm), sizeof(tlm));
}

void loop() {
    // TODO: update currentDepthM from the depth sensor here.

    float desiredPos = computeDesiredPosition();
    lastMotorPosition = desiredPos;

    sendMotorCommand(desiredPos);   // -> Motor_Cont over UART
    rebroadcastTelemetry();         // -> Ground_Com over ESP-NOW

    // TODO: log/store data for later retrieval.

    delay(100);
}
