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

// PDR_SideEye.md section 3 color contract
// orange/yellow were indistinguishable at USB-safe brightness -- switched to
// red/blue for maximum visual contrast (2026-08-05).
#define COLOR_LEFT_R  255
#define COLOR_LEFT_G  0
#define COLOR_LEFT_B  0     // red
#define COLOR_RIGHT_R 0
#define COLOR_RIGHT_G 0
#define COLOR_RIGHT_B 255   // blue

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

void neoPixelShow(uint8_t red, uint8_t green, uint8_t blue) {
  red = (uint16_t)red * BRIGHTNESS_SCALE / 255;
  green = (uint16_t)green * BRIGHTNESS_SCALE / 255;
  blue = (uint16_t)blue * BRIGHTNESS_SCALE / 255;
  size_t symbolIndex = 0;
  for (int pixel = 0; pixel < NEOPIXEL_COUNT; pixel++) {
    const uint8_t grb[] = {green, red, blue};
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

// Debounce: a lean-then-return-to-center swing produces a brief opposite-
// direction signal the model was never trained on (training data was
// static held-lean windows, not the full natural gesture), so a single
// opposite-label window right after a real lean is very likely that
// return swing, not a real direction change. idle is always accepted
// immediately (fail-safe: never delay turning the signal OFF). Switching
// between left and right requires two consecutive matching windows.
const char *currentState = "idle";
const char *pendingState = "idle";
int pendingCount = 0;

void applyDebounced(const char *label) {
  bool isIdle = strcmp(label, "idle") == 0;
  bool sameAsCurrent = strcmp(label, currentState) == 0;

  if (isIdle || sameAsCurrent) {
    currentState = isIdle ? "idle" : label;
    pendingState = currentState;
    pendingCount = 0;
  } else {
    // candidate direction change away from current non-idle state
    if (strcmp(label, pendingState) == 0) {
      pendingCount++;
    } else {
      pendingState = label;
      pendingCount = 1;
    }
    if (pendingCount >= 2) {
      currentState = pendingState;
      pendingCount = 0;
    }
  }

  if (strcmp(currentState, "left") == 0) {
    neoPixelShow(COLOR_LEFT_R, COLOR_LEFT_G, COLOR_LEFT_B);
  } else if (strcmp(currentState, "right") == 0) {
    neoPixelShow(COLOR_RIGHT_R, COLOR_RIGHT_G, COLOR_RIGHT_B);
  } else {
    neoPixelShow(0, 0, 0);
  }
}
