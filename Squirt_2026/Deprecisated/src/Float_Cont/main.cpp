// Float_Cont
// Float controller. Central node of the system.
//
// Responsibilities:
//   - ESP-NOW link to Ground_Com: handle the UI command set (S N C T M P D R),
//     send back Status and data messages.
//   - Compute desired depth / position.
//   - Store data (depth profile, telemetry) and rebroadcast/dump on request.
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

// ---- Device state mirrored to the UI ----------------------------------------
static char     teamName[SQUIRT_NAME_LEN] = "Squirt";
static float    currentDepthM = 0.0f;   // measured depth (TODO: read sensor)
static float    pressureKpa = 0.0f;     // TODO: read sensor
static float    temperatureC = 0.0f;    // TODO: read sensor
static float    batteryLevel = 0.0f;    // TODO: read battery
static float    motorValue = 50.0f;     // commanded motor position (0..100)
static bool     calibrated = false;
static bool     timeSet = false;
static bool     profileRunning = false;

// Device clock: epoch set via CMD_SET_TIME, advanced with millis().
static uint32_t epochAtSet = 0;
static uint32_t millisAtSet = 0;

static uint32_t deviceEpoch() {
    if (!timeSet) return 0;
    return epochAtSet + (millis() - millisAtSet) / 1000;
}

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

void sendStatus() {
    FloatStatus s = {};
    s.version = SQUIRT_PROTOCOL_VERSION;
    s.msgType = MSG_STATUS;
    strncpy(s.team, teamName, SQUIRT_NAME_LEN - 1);
    s.depth = currentDepthM;
    s.pressure = pressureKpa;
    s.temperature = temperatureC;
    s.battery = batteryLevel;
    s.time = deviceEpoch();
    s.calibration = calibrated ? 1 : 0;
    s.timeSet = timeSet ? 1 : 0;
    s.motorPosition = motorValue;
    esp_now_send(groundComMac, reinterpret_cast<uint8_t *>(&s), sizeof(s));
}

void sendDataPoint(uint32_t t, float depth_m, bool reset) {
    FloatDataPoint d = {};
    d.version = SQUIRT_PROTOCOL_VERSION;
    d.msgType = MSG_DATA;
    d.time = t;
    d.depth_m = depth_m;
    d.reset = reset ? 1 : 0;
    esp_now_send(groundComMac, reinterpret_cast<uint8_t *>(&d), sizeof(d));
}

void dumpStoredData() {
    // TODO: replace with stored samples. The first point should carry reset=true
    // so the UI clears its graph before the dump.
    sendDataPoint(deviceEpoch(), currentDepthM, true);
}

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
        case CMD_STATUS_REQUEST:
            sendStatus();
            break;
        case CMD_SET_NAME:
            strncpy(teamName, msg.name, SQUIRT_NAME_LEN - 1);
            teamName[SQUIRT_NAME_LEN - 1] = '\0';
            sendStatus();
            break;
        case CMD_CALIBRATE:
            // TODO: run calibration routine.
            calibrated = true;
            sendStatus();
            break;
        case CMD_SET_TIME:
            epochAtSet = msg.epochSeconds;
            millisAtSet = millis();
            timeSet = true;
            sendStatus();
            break;
        case CMD_SET_MOTOR:
            profileRunning = false;  // manual override stops the profile
            motorValue = msg.motorValue;
            break;
        case CMD_RUN_PROFILE:
            // TODO: kick off stored depth profile.
            profileRunning = true;
            break;
        case CMD_DATA_REQUEST:
            dumpStoredData();
            break;
        case CMD_CLEAR_DATA:
            // TODO: clear stored samples.
            break;
        default:
            break;
    }
}

void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {
    // Optional: track delivery to Ground_Com.
}

float computeDesiredPosition() {
    if (profileRunning) {
        // TODO: depth control law -> desired motor position from the profile.
        return motorValue;
    }
    // Manual mode: honor the last CMD_SET_MOTOR value.
    return motorValue;
}

void sendMotorCommand(float desiredPos) {
    FloatToMotor frame;
    frame.sync = MOTOR_FRAME_SYNC;
    frame.version = SQUIRT_PROTOCOL_VERSION;
    frame.desiredPosition = desiredPos;
    frame.checksum = squirtChecksum(reinterpret_cast<uint8_t *>(&frame), sizeof(frame));
    MotorSerial.write(reinterpret_cast<uint8_t *>(&frame), sizeof(frame));
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

void loop() {
    // TODO: update currentDepthM / pressure / temperature / battery from sensors.

    float desiredPos = computeDesiredPosition();
    sendMotorCommand(desiredPos);   // -> Motor_Cont over UART

    // Periodically push live status to the UI.
    static uint32_t lastStatusMs = 0;
    if (millis() - lastStatusMs >= 1000) {
        lastStatusMs = millis();
        sendStatus();               // -> Ground_Com over ESP-NOW

        // TODO: store a sample for later CMD_DATA_REQUEST dumps.
    }

    delay(20);
}
