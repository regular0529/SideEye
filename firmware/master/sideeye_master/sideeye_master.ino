/*
 * SideEye — checkpoint 7 (final): 5-class IMU turn signal + real ESPNOW
 * vehicle check + comm fail-safe.
 * (PDR_SideEye.md section 8, rows "3. IMU 학습" / "4. 턴시그널" / "5. 비전")
 *
 * Architecture decision (2026-08-06, after two failed attempts to run both
 * SideEYE_inferencing and SideEYEVision_inferencing on this one board):
 * Edge Impulse's Arduino library export is not safe to combine two different
 * models in one sketch -- both export the same relative header path
 * (model-parameters/model_metadata.h) AND the same include guard, so
 * whichever the Arduino build resolves first silently wins for BOTH models'
 * macros (proven with a static_assert), and even after forcing that fix,
 * their precompiled TFLite Micro runtime objects still collide at link time
 * and corrupt memory at inference. Real fix would need binary symbol
 * renaming -- too large a change to land safely before presentation.
 *
 * So: this board runs ONLY the IMU model (idle/left/right/stop/helmet_on,
 * 5-class). Vehicle detection is real vision, but only on the slave's own
 * camera (firmware/slave/checkpoint7_vision_integrated, proven stable --
 * SideEYEVision_inferencing is the *only* EI library linked there):
 *   - left-lean (slave's side, PDR section 4) -> CMD_CAPTURE_REQUEST over
 *     ESPNOW; slave classifies with its camera and replies the real
 *     vehicleFound bit.
 *   - right-lean (master's own side) -> no camera model on this board
 *     (see above), so vehicle presence is stubbed true, same as the
 *     original checkpoint4. Turn signal and stop/helmet_on classification
 *     are unaffected by this -- only the right-side buzzer confirmation is
 *     not a real camera check.
 *
 * ESPNOW fail-safe protocol (left side only, since only the slave request
 * involves the network):
 *   1. Every request carries a sequence number; onReceive() only accepts a
 *      reply whose sequence matches the one currently outstanding -- stale
 *      or duplicate replies (e.g. from a retry) are dropped.
 *   2. If no matching reply arrives within ESPNOW_TIMEOUT_MS, the request is
 *      resent once. Two timeouts (2 * 700ms = 1400ms) still fit inside the
 *      5-blink window (3000ms), so a comms failure never delays the blink
 *      cadence itself.
 *   3. If the retry also times out, triggerCommFailureAlarm() sounds a 3x
 *      1000Hz beep (distinct from the single 2000Hz vehicle-found beep) so
 *      the rider can tell "no vehicle" apart from "this side's blind-spot
 *      check isn't working right now". The turn signal itself is never
 *      blocked by any of this.
 *
 * 5-class IMU model note: the delivered SideEYE_inferencing library's label
 * set is actually 7 classes (background/helmet_on/idle/left/right/stop/
 * vehicle) -- "background"/"vehicle" leaked in from vision data that shared
 * the same Edge Impulse project earlier in the course of this build. Only
 * left/right drive the turn signal; every other label (including the two
 * contaminated ones) falls through to "no turn signal", so it doesn't
 * affect correctness here, just noted for whoever retrains this model next.
 *
 * MAC (re-measured 2026-08-06 after board/port reshuffle -- do not trust
 * MACs recorded in older checkpoint comments, ports move):
 *   master (this board)  AC:27:6E:A8:47:80  COM14
 *   slave (peer)          AC:27:6E:A8:42:08  COM15
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
constexpr uint32_t ESPNOW_TIMEOUT_MS = 700;

// Passive piezo buzzer -- tone() only drives a fixed-swing square wave, so
// use the LEDC PWM peripheral directly instead: duty cycle is the volume
// knob (buzzer_test.ino). Loudest is 50% duty (128/255), NOT 100% -- 100%
// duty is just a constant-high signal (no oscillation), so the piezo barely
// moves and it goes quiet. 50% is the real max-volume square wave.
constexpr int BUZZER_VOLUME = 128;
void buzzerBeep(int freqHz, int ms) {
  ledcChangeFrequency(BUZZER_PIN, freqHz, 8);
  ledcWrite(BUZZER_PIN, BUZZER_VOLUME);
  delay(ms);
  ledcWrite(BUZZER_PIN, 0);
}

// Turn-signal spec (regular spec, applies to every checkpoint -- see
// turn_signal_live.ino): both directions light the same amber color, only
// which half of the ring differs, and blink 5x at real-car cadence
// (300ms on + 300ms off = 3000ms for 5 blinks).
#define AMBER_R 255
#define AMBER_G 191
#define AMBER_B 0
// Stop light (PDR roadmap: "정지 시 정지등 적색") -- full ring, not half,
// so it reads as a brake light rather than a directional indicator.
#define STOP_R 255
#define STOP_G 0
#define STOP_B 0
// Helmet-on confirmation (PDR roadmap) -- green, fills the full ring.
#define HELMET_R 0
#define HELMET_G 255
#define HELMET_B 0
constexpr uint32_t HELMET_FILL_STEP_MS = 60;
constexpr uint32_t HELMET_HOLD_MS = 500;
constexpr int HALF_COUNT = NEOPIXEL_COUNT / 2;
constexpr int LEFT_HALF_START = HALF_COUNT;
constexpr int RIGHT_HALF_START = 0;
constexpr int BLINK_COUNT = 5;
constexpr uint32_t BLINK_ON_MS = 300;
constexpr uint32_t BLINK_OFF_MS = 300;

Adafruit_BNO055 bno(55, BNO_ADDR, &Wire);
uint8_t slaveMac[6] = {0xAC, 0x27, 0x6E, 0xA8, 0x42, 0x08};
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

// ---- ESPNOW round trip (fail-safe: sequence match + timeout + 1 retry) ----
volatile bool replyPending = false;
uint8_t replyVehicleFound = 0;
uint32_t replySequence = 0;
uint32_t pendingSequence = 0;
bool awaitingReply = false;
uint32_t requestSentAt = 0;

void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int length) {
  if (length != static_cast<int>(sizeof(EspNowMessage))) return;
  const EspNowMessage *msg = reinterpret_cast<const EspNowMessage *>(data);
  if (msg->magic != SIDEEYE_MSG_MAGIC || msg->version != SIDEEYE_MSG_VERSION) return;
  if (msg->command != CMD_RESULT_REPLY) return;
  replyVehicleFound = msg->vehicleFound;
  replySequence = msg->sequence;
  replyPending = true;
}

void sendCaptureRequest() {
  EspNowMessage msg = {};
  msg.magic = SIDEEYE_MSG_MAGIC;
  msg.version = SIDEEYE_MSG_VERSION;
  msg.command = CMD_CAPTURE_REQUEST;
  msg.sequence = ++sequenceNumber;
  pendingSequence = msg.sequence;
  awaitingReply = true;
  replyVehicleFound = 0;  // clear any stale value from a previous request before this one can be answered
  requestSentAt = millis();
  esp_err_t sendResult = esp_now_send(slaveMac, reinterpret_cast<const uint8_t *>(&msg), sizeof(msg));
  Serial.printf("[espnow] send seq=%lu result=%d (%s)\n", msg.sequence, sendResult, esp_err_to_name(sendResult));
}

// Loud and lasts as long as one full turn-signal cycle (BLINK_COUNT *
// (BLINK_ON_MS+BLINK_OFF_MS) = 3000ms) so it's audible for as long as the
// blink itself is running -- vehicle-found is the actual danger signal, it
// needs to stand out, not just chirp once.
void triggerAlert(const char *direction, uint32_t roundTripMs) {
  uint32_t alarmUntil = millis() + BLINK_COUNT * (BLINK_ON_MS + BLINK_OFF_MS);
  while (millis() < alarmUntil) {
    buzzerBeep(2500, 150);
    delay(50);
  }
  Serial.printf("ALERT: %s vehicleFound, round trip %lums (target <1000ms)\n", direction, roundTripMs);
}

// Distinct pattern from triggerAlert (1000Hz x3 vs. the vehicle alert's
// rapid 2500Hz x4) -- only sounded once, by bootCommCheck() at startup (see
// setup()), not on every lean event: a mid-ride comms hiccup just silently
// skips that one vehicle check (see pollReplyDuring), it doesn't need to
// interrupt the rider every time.
void triggerCommFailureAlarm() {
  for (int i = 0; i < 3; i++) {
    buzzerBeep(1000, 120);
    delay(40);
  }
  Serial.println("ALARM: slave comm failure");
}

// Waits `ms`, keeping the slave round trip alive the whole time so an alert
// arriving during the 5-blink block (3s) isn't delayed behind it. Also owns
// the fail-safe: drops replies whose sequence doesn't match what's currently
// outstanding, and retries once on timeout. A retry that also times out is
// logged and silently skipped for this one lean event -- see
// triggerCommFailureAlarm()'s comment for why it doesn't alarm here.
void pollReplyDuring(uint32_t ms) {
  uint32_t until = millis() + ms;
  while (millis() < until) {
    if (replyPending) {
      replyPending = false;
      if (awaitingReply && replySequence == pendingSequence) {
        awaitingReply = false;
        if (replyVehicleFound) triggerAlert("left", millis() - requestSentAt);
        else Serial.println("[espnow] slave replied: no vehicle");
      }  // else: stale/foreign reply, drop
    }
    if (awaitingReply && millis() - requestSentAt > ESPNOW_TIMEOUT_MS) {
      static bool retried = false;
      if (!retried) {
        retried = true;
        Serial.println("[espnow] timeout, retrying once");
        sendCaptureRequest();
      } else {
        retried = false;
        awaitingReply = false;
        Serial.println("[espnow] slave unreachable, skipping this vehicle check");
      }
    }
    delay(5);
  }
}

// One-time link check, called from setup() only (see file header + PDR "처음
// 착용" note) -- confirms the slave is reachable once at power-on. Runs its
// own request/retry independent of pollReplyDuring's per-blink state.
void bootCommCheck() {
  Serial.println("[boot] checking slave link...");
  for (int attempt = 0; attempt < 2; attempt++) {
    sendCaptureRequest();
    uint32_t start = millis();
    while (millis() - start < ESPNOW_TIMEOUT_MS) {
      if (replyPending) {
        replyPending = false;
        if (awaitingReply && replySequence == pendingSequence) {
          awaitingReply = false;
          Serial.println("[boot] slave link OK");
          return;
        }
      }
      delay(5);
    }
  }
  awaitingReply = false;
  triggerCommFailureAlarm();
}

void blinkHalf(int start, int count) {
  for (int i = 0; i < BLINK_COUNT; i++) {
    neoPixelShowHalf(start, count, AMBER_R, AMBER_G, AMBER_B);
    pollReplyDuring(BLINK_ON_MS);
    neoPixelShow(0, 0, 0);
    pollReplyDuring(BLINK_OFF_MS);
  }
}

// Full-ring version for the stop light -- no ESPNOW involved, so plain
// delay() instead of pollReplyDuring().
void blinkFull(uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < BLINK_COUNT; i++) {
    neoPixelShow(r, g, b);
    delay(BLINK_ON_MS);
    neoPixelShow(0, 0, 0);
    delay(BLINK_OFF_MS);
  }
}

// Progressive green fill around the full ring (helmet-on confirmation),
// holds lit briefly, then clears.
void helmetOnAnimation() {
  uint8_t perPixel[NEOPIXEL_COUNT][3] = {};
  for (int i = 0; i < NEOPIXEL_COUNT; i++) {
    perPixel[i][0] = HELMET_R; perPixel[i][1] = HELMET_G; perPixel[i][2] = HELMET_B;
    neoPixelShowPixels(perPixel);
    delay(HELMET_FILL_STEP_MS);
  }
  delay(HELMET_HOLD_MS);
  neoPixelShow(0, 0, 0);
}

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("=== SideEye checkpoint 7: 5-class IMU + ESPNOW fail-safe ===");

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
  ledcAttach(BUZZER_PIN, 2000, 8);  // 8-bit duty; buzzerBeep() sets freq/duty per call

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
  esp_err_t peerResult = esp_now_add_peer(&peerInfo);
  Serial.printf("[espnow] add_peer result=%d (%s), WiFi channel now=%d\n",
                peerResult, esp_err_to_name(peerResult), WiFi.channel());
  bootCommCheck();

  Serial.printf("IMU model: %d samples/window @ %dHz, %d labels\n",
                EI_CLASSIFIER_RAW_SAMPLE_COUNT, EI_CLASSIFIER_FREQUENCY, EI_CLASSIFIER_LABEL_COUNT);
  Serial.println("Ready");
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
// helmet_on gets a higher confidence bar than the other labels -- the
// contaminated 7-class model (see file header) is noisy enough that
// helmet_on was firing on ordinary movement at the default argmax threshold.
constexpr float HELMET_ON_MIN_CONFIDENCE = 0.85f;
constexpr float LEFT_BIAS_PENALTY = 0.15f;
// left/right were firing on borderline classifications (seen as low as
// 0.50-0.58 in testing) -- ordinary jitter, not an intentional lean. Require
// a clearer margin before the turn signal actually fires.
constexpr float TURN_SIGNAL_MIN_CONFIDENCE = 0.75f;

void applyTurnSignal(const char *label, float confidence) {
  currentState = label;

  // pixels 0 and 6 stay dark (excluded on request) -- skip the first pixel
  // of each half, light the remaining 5. See turn_signal_live.ino.
  if (strcmp(label, "left") == 0 && confidence < TURN_SIGNAL_MIN_CONFIDENCE) {
    neoPixelShow(0, 0, 0);
  } else if (strcmp(label, "right") == 0 && confidence < TURN_SIGNAL_MIN_CONFIDENCE) {
    neoPixelShow(0, 0, 0);
  } else if (strcmp(label, "left") == 0) {
    sendCaptureRequest();  // slave's side -- real camera check, reply polled during the blink
    blinkHalf(LEFT_HALF_START + 1, HALF_COUNT - 1);
  } else if (strcmp(label, "right") == 0) {
    // master's own side has no camera model on this board (see file
    // header), so there's no real vehicle check to alarm on -- just the
    // turn signal itself, no vehicle-found beep for a check that isn't real.
    blinkHalf(RIGHT_HALF_START + 1, HALF_COUNT - 1);
  } else if (strcmp(label, "stop") == 0) {
    blinkFull(STOP_R, STOP_G, STOP_B);
  } else if (strcmp(label, "helmet_on") == 0 && confidence >= HELMET_ON_MIN_CONFIDENCE) {
    helmetOnAnimation();
  } else {
    neoPixelShow(0, 0, 0);
  }
}

void loop() {
  if (replyPending) {
    replyPending = false;
    if (awaitingReply && replySequence == pendingSequence) {
      awaitingReply = false;
      if (replyVehicleFound) triggerAlert("left", millis() - requestSentAt);
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

  // "left" comes out biased on this model (fires far more than "right" for
  // similar motions) -- penalize it slightly before picking the winner so it
  // has to be clearly ahead, not just barely, to win the argmax.
  int bestIndex = 0;
  float bestScore = result.classification[0].value -
                     (strcmp(ei_classifier_inferencing_categories[0], "left") == 0 ? LEFT_BIAS_PENALTY : 0.0f);
  for (int i = 1; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    float score = result.classification[i].value -
                   (strcmp(ei_classifier_inferencing_categories[i], "left") == 0 ? LEFT_BIAS_PENALTY : 0.0f);
    if (score > bestScore) { bestScore = score; bestIndex = i; }
  }
  const char *label = ei_classifier_inferencing_categories[bestIndex];
  Serial.printf("[imu] %s %.2f\n", label, result.classification[bestIndex].value);
  applyTurnSignal(label, result.classification[bestIndex].value);
}
