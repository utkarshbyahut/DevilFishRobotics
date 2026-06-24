//Ground_Com ESP32 Serial Brodcaster

#define ESPNOW_WIFI_CHANNEL 10
#define LIMIT_SWITCH D4
#define E1 D0
#define E2 D1
#define AIN1 D2
#define AIN2 D3




// INCLUDES
#include <Arduino.h>    // Core Arduino types/functions (Serial, millis, ...)
#include <WiFi.h>       // Wi-Fi stack; ESP-NOW runs on top of it
#include <esp_wifi.h>   // Lower-level Wi-Fi API (used here to read the MAC address)
#include <esp_now.h>    // ESP-NOW peer-to-peer messaging API


//Variables
//Controller 1 : e8:f6:0a:16:e6:50
static uint8_t floatContMac[6] = {0xE8, 0xF6, 0x0A, 0x16, 0xE6, 0x50};
static const size_t ESPNOW_MAX_PACKET = 250;




//Function Declarations here:
void readMacAddress();
void onDataSent(const uint8_t *mac, esp_now_send_status_t status);
void onDataRecv(const uint8_t *mac, const uint8_t *data, int len);





//--------- Setup -------------
void setup() {
    //Initalize ESP Wifi Driver
    WiFi.mode(WIFI_MODE_STA);
    WiFi.begin();

    esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

    // Initialise ESP-NOW. If this fails the radio isn't usable, so bail out of
    // setup() (the board will sit idle rather than crash-loop).
    if (esp_now_init() != ESP_OK) {
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
    }
    pinMode(LIMIT_SWITCH, INPUT_PULLUP);
    pinMode(E1, INPUT);
    pinMode(E2, INPUT);
    pinMode(AIN1, OUTPUT);
    pinMode(AIN2, OUTPUT);
    digitalWrite(AIN1, LOW);
    digitalWrite(AIN2, LOW);
}

//-----------loop-------------
void loop() {
    static uint32_t lastSend = 0;
    const uint32_t STATUS_INTERVAL_MS = 10000;
    static int stat = 0;

    uint32_t now = millis();
    if (now - lastSend >= STATUS_INTERVAL_MS) {
        lastSend = now;

        int limitSwitch = digitalRead(LIMIT_SWITCH);
        int e1 = digitalRead(E1);
        int e2 = digitalRead(E2);

        char status[50];
        int len = snprintf(status, sizeof(status), "LIMIT:%d E1:%d E2:%d\n", limitSwitch, e1, e2);
        esp_now_send(floatContMac, (uint8_t *)status, len);

        if (stat == 0){
              digitalWrite(AIN1, LOW);
              digitalWrite(AIN2, LOW);
              stat++;
        } else if (stat == 1){
              digitalWrite(AIN1, HIGH);
              digitalWrite(AIN2, LOW);
              stat++;
        } else if (stat == 2){
              digitalWrite(AIN1, LOW);
              digitalWrite(AIN2, LOW);
              stat++;
        } else if (stat == 3) {
              digitalWrite(AIN1, LOW);
              digitalWrite(AIN2, HIGH);
              stat++;
        } else{
              digitalWrite(AIN1, LOW);
              digitalWrite(AIN2, LOW);
              stat = 0;
        }
    }
}


////---------------Functions-------------


//On Data Sent
void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {

}

void onDataRecv(const uint8_t *mac_addr, const uint8_t *data, int len) {
    char reply[50];
    snprintf(reply, sizeof(reply), "Received message from %02X:%02X:%02X:%02X:%02X:%02X",
        mac_addr[0], mac_addr[1], mac_addr[2],
        mac_addr[3], mac_addr[4], mac_addr[5]);
    
    esp_now_send(mac_addr, (uint8_t *)reply, strlen(reply));
}

///