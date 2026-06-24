// Shared message definitions for the Squirt 2026 three-controller system.
//
// This lives in include/ so PlatformIO adds it to the build path for every
// environment automatically. Keeping the payload structs here guarantees
// Ground_Com, Float_Cont, and Motor_Cont agree on the wire format byte-for-byte.
//
// Topology:
//   squirt UX  <--USB serial-->  Ground_Com  <--ESP-NOW-->  Float_Cont  <--UART-->  Motor_Cont
//
// The squirt UX legacy client (squirt_UX/current_lean) speaks a line-based
// protocol over USB serial:
//   - Commands out:  single letters  S N C T M P D R   (see PROTOCOL.md)
//   - Responses in:  JSON lines of type "Status" and "data"
//
// Ground_Com is the bridge: it translates those serial commands into the
// GroundToFloat struct below (ESP-NOW), and translates the FloatStatus /
// FloatDataPoint structs it receives back into the JSON the UI expects.
//
// All multi-controller payloads are #pragma pack(1) so the layout is identical
// regardless of compiler padding. ESP-NOW payloads must stay <= 250 bytes.

#pragma once

#include <stdint.h>

// Bump when any struct below changes so receivers can reject mismatched peers.
static const uint8_t SQUIRT_PROTOCOL_VERSION = 1;

// Max length (including null terminator) for a team/float name. Matches the
// "N <name>" command and the "team" field in the Status JSON.
static const uint8_t SQUIRT_NAME_LEN = 24;

#pragma pack(push, 1)

// ---- ESP-NOW: Ground_Com -> Float_Cont --------------------------------------
// Mirrors the legacy UI's single-letter serial command set. The enum values are
// the ASCII letters themselves, so Ground_Com can map an inbound serial byte
// straight to a command.
//
//   S -> CMD_STATUS_REQUEST   request a Status message
//   N -> CMD_SET_NAME         set team/float name        (uses name[])
//   C -> CMD_CALIBRATE        run calibration
//   T -> CMD_SET_TIME         set device clock           (uses epochSeconds)
//   M -> CMD_SET_MOTOR        set motor/ballast position (uses motorValue)
//   P -> CMD_RUN_PROFILE      start the dive profile
//   D -> CMD_DATA_REQUEST     dump stored data points
//   R -> CMD_CLEAR_DATA       clear stored data
enum GroundCommand : uint8_t {
    CMD_NONE           = 0,
    CMD_STATUS_REQUEST = 'S',
    CMD_SET_NAME       = 'N',
    CMD_CALIBRATE      = 'C',
    CMD_SET_TIME       = 'T',
    CMD_SET_MOTOR      = 'M',
    CMD_RUN_PROFILE    = 'P',
    CMD_DATA_REQUEST   = 'D',
    CMD_CLEAR_DATA     = 'R',
};

struct GroundToFloat {
    uint8_t       version;                 // = SQUIRT_PROTOCOL_VERSION
    GroundCommand command;                 // which action (see GroundCommand)
    float         motorValue;              // CMD_SET_MOTOR: 0..100 (0 down, 50 neutral, 100 up)
    uint32_t      epochSeconds;            // CMD_SET_TIME: Unix time to set
    char          name[SQUIRT_NAME_LEN];   // CMD_SET_NAME: null-terminated
};

// ---- ESP-NOW: Float_Cont -> Ground_Com --------------------------------------
// Float_Cont sends two message kinds; the first byte after version is the type
// so Ground_Com can tell them apart and emit the matching JSON.
enum FloatMsgType : uint8_t {
    MSG_STATUS = 1,  // -> {"type":"Status", ...}
    MSG_DATA   = 2,  // -> {"type":"data", ...}
};

// MSG_STATUS -> the UI's "Status" message (statusHandler in serial.js).
struct FloatStatus {
    uint8_t  version;                 // = SQUIRT_PROTOCOL_VERSION
    uint8_t  msgType;                 // = MSG_STATUS
    char     team[SQUIRT_NAME_LEN];   // -> "team"
    float    depth;                   // -> "depth"        (meters)
    float    pressure;                // -> "pressure"     (kPa)
    float    temperature;             // -> "temperature"  (deg C)
    float    battery;                 // -> "battery"      (volts or %)
    uint32_t time;                    // -> "time"         (epoch seconds)
    uint8_t  calibration;             // -> "calibration"  (bool: calibrated)
    uint8_t  timeSet;                 // -> "timeSet"      (bool: clock set)
    float    motorPosition;           // last commanded motor position
};

// MSG_DATA -> the UI's "data" message (dataHandler in serial.js). One point per
// message; sent in a burst in response to CMD_DATA_REQUEST.
struct FloatDataPoint {
    uint8_t  version;   // = SQUIRT_PROTOCOL_VERSION
    uint8_t  msgType;   // = MSG_DATA
    uint32_t time;      // -> "time"     (epoch seconds)
    float    depth_m;   // -> "depth_m"  (meters)
    uint8_t  reset;     // -> "reset"    (bool: clear the graph before this point)
};

// ---- UART: Float_Cont -> Motor_Cont -----------------------------------------
// Desired position command. Framed with a sync byte + checksum so Motor_Cont
// can resync on a noisy line instead of relying on newline parsing.
static const uint8_t MOTOR_FRAME_SYNC = 0xA5;

struct FloatToMotor {
    uint8_t  sync;             // = MOTOR_FRAME_SYNC
    uint8_t  version;          // = SQUIRT_PROTOCOL_VERSION
    float    desiredPosition;  // target motor position (0..100, from CMD_SET_MOTOR)
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
