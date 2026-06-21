// Shared message definitions for the Squirt 2026 three-controller system.
//
// This lives in include/ so PlatformIO adds it to the build path for every
// environment automatically. Keeping the payload structs here guarantees
// Ground_Com, Float_Cont, and Motor_Cont agree on the wire format byte-for-byte.
//
// Links:
//   Ground_Com  <--ESP-NOW-->  Float_Cont  <--UART-->  Motor_Cont
//
// All multi-controller payloads are #pragma pack(1) so the layout is identical
// regardless of compiler padding. ESP-NOW payloads must stay <= 250 bytes.

#pragma once

#include <stdint.h>

// Bump when any struct below changes so receivers can reject mismatched peers.
static const uint8_t SQUIRT_PROTOCOL_VERSION = 1;

#pragma pack(push, 1)

// ---- ESP-NOW: Ground_Com -> Float_Cont --------------------------------------
// Operator commands sent topside-to-float.
enum GroundCommand : uint8_t {
    CMD_NONE        = 0,
    CMD_SET_DEPTH   = 1,  // use targetDepthM
    CMD_RUN_PROFILE = 2,  // start the stored depth profile
    CMD_STOP        = 3,  // hold/return to surface
};

struct GroundToFloat {
    uint8_t      version;       // = SQUIRT_PROTOCOL_VERSION
    GroundCommand command;
    float        targetDepthM;  // meters, used when command == CMD_SET_DEPTH
};

// ---- ESP-NOW: Float_Cont -> Ground_Com --------------------------------------
// Telemetry rebroadcast float-to-topside.
struct FloatToGround {
    uint8_t  version;        // = SQUIRT_PROTOCOL_VERSION
    uint32_t timestampMs;    // millis() on the float
    float    currentDepthM;  // measured depth, meters
    float    targetDepthM;   // current setpoint, meters
    float    motorPosition;  // last commanded motor position
    uint8_t  status;         // role-defined status / flags
};

// ---- UART: Float_Cont -> Motor_Cont -----------------------------------------
// Desired position command. Framed with a sync byte + checksum so Motor_Cont
// can resync on a noisy line instead of relying on newline parsing.
static const uint8_t MOTOR_FRAME_SYNC = 0xA5;

struct FloatToMotor {
    uint8_t  sync;             // = MOTOR_FRAME_SYNC
    uint8_t  version;          // = SQUIRT_PROTOCOL_VERSION
    float    desiredPosition;  // target motor position
    uint8_t  checksum;         // XOR of all preceding bytes
};

#pragma pack(pop)

// XOR checksum over the first (len-1) bytes of a frame; the final byte holds it.
static inline uint8_t squirtChecksum(const uint8_t *data, uint32_t len) {
    uint8_t c = 0;
    for (uint32_t i = 0; i + 1 < len; ++i) {
        c ^= data[i];
    }
    return c;
}
