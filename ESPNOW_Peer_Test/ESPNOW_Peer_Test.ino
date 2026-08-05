#include <Arduino.h>
#include <WiFi.h>
#include <cstring>
#include "esp_now.h"

// Change only this line on each board.
// A uses B's MAC. B uses A's MAC.
const char *PEER_MAC_TEXT = "1C:DB:D4:74:49:E0";  // master's MAC (this build is for the slave)

constexpr uint8_t ESPNOW_WIFI_CHANNEL = 0;
constexpr uint8_t TOUCH_GPIO = 1;  // XIAO D0 / TOUCH1
constexpr uint32_t TOUCH_SAMPLE_MS = 50;
constexpr uint8_t CALIBRATION_SAMPLES = 32;
constexpr uint8_t CALIBRATION_DELAY_MS = 10;
constexpr uint8_t LED_ON_LEVEL = LOW;
constexpr uint8_t LED_OFF_LEVEL = HIGH;

constexpr uint32_t MESSAGE_MAGIC = 0x4B4A4731;
constexpr uint8_t MESSAGE_VERSION = 1;
constexpr uint8_t COMMAND_LED_STATE = 1;

struct __attribute__((packed)) EspNowMessage {
  uint32_t magic;
  uint8_t version;
  uint8_t command;
  uint8_t led;
  uint16_t touchRaw;
  uint32_t sequence;
};

uint8_t peerMac[6] = {};
uint8_t selfMac[6] = {};
bool peerConfigured = false;
bool ledOn = false;
bool touchState = false;
bool previousTouchState = false;
bool touchInitialized = false;
uint16_t touchBaseline = 0;
uint16_t touchThreshold = 0;
uint16_t touchRaw = 0;
uint32_t sequenceNumber = 0;
uint32_t lastTouchSampleAt = 0;

volatile bool receivedPending = false;
EspNowMessage receivedMessage = {};
uint8_t receivedFrom[6] = {};

volatile bool sendResultPending = false;
volatile esp_now_send_status_t pendingSendStatus = ESP_NOW_SEND_FAIL;
uint8_t pendingSendMac[6] = {};

void printMac(const uint8_t *mac) {
  Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

bool parseMac(const char *text, uint8_t *mac) {
  unsigned int values[6] = {};

  if (sscanf(text, "%x:%x:%x:%x:%x:%x",
             &values[0], &values[1], &values[2],
             &values[3], &values[4], &values[5]) != 6) {
    return false;
  }

  for (size_t i = 0; i < 6; ++i) {
    if (values[i] > 0xFF) {
      return false;
    }
    mac[i] = static_cast<uint8_t>(values[i]);
  }

  return true;
}

bool isZeroMac(const uint8_t *mac) {
  for (size_t i = 0; i < 6; ++i) {
    if (mac[i] != 0) {
      return false;
    }
  }
  return true;
}

void setLed(bool on) {
  ledOn = on;
  digitalWrite(LED_BUILTIN, on ? LED_ON_LEVEL : LED_OFF_LEVEL);
}

void calibrateTouch() {
  Serial.println("[TOUCH] keep D0 untouched during calibration");
  uint32_t total = 0;

  for (uint8_t sample = 0; sample < CALIBRATION_SAMPLES; ++sample) {
    total += touchRead(TOUCH_GPIO);
    delay(CALIBRATION_DELAY_MS);
  }

  touchBaseline = total / CALIBRATION_SAMPLES;
  uint32_t threshold = touchBaseline / 8;
  if (threshold < 12) {
    threshold = 12;
  }
  if (threshold > 400) {
    threshold = 400;
  }
  touchThreshold = static_cast<uint16_t>(threshold);

  Serial.printf("[TOUCH] D0 baseline=%u threshold=%u\n",
                touchBaseline, touchThreshold);
}

void sampleTouch() {
  touchRaw = touchRead(TOUCH_GPIO);
  int32_t delta = static_cast<int32_t>(touchRaw) -
                  static_cast<int32_t>(touchBaseline);
  if (delta < 0) {
    delta = -delta;
  }

  touchState = delta >= touchThreshold;

  if (!touchInitialized) {
    previousTouchState = touchState;
    touchInitialized = true;
  }
}

void sendTouchState() {
  if (!peerConfigured) {
    return;
  }

  EspNowMessage message = {};
  message.magic = MESSAGE_MAGIC;
  message.version = MESSAGE_VERSION;
  message.command = COMMAND_LED_STATE;
  message.led = touchState ? 1 : 0;
  message.touchRaw = touchRaw;
  message.sequence = ++sequenceNumber;

  const esp_err_t result = esp_now_send(
      peerMac,
      reinterpret_cast<const uint8_t *>(&message),
      sizeof(message));

  Serial.printf("[TX] touch=%u raw=%u seq=%lu result=%s\n",
                message.led,
                message.touchRaw,
                message.sequence,
                result == ESP_OK ? "queued" : "error");
}

void onSend(const uint8_t *mac, esp_now_send_status_t status) {
  memcpy(pendingSendMac, mac, sizeof(pendingSendMac));
  pendingSendStatus = status;
  sendResultPending = true;
}

void onReceive(const esp_now_recv_info_t *info,
               const uint8_t *data,
               int length) {
  if (length != static_cast<int>(sizeof(EspNowMessage))) {
    return;
  }

  if (!peerConfigured ||
      memcmp(info->src_addr, peerMac, sizeof(peerMac)) != 0) {
    return;
  }

  memcpy(&receivedMessage, data, sizeof(receivedMessage));
  memcpy(receivedFrom, info->src_addr, sizeof(receivedFrom));
  receivedPending = true;
}

void processReceivedMessage() {
  if (!receivedPending) {
    return;
  }

  receivedPending = false;

  if (receivedMessage.magic != MESSAGE_MAGIC ||
      receivedMessage.version != MESSAGE_VERSION ||
      receivedMessage.command != COMMAND_LED_STATE) {
    Serial.println("[RX] ignored: invalid message");
    return;
  }

  setLed(receivedMessage.led == 1);
  Serial.printf("[RX] from=");
  printMac(receivedFrom);
  Serial.printf(" led=%u touchRaw=%u seq=%lu\n",
                receivedMessage.led,
                receivedMessage.touchRaw,
                receivedMessage.sequence);
}

void processSendResult() {
  if (!sendResultPending) {
    return;
  }

  sendResultPending = false;
  Serial.printf("[SEND] peer=");
  printMac(pendingSendMac);
  Serial.printf(" status=%s\n",
                pendingSendStatus == ESP_NOW_SEND_SUCCESS ? "success" : "fail");
}

void printHelp() {
  Serial.println("[INFO] Touch D0 on this board to control the peer LED");
  Serial.println("[INFO] Release D0 to send LED OFF");
  Serial.println("[INFO] Both boards use this same sketch");
}

void setupEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.setChannel(ESPNOW_WIFI_CHANNEL);
  delay(100);
  WiFi.macAddress(selfMac);

  Serial.print("[MAC] self=");
  printMac(selfMac);
  Serial.println();

  if (strcmp(PEER_MAC_TEXT, "NONE") == 0) {
    Serial.println("[MAC] peer disabled");
    return;
  }

  peerConfigured = parseMac(PEER_MAC_TEXT, peerMac) && !isZeroMac(peerMac);
  if (!peerConfigured) {
    Serial.printf("[MAC] invalid PEER_MAC_TEXT: %s\n", PEER_MAC_TEXT);
    return;
  }

  Serial.print("[MAC] peer=");
  printMac(peerMac);
  Serial.println();

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] init failed");
    peerConfigured = false;
    return;
  }

  esp_now_register_send_cb(onSend);
  esp_now_register_recv_cb(onReceive);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, peerMac, sizeof(peerInfo.peer_addr));
  peerInfo.channel = ESPNOW_WIFI_CHANNEL;
  peerInfo.ifidx = WIFI_IF_STA;
  peerInfo.encrypt = false;

  const esp_err_t addResult = esp_now_add_peer(&peerInfo);
  if (addResult != ESP_OK && addResult != ESP_ERR_ESPNOW_EXIST) {
    Serial.printf("[ESP-NOW] peer add failed: %d\n", addResult);
    peerConfigured = false;
    return;
  }

  Serial.println("[ESP-NOW] ready");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== ESP-NOW A/B Peer Unit Test ===");

  pinMode(LED_BUILTIN, OUTPUT);
  setLed(false);
  calibrateTouch();
  setupEspNow();
  printHelp();
}

void loop() {
  const uint32_t now = millis();

  if (now - lastTouchSampleAt >= TOUCH_SAMPLE_MS) {
    lastTouchSampleAt = now;
    sampleTouch();

    if (touchInitialized && touchState != previousTouchState) {
      previousTouchState = touchState;
      sendTouchState();
      Serial.printf("[TOUCH] D0=%u state=%u\n",
                    touchRaw, touchState ? 1 : 0);
    }
  }

  processReceivedMessage();
  processSendResult();
}
