/*
 * SideEye — checkpoint 4: slave capture stub (no camera yet).
 *
 * Listens for CMD_CAPTURE_REQUEST from the master over ESPNOW and replies
 * CMD_RESULT_REPLY with vehicleFound=1 immediately -- stands in for the
 * real camera cascade (checkpoint 5) so the master<->slave round trip and
 * the alert path can be proven before vision is ready.
 *
 * Blinks the built-in LED once per request purely for local debugging;
 * per PDR_SideEye.md section 4 the slave has no alert-display role.
 *
 * MAC (PDR_SideEye.md / ESPNOW_Peer_Test/TEAM_CONFIG.md, checkpoint 2 verified):
 *   slave (this board)  AC:27:6E:A8:47:80  COM14
 *   master (peer)       1C:DB:D4:74:49:E0  COM13
 */
#include <WiFi.h>
#include <cstring>
#include "esp_now.h"
#include "../../shared/protocol.h"

constexpr uint8_t ESPNOW_WIFI_CHANNEL = 0;
uint8_t masterMac[6] = {0x1C, 0xDB, 0xD4, 0x74, 0x49, 0xE0};
uint32_t sequenceNumber = 0;

volatile bool requestPending = false;
uint8_t requestFrom[6] = {};

void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int length) {
  if (length != static_cast<int>(sizeof(EspNowMessage))) return;
  const EspNowMessage *msg = reinterpret_cast<const EspNowMessage *>(data);
  if (msg->magic != SIDEEYE_MSG_MAGIC || msg->version != SIDEEYE_MSG_VERSION) return;
  if (msg->command != CMD_CAPTURE_REQUEST) return;
  memcpy(requestFrom, info->src_addr, sizeof(requestFrom));
  requestPending = true;
}

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("=== SideEye checkpoint 4: slave capture stub ===");

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);  // off (active low)

  WiFi.mode(WIFI_STA);
  WiFi.setChannel(ESPNOW_WIFI_CHANNEL);
  delay(100);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: esp_now_init failed");
    while (true) delay(1000);
  }
  esp_now_register_recv_cb(onReceive);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, masterMac, sizeof(peerInfo.peer_addr));
  peerInfo.channel = ESPNOW_WIFI_CHANNEL;
  peerInfo.ifidx = WIFI_IF_STA;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  Serial.println("Ready, waiting for capture requests");
}

void loop() {
  if (!requestPending) return;
  requestPending = false;

  digitalWrite(LED_BUILTIN, LOW);

  EspNowMessage reply = {};
  reply.magic = SIDEEYE_MSG_MAGIC;
  reply.version = SIDEEYE_MSG_VERSION;
  reply.command = CMD_RESULT_REPLY;
  reply.vehicleFound = 1;  // stub: no camera yet, always "found"
  reply.sequence = ++sequenceNumber;

  esp_err_t result = esp_now_send(requestFrom, reinterpret_cast<const uint8_t *>(&reply), sizeof(reply));
  Serial.printf("[REPLY] seq=%lu vehicleFound=1 result=%s\n",
                reply.sequence, result == ESP_OK ? "queued" : "error");

  delay(80);
  digitalWrite(LED_BUILTIN, HIGH);
}
