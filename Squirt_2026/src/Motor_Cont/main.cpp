// Motor_Cont
// Motor controller.
//
// Responsibilities:
//   - Receive desired position from Float_Cont over UART.
//   - Drive a motor to that position (closed-loop).

#include <Arduino.h>
#include "messages.h"

// UART link from Float_Cont.
// TODO: confirm/assign the TX/RX GPIOs you wire to Float_Cont.
static const int FLOAT_UART_TX = 21;
static const int FLOAT_UART_RX = 20;
static const uint32_t FLOAT_UART_BAUD = 115200;
HardwareSerial FloatSerial(1);

static float desiredPosition = 0.0f;

void driveMotorTo(float targetPosition) {
    // TODO: read encoder/feedback and drive motor toward targetPosition.
}

void handleFrame(const FloatToMotor &frame) {
    if (frame.version != SQUIRT_PROTOCOL_VERSION) {
        Serial.printf("Protocol mismatch: %u\n", frame.version);
        return;
    }
    uint8_t expected = squirtChecksum(reinterpret_cast<const uint8_t *>(&frame),
                                      sizeof(frame));
    if (expected != frame.checksum) {
        Serial.println("Checksum mismatch, frame dropped");
        return;
    }
    desiredPosition = frame.desiredPosition;
    Serial.printf("Desired position: %.3f\n", desiredPosition);
}

void setup() {
    Serial.begin(115200);
    FloatSerial.begin(FLOAT_UART_BAUD, SERIAL_8N1, FLOAT_UART_RX, FLOAT_UART_TX);

    Serial.println("[Motor_Cont] ready");

    // TODO: init motor driver / PWM / encoder here.
}

void loop() {
    // Reassemble FloatToMotor frames byte-by-byte, resyncing on the sync byte.
    static uint8_t rx[sizeof(FloatToMotor)];
    static size_t idx = 0;

    while (FloatSerial.available()) {
        uint8_t b = (uint8_t)FloatSerial.read();

        // Resync: a frame always starts with MOTOR_FRAME_SYNC.
        if (idx == 0 && b != MOTOR_FRAME_SYNC) {
            continue;
        }

        rx[idx++] = b;
        if (idx == sizeof(rx)) {
            FloatToMotor frame;
            memcpy(&frame, rx, sizeof(frame));
            handleFrame(frame);
            idx = 0;
        }
    }

    driveMotorTo(desiredPosition);
    delay(5);
}
