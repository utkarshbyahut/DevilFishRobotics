// ============================================================================
// Ground_Com
// ----------------------------------------------------------------------------
// Topside ("ground station") ESP32. It is a TRANSPARENT bridge between the
// operator's web UI and the float:
//
//     squirt UX (web app)  <==USB serial==>  Ground_Com  <==ESP-NOW==>  Float_Cont
//
// Ground_Com does NOT understand or modify the traffic. It just relays bytes:
//   - Anything it reads from USB serial (commands sent by the UI) is forwarded
//     verbatim to Float_Cont over ESP-NOW.
//   - Anything it receives from Float_Cont over ESP-NOW is written verbatim to
//     USB serial, straight back to the UI.
//
// "Send x, receive x." All protocol meaning lives on the float: the float
// parses the single-letter commands and transmits its JSON ("Status"/"data")
// lines directly, which simply pass through here untouched. Because nothing here
// interprets the payload, this file does not need include/messages.h.
// ============================================================================


// INCLUDES
#include <Arduino.h>    // Core Arduino types/functions (Serial, millis, ...)
#include <WiFi.h>       // Wi-Fi stack; ESP-NOW runs on top of it
#include <esp_wifi.h>   // Lower-level Wi-Fi API (used here to read the MAC address)
#include <esp_now.h>    // ESP-NOW peer-to-peer messaging API

// MAC address of the Float_Cont board. ESP-NOW addresses peers by their 6-byte
// Wi-Fi MAC, so Ground_Com must know exactly which board to send to.
// TODO: replace with the real Float_Cont MAC. You can print any board's MAC by
//       flashing it and reading the line that readMacAddress() prints on boot.
static uint8_t floatContMac[6] = {0x80, 0xF1, 0xB2, 0x50, 0xE6, 0x4C};

// ESP-NOW caps a single packet at 250 bytes, so we flush the outgoing serial
// buffer at (or before) that size.
static const size_t ESPNOW_MAX_PACKET = 250;

// ----------------------------------------------------------------------------
// readMacAddress()
// Prints THIS board's own Wi-Fi MAC address over USB serial. Handy during
// bring-up: run it on each board to discover the MACs you need to hard-code as
// peers (e.g. the floatContMac above, and Ground_Com's MAC inside Float_Cont).
// ----------------------------------------------------------------------------
void readMacAddress() {
    uint8_t baseMac[6];
    // Ask the Wi-Fi driver for the station-interface MAC into baseMac[].
    esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, baseMac);
    if (ret == ESP_OK) {
        // Print the 6 bytes as colon-separated hex, e.g. "80:f1:b2:50:e6:4c".
        Serial.printf("%02x:%02x:%02x:%02x:%02x:%02x\n",
                      baseMac[0], baseMac[1], baseMac[2],
                      baseMac[3], baseMac[4], baseMac[5]);
    } else {
        Serial.println("Failed to read MAC address");
    }
}

// ----------------------------------------------------------------------------
// onDataSent()  [ESP-NOW transmit callback]
// ESP-NOW calls this automatically after each send attempt, reporting whether
// the packet was acknowledged by the peer. Unused today; a natural hook for
// retry logic or a "link lost" indicator later.
// ----------------------------------------------------------------------------
void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {
}

// ----------------------------------------------------------------------------
// onDataRecv()  [ESP-NOW receive callback]   float -> UI direction
// ESP-NOW calls this automatically whenever a packet arrives from the float.
//   mac  = sender's MAC address (unused here)
//   data = raw bytes received
//   len  = number of bytes
// We pass the bytes straight out to USB serial, so the UI sees exactly what the
// float sent (e.g. a JSON line the float built itself). No parsing.
// ----------------------------------------------------------------------------
void onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
    Serial.write(data, len);
}

// ----------------------------------------------------------------------------
// setup()  -- runs once at boot
// Brings up USB serial, Wi-Fi/ESP-NOW, and registers the float as a peer.
// ----------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);  // USB serial link to the UI; must match the UI's baud

    // ESP-NOW requires Wi-Fi to be running. Station mode + begin() with no SSID
    // starts the radio without actually connecting to an access point.
    WiFi.mode(WIFI_MODE_STA);
    WiFi.begin();

    // Print our own MAC so it can be copied into Float_Cont's peer list.
    Serial.print("[Ground_Com] MAC Address: ");
    readMacAddress();

    // Initialise ESP-NOW. If this fails the radio isn't usable, so bail out of
    // setup() (the board will sit idle rather than crash-loop).
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        return;
    }

    // Hook our callbacks so ESP-NOW notifies us on send completion and receive.
    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);

    // Register Float_Cont as a peer. You must add a peer before you can send to
    // it. channel 0 = use the current Wi-Fi channel; encrypt = false = no PMK.
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, floatContMac, 6);
    peer.channel = 0;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK) {
        Serial.println("Failed to add Float_Cont peer");
    }
}

// ----------------------------------------------------------------------------
// loop()  -- runs forever after setup()
// UI -> float direction. We forward serial bytes to the float, breaking the
// stream on newlines so each command line goes as its own ESP-NOW packet (and
// guarding against the 250-byte packet limit). The float -> UI direction is
// handled entirely by onDataRecv above.
// ----------------------------------------------------------------------------
void loop() {
    // `static` so a partially received line survives between loop() iterations.
    static uint8_t buf[ESPNOW_MAX_PACKET];
    static size_t len = 0;

    // Drain whatever bytes are currently available from USB serial.
    while (Serial.available()) {
        uint8_t c = (uint8_t)Serial.read();
        buf[len++] = c;

        // Flush at end-of-line, or when the packet buffer is full.
        if (c == '\n' || len == ESPNOW_MAX_PACKET) {
            esp_now_send(floatContMac, buf, len);
            len = 0;
        }
    }
}
