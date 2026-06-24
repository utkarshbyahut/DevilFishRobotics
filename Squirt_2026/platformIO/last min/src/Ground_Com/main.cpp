// //Ground_Com ESP32 Serial Brodcaster

#define ESPNOW_WIFI_CHANNEL 10



// INCLUDES
#include <Arduino.h>    // Core Arduino types/functions (Serial, millis, ...)
#include <WiFi.h>       // Wi-Fi stack; ESP-NOW runs on top of it
#include <esp_wifi.h>   // Lower-level Wi-Fi API (used here to read the MAC address)
#include <esp_now.h>    // ESP-NOW peer-to-peer messaging API


//Variables
//Float control MC: 80:f1:b2:50:e6:4c
//Motor Control: 80:f1:b2:50:ea:44
static uint8_t floatContMac[6] = {0x80, 0xF1, 0xB2, 0x50, 0xEa, 0x44};
static const size_t ESPNOW_MAX_PACKET = 250;


//Function Declarations here:
void readMacAddress();
void onDataSent(const uint8_t *mac, esp_now_send_status_t status);
void onDataRecv(const uint8_t *mac, const uint8_t *data, int len);





//--------- Setup -------------
void setup() {
    Serial.begin(115200);
    delay(5000);
    //Initalize ESP Wifi Driver
    WiFi.mode(WIFI_MODE_STA);
    WiFi.begin();


    esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

    //Wait for connection
    while (!Serial && millis() < 10000) {
        delay(10); 
    }
    
    Serial.println("Hello 1");

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
    peer.channel = ESPNOW_WIFI_CHANNEL;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK) {
        Serial.println("Failed to add Float_Cont peer");
    }
}

//-----------loop-------------
void loop() {
      // `static` so a partially received line survives between loop() iterations.
    static uint8_t buf[ESPNOW_MAX_PACKET];
    static size_t len = 0;

    // Drain whatever bytes are currently available from USB serial.
    while (Serial.available()) {
        uint8_t c = (uint8_t)Serial.read();
        Serial.write(c);
        buf[len++] = c;

        // Flush at end-of-line, or when the packet buffer is full.
        if (c == '\n' || len == ESPNOW_MAX_PACKET) {
            esp_now_send(floatContMac, buf, len);
            len = 0;
        }
    }
}









////---------------Functions-------------
//Print MAC Address
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

//On Data Sent
void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        Serial.println("Send success");
    } else {
        Serial.println("Send failed");
    }
}

void onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
    Serial.write(data, len);
    Serial.println();
}


// void setup() {
//     Serial.begin(115200);
//     //Wait for connection
//     while (!Serial && millis() < 4000) {
//         delay(10); 
//     }
// }

// void loop() {
//     Serial.println("Hello");
//     delay(2000);
// }
