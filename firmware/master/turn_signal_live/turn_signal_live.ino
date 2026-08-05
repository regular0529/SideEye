/*
 * SideEye — live turn-signal test (checkpoint 3.3 on-device deployment).
 *
 * Streams BNO055 linear accel + gyro at 50Hz into a rolling window, runs the
 * trained Edge Impulse classifier (idle/left/right) every time the window
 * fills, and drives the NeoPixel turn-signal color per PDR_SideEye.md
 * section 3: orange while classified left, yellow while classified right,
 * off while idle. No camera/ESPNOW/buzzer here -- this sketch only proves
 * IMU classification -> visual turn signal end to end.
 *
 * Wiring: same as checkpoint1_hw_test (PDR_SideEye.md section 4).
 * Library: SideEYE_inferencing (installed from the trained EI model,
 *          see NEOPIXEL_DEBUGGING.md-style notes in imu_harness/).
 */
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include <SideEYE_inferencing.h>

#define BNO_ADDR       0x29
#define NEOPIXEL_PIN   2   // GPIO2 / D1
#define NEOPIXEL_COUNT 12
constexpr uint32_t SAMPLE_INTERVAL_MS = 1000 / EI_CLASSIFIER_FREQUENCY;  // 20ms @ 50Hz

// Real turn-signal look (2026-08-06): both directions use the same amber
// color; only WHICH HALF of the 12-pixel ring lights up differs, like an
// actual vehicle blinker. Pixel index order depends on the ring's internal
// wiring direction -- if left/right come out swapped on the physical
// helmet, swap LEFT_HALF_START and RIGHT_HALF_START.
#define AMBER_R 255
#define AMBER_G 191
#define AMBER_B 0
constexpr int HALF_COUNT = NEOPIXEL_COUNT / 2;
constexpr int LEFT_HALF_START = HALF_COUNT;
constexpr int RIGHT_HALF_START = 0;

Adafruit_BNO055 bno(55, BNO_ADDR, &Wire);

// ---- direct-RMT WS2812 driver (see checkpoint1_hw_test / NEOPIXEL_DEBUGGING.md) ----
constexpr uint32_t RMT_RESOLUTION_HZ = 10 * 1000 * 1000;
constexpr size_t SYMBOLS_PER_PIXEL = 24;
constexpr uint8_t BRIGHTNESS_SCALE = 64;  // out of 255, USB-safe for 12 pixels
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
  channelConfig.flags.allow_pd = 0;  // ESP32-S3 cannot retain RMT through light sleep
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

// Lights `count` contiguous pixels starting at `start` in the given color,
// everything else off -- mimics a real turn-signal blinker (one side lit).
void neoPixelShowHalf(int start, int count, uint8_t red, uint8_t green, uint8_t blue) {
  uint8_t perPixel[NEOPIXEL_COUNT][3] = {};
  for (int n = 0; n < count; n++) {
    int i = (start + n) % NEOPIXEL_COUNT;
    perPixel[i][0] = red; perPixel[i][1] = green; perPixel[i][2] = blue;
  }
  neoPixelShowPixels(perPixel);
}

// ---- rolling window buffer fed to the classifier ----
float windowBuffer[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];
size_t windowIndex = 0;
uint32_t lastSampleAt = 0;

int getWindowData(size_t offset, size_t length, float *out_ptr) {
  memcpy(out_ptr, windowBuffer + offset, length * sizeof(float));
  return 0;
}

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("=== SideEye live turn-signal test ===");

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

  Serial.printf("Model: %d samples/window @ %dHz, %d labels\n",
                EI_CLASSIFIER_RAW_SAMPLE_COUNT, EI_CLASSIFIER_FREQUENCY, EI_CLASSIFIER_LABEL_COUNT);
}

void loop() {
  uint32_t now = millis();
  if (now - lastSampleAt < SAMPLE_INTERVAL_MS) {
    return;
  }
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

  if (windowIndex < EI_CLASSIFIER_RAW_SAMPLE_COUNT) {
    return;
  }
  windowIndex = 0;  // non-overlapping windows: simplest correct behavior

  signal_t signal;
  signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
  signal.get_data = &getWindowData;

  ei_impulse_result_t result = {0};
  EI_IMPULSE_ERROR res = run_classifier(&signal, &result, false);
  if (res != EI_IMPULSE_OK) {
    Serial.printf("ERR: run_classifier failed (%d)\n", res);
    return;
  }

  int bestIndex = 0;
  for (int i = 1; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    if (result.classification[i].value > result.classification[bestIndex].value) {
      bestIndex = i;
    }
  }
  const char *label = ei_classifier_inferencing_categories[bestIndex];
  float confidence = result.classification[bestIndex].value;

  Serial.printf("[%s] %.2f (dsp %dms, classify %dms)\n",
                label, confidence, result.timing.dsp, result.timing.classification);

  applyDebounced(label);
}

// Hold-then-lock: react instantly the moment a lean is classified (no
// waiting for confirmation -- a real turn signal must snap on immediately).
// The lean-then-return-to-center swing produces a brief opposite-direction
// signal the model was never trained on (training data was static held-lean
// windows, not the full natural gesture) -- instead of trying to tell that
// apart from a real direction change, just hold whatever state a left/right
// trigger set for HOLD_MS and ignore every classification in between.
constexpr uint32_t HOLD_MS = 3000;
const char *currentState = "idle";
uint32_t holdUntil = 0;

void applyDebounced(const char *label) {
  if (millis() < holdUntil) {
    return;  // locked -- ignore classification, keep showing currentState
  }

  currentState = label;
  if (strcmp(label, "left") == 0 || strcmp(label, "right") == 0) {
    holdUntil = millis() + HOLD_MS;
  }

  if (strcmp(currentState, "left") == 0) {
    neoPixelShowHalf(LEFT_HALF_START, HALF_COUNT, AMBER_R, AMBER_G, AMBER_B);
  } else if (strcmp(currentState, "right") == 0) {
    neoPixelShowHalf(RIGHT_HALF_START, HALF_COUNT, AMBER_R, AMBER_G, AMBER_B);
  } else {
    neoPixelShow(0, 0, 0);
  }
}
