/*
 * SideEye — checkpoint 4: turn signal + ESPNOW alert round trip.
 * (PDR_SideEye.md section 8, row "4. 턴시그널 (경보 로직 통합)")
 *
 * Runs the IMU classifier (as in turn_signal_live.ino) to drive the
 * NeoPixel turn-signal color instantly on lean, independent of any vehicle
 * check (PDR section 3: NeoPixel = turn signal, always on lean). On a
 * left-lean (slave's side, PDR section 4), sends CMD_CAPTURE_REQUEST to the
 * slave over ESPNOW and waits for CMD_RESULT_REPLY -- the slave has no
 * camera yet (checkpoint 5) so it stubs vehicleFound=1 immediately
 * (firmware/slave/checkpoint4_capture_stub). On a right-lean (master's own
 * side), the "capture" is stubbed locally the same way, no ESPNOW needed.
 * Once vehicleFound is true, fires the buzzer (silent -- GND intentionally
 * unconnected in the classroom, PDR section 4) and logs round-trip latency
 * against the <1s MVP acceptance target (PDR section 11).
 *
 * MAC (checkpoint 2 verified, ESPNOW_Peer_Test/TEAM_CONFIG.md):
 *   master (this board)  1C:DB:D4:74:49:E0  COM13
 *   slave (peer)          AC:27:6E:A8:47:80  COM14
 */
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <WiFi.h>
#include <cstring>
#include "esp_now.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include <SideEYE_inferencing.h>
#include "../../shared/protocol.h"

#define BNO_ADDR       0x29
#define BUZZER_PIN     D0
#define NEOPIXEL_PIN   2
#define NEOPIXEL_COUNT 12
constexpr uint32_t SAMPLE_INTERVAL_MS = 1000 / EI_CLASSIFIER_FREQUENCY;
constexpr uint8_t ESPNOW_WIFI_CHANNEL = 0;

// Turn-signal spec (regular spec, applies to every checkpoint -- see
// turn_signal_live.ino): both directions light the same amber color, only
// which half of the ring differs, and blink 5x at real-car cadence
// (300ms on + 300ms off = 3000ms for 5 blinks).
#define AMBER_R 255
#define AMBER_G 191
#define AMBER_B 0
constexpr int HALF_COUNT = NEOPIXEL_COUNT / 2;
constexpr int LEFT_HALF_START = HALF_COUNT;
constexpr int RIGHT_HALF_START = 0;
constexpr int BLINK_COUNT = 5;
constexpr uint32_t BLINK_ON_MS = 300;
constexpr uint32_t BLINK_OFF_MS = 300;

Adafruit_BNO055 bno(55, BNO_ADDR, &Wire);
uint8_t slaveMac[6] = {0xAC, 0x27, 0x6E, 0xA8, 0x47, 0x80};
uint32_t sequenceNumber = 0;

// ---- direct-RMT WS2812 driver (see NEOPIXEL_DEBUGGING.md) ----
constexpr uint32_t RMT_RESOLUTION_HZ = 10 * 1000 * 1000;
constexpr size_t SYMBOLS_PER_PIXEL = 24;
constexpr uint8_t BRIGHTNESS_SCALE = 64;
rmt_channel_handle_t rmtChannel = nullptr;
rmt_encoder_handle_t copyEncoder = nullptr;
rmt_symbol_word_t frame[NEOPIXEL_COUNT * SYMBOLS_PER_PIXEL];

void neoPixelBegin() {
  rmt_tx_channel_config_t channelConfig = {};
  channelConfig.gpio_num = static_cast<gpio_num_t>(NEOPIXEL_PIN);
  channelConfig.clk_src = RMT_CLK_SRC_DEFAULT;
  channelConfig.resolution_hz = RMT_RESOLUTION_HZ;
  channelConfig.mem_block_symbols = 64;
  channelConfig.trans_queue_depth = 1;
  channelConfig.flags.allow_pd = 0;
  rmt_new_tx_channel(&channelConfig, &rmtChannel);
  rmt_copy_encoder_config_t encoderConfig = {};
  rmt_new_copy_encoder(&encoderConfig, &copyEncoder);
  rmt_enable(rmtChannel);
}

// perPixel[i] = {r,g,b} for pixel i. Lets callers light only some pixels.
void neoPixelShowPixels(const uint8_t perPixel[NEOPIXEL_COUNT][3]) {
  size_t symbolIndex = 0;
  for (int pixel = 0; pixel < NEOPIXEL_COUNT; pixel++) {
    uint8_t r = (uint16_t)perPixel[pixel][0] * BRIGHTNESS_SCALE / 255;
    uint8_t g = (uint16_t)perPixel[pixel][1] * BRIGHTNESS_SCALE / 255;
    uint8_t b = (uint16_t)perPixel[pixel][2] * BRIGHTNESS_SCALE / 255;
    const uint8_t grb[] = {g, r, b};
    for (int component = 0; component < 3; component++) {
      for (int bit = 7; bit >= 0; bit--) {
        const bool one = grb[component] & (1 << bit);
        rmt_symbol_word_t &symbol = frame[symbolIndex++];
        symbol.level0 = 1;
        symbol.duration0 = one ? 8 : 4;
        symbol.level1 = 0;
        symbol.duration1 = one ? 5 : 9;
      }
    }
  }
  rmt_transmit_config_t transmitConfig = {};
  rmt_transmit(rmtChannel, copyEncoder, frame, sizeof(frame), &transmitConfig);
  rmt_tx_wait_all_done(rmtChannel, -1);
}

void neoPixelShow(uint8_t red, uint8_t green, uint8_t blue) {
  uint8_t perPixel[NEOPIXEL_COUNT][3];
  for (int i = 0; i < NEOPIXEL_COUNT; i++) { perPixel[i][0] = red; perPixel[i][1] = green; perPixel[i][2] = blue; }
  neoPixelShowPixels(perPixel);
}

void neoPixelShowHalf(int start, int count, uint8_t red, uint8_t green, uint8_t blue) {
  uint8_t perPixel[NEOPIXEL_COUNT][3] = {};
  for (int n = 0; n < count; n++) {
    int i = (start + n) % NEOPIXEL_COUNT;
    perPixel[i][0] = red; perPixel[i][1] = green; perPixel[i][2] = blue;
  }
  neoPixelShowPixels(perPixel);
}

// ---- turn-signal state ----
const char *currentState = "idle";

// ---- ESPNOW round trip ----
volatile bool replyPending = false;
uint8_t replyVehicleFound = 0;
uint32_t requestSentAt = 0;

void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int length) {
  if (length != static_cast<int>(sizeof(EspNowMessage))) return;
  const EspNowMessage *msg = reinterpret_cast<const EspNowMessage *>(data);
  if (msg->magic != SIDEEYE_MSG_MAGIC || msg->version != SIDEEYE_MSG_VERSION) return;
  if (msg->command != CMD_RESULT_REPLY) return;
  replyVehicleFound = msg->vehicleFound;
  replyPending = true;
}

void requestSlaveCapture() {
  EspNowMessage msg = {};
  msg.magic = SIDEEYE_MSG_MAGIC;
  msg.version = SIDEEYE_MSG_VERSION;
  msg.command = CMD_CAPTURE_REQUEST;
  msg.sequence = ++sequenceNumber;
  requestSentAt = millis();
  esp_now_send(slaveMac, reinterpret_cast<const uint8_t *>(&msg), sizeof(msg));
}

void triggerAlert(uint32_t roundTripMs) {
  tone(BUZZER_PIN, 2000, 200);  // silent: GND intentionally unconnected
  Serial.printf("ALERT: vehicleFound, round trip %lums (target <1000ms)\n", roundTripMs);
}

// Waits `ms` but keeps checking for the ESPNOW reply the whole time --
// without this, an alert arriving during the 5-blink block (3s) would sit
// unprocessed until the blink finished, adding up to 3s of fake latency to
// the round-trip number.
void pollReplyDuring(uint32_t ms) {
  uint32_t until = millis() + ms;
  while (millis() < until) {
    if (replyPending) {
      replyPending = false;
      if (replyVehicleFound) triggerAlert(millis() - requestSentAt);
    }
    delay(5);
  }
}

void blinkHalf(int start, int count) {
  for (int i = 0; i < BLINK_COUNT; i++) {
    neoPixelShowHalf(start, count, AMBER_R, AMBER_G, AMBER_B);
    pollReplyDuring(BLINK_ON_MS);
    neoPixelShow(0, 0, 0);
    pollReplyDuring(BLINK_OFF_MS);
  }
}

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("=== SideEye checkpoint 4: turn signal + ESPNOW round trip ===");

  Wire.begin(D4, D5);
  Wire.setClock(100000);
  if (!bno.begin()) {
    Serial.println("ERROR: BNO055 begin() failed");
    while (true) delay(1000);
  }
  delay(1000);
  // do NOT call setExtCrystalUse(true) -- see xiao-i2c-sensors skill pitfall 2

  neoPixelBegin();
  neoPixelShow(0, 0, 0);
  pinMode(BUZZER_PIN, OUTPUT);

  WiFi.mode(WIFI_STA);
  WiFi.setChannel(ESPNOW_WIFI_CHANNEL);
  delay(100);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: esp_now_init failed");
    while (true) delay(1000);
  }
  esp_now_register_recv_cb(onReceive);
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, slaveMac, sizeof(peerInfo.peer_addr));
  peerInfo.channel = ESPNOW_WIFI_CHANNEL;
  peerInfo.ifidx = WIFI_IF_STA;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  Serial.printf("Model: %d samples/window @ %dHz\n", EI_CLASSIFIER_RAW_SAMPLE_COUNT, EI_CLASSIFIER_FREQUENCY);
}

float windowBuffer[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];
size_t windowIndex = 0;
uint32_t lastSampleAt = 0;

int getWindowData(size_t offset, size_t length, float *out_ptr) {
  memcpy(out_ptr, windowBuffer + offset, length * sizeof(float));
  return 0;
}

// Blocking for the 5-blink duration (3s) also satisfies "no new steering
// recognized while the turn signal is running" -- loop() can't sample/
// classify again until this returns, so there's no separate lock flag.
void applyTurnSignal(const char *label) {
  currentState = label;

  // pixels 0 and 6 stay dark (excluded on request) -- skip the first pixel
  // of each half, light the remaining 5. See turn_signal_live.ino.
  if (strcmp(label, "left") == 0) {
    requestSlaveCapture();  // slave's side -- ask it to "look"; reply is polled during the blink
    blinkHalf(LEFT_HALF_START + 1, HALF_COUNT - 1);
  } else if (strcmp(label, "right") == 0) {
    // master's own side -- no camera yet either, stub locally same as slave
    triggerAlert(0);
    blinkHalf(RIGHT_HALF_START + 1, HALF_COUNT - 1);
  } else {
    neoPixelShow(0, 0, 0);
  }
}

void loop() {
  if (replyPending) {
    replyPending = false;
    if (replyVehicleFound) {
      triggerAlert(millis() - requestSentAt);
    }
  }

  uint32_t now = millis();
  if (now - lastSampleAt < SAMPLE_INTERVAL_MS) return;
  lastSampleAt = now;

  imu::Vector<3> accel = bno.getVector(Adafruit_BNO055::VECTOR_LINEARACCEL);
  imu::Vector<3> gyro = bno.getVector(Adafruit_BNO055::VECTOR_GYROSCOPE);
  size_t base = windowIndex * EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME;
  windowBuffer[base + 0] = accel.x();
  windowBuffer[base + 1] = accel.y();
  windowBuffer[base + 2] = accel.z();
  windowBuffer[base + 3] = gyro.x();
  windowBuffer[base + 4] = gyro.y();
  windowBuffer[base + 5] = gyro.z();
  windowIndex++;
  if (windowIndex < EI_CLASSIFIER_RAW_SAMPLE_COUNT) return;
  windowIndex = 0;

  signal_t signal;
  signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
  signal.get_data = &getWindowData;
  ei_impulse_result_t result = {0};
  if (run_classifier(&signal, &result, false) != EI_IMPULSE_OK) return;

  int bestIndex = 0;
  for (int i = 1; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    if (result.classification[i].value > result.classification[bestIndex].value) bestIndex = i;
  }
  const char *label = ei_classifier_inferencing_categories[bestIndex];
  Serial.printf("[%s] %.2f\n", label, result.classification[bestIndex].value);
  applyTurnSignal(label);
}
