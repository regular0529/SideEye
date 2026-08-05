/*
 * SideEye — checkpoint 1: master board hardware bring-up
 * (PDR_SideEye.md section 8, row "1. 하드웨어 구성")
 *
 * Verifies BNO055 IMU is alive and fusing, then exercises the NeoPixel
 * (left/right turn-signal colors) and the buzzer signal line.
 *
 * NeoPixel driver note (2026-08-05): Adafruit_NeoPixel's RMT init on
 * arduino-esp32 core 3.2.0 leaves `flags.allow_pd` uninitialized, which can
 * fail WS2812 transmission on ESP32-S3 (observed as a totally dark strip
 * with everything else electrically verified fine). This sketch drives the
 * WS2812B ring directly via the ESP-IDF `driver/rmt_tx.h` API instead, with
 * `allow_pd = 0` set explicitly. See `firmware/master/neopixel_direct_rmt_test/`
 * for the isolated proof-of-fix.
 *
 * Wiring (jumper wires, no PCB — PDR_SideEye.md section 4):
 *   BNO055   VIN -> 3.3V-OUT   GND -> GND   SCL -> D5/GPIO6   SDA -> D4/GPIO5
 *            ADD -> floating (address stays 0x29)
 *   NeoPixel (WCMCU-2812B-12 ring) VCC -> 5V/VBUS   GND -> GND   DI -> D1/GPIO2
 *   Buzzer   signal -> D0/GPIO1   GND -> intentionally NOT connected yet
 *            (classroom — keeps the buzzer silent while still exercising
 *             the tone() call path; connect GND before the real demo)
 *
 * Libraries:
 *   arduino-cli lib install "Adafruit BNO055"
 *
 * Build:  arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 -u -p <PORT> checkpoint1_hw_test
 * Watch:  mon.ps1 -Port <PORT>
 */
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"

#define BNO_ADDR       0x29
#define BUZZER_PIN     D0
#define NEOPIXEL_PIN   2   // GPIO2 / D1
#define NEOPIXEL_COUNT 12
// LED_BUILTIN (GPIO21) is inverted: LOW = on, HIGH = off.

// PDR_SideEye.md section 3 color contract (red/blue -- orange/yellow were
// indistinguishable at USB-safe brightness, changed 2026-08-05)
#define COLOR_LEFT_R  255
#define COLOR_LEFT_G  0
#define COLOR_LEFT_B  0     // red
#define COLOR_RIGHT_R 0
#define COLOR_RIGHT_G 0
#define COLOR_RIGHT_B 255   // blue
#define COLOR_OK_R    0
#define COLOR_OK_G    255
#define COLOR_OK_B    0     // green
#define COLOR_FAIL_R  255
#define COLOR_FAIL_G  0
#define COLOR_FAIL_B  0     // red
#define COLOR_HEARTBEAT_R 0
#define COLOR_HEARTBEAT_G 0
#define COLOR_HEARTBEAT_B 255  // blue

Adafruit_BNO055 bno(55, BNO_ADDR, &Wire);

constexpr uint32_t RMT_RESOLUTION_HZ = 10 * 1000 * 1000;
constexpr size_t SYMBOLS_PER_PIXEL = 24;
rmt_channel_handle_t rmtChannel = nullptr;
rmt_encoder_handle_t copyEncoder = nullptr;
rmt_symbol_word_t frame[NEOPIXEL_COUNT * SYMBOLS_PER_PIXEL];

int testsRun = 0, testsPassed = 0;

void check(const char *name, bool ok, const char *detail = "") {
  testsRun++;
  if (ok) testsPassed++;
  Serial.printf("[%s] %-32s %s\n", ok ? "PASS" : "FAIL", name, detail);
}

bool i2cPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// ---- direct-RMT WS2812 driver (bypasses the Adafruit_NeoPixel allow_pd bug) ----

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

// USB-safe brightness scale for 12 pixels (NEOPIXEL_DEBUGGING.md): full 255
// on all 12 LEDs can pull more current than a USB port safely supplies.
constexpr uint8_t BRIGHTNESS_SCALE = 64;  // out of 255

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
        symbol.duration0 = one ? 8 : 4;  // 0.8 us or 0.4 us high
        symbol.level1 = 0;
        symbol.duration1 = one ? 5 : 9;  // 0.5 us or 0.9 us low
      }
    }
  }

  rmt_transmit_config_t transmitConfig = {};
  rmt_transmit(rmtChannel, copyEncoder, frame, sizeof(frame), &transmitConfig);
  rmt_tx_wait_all_done(rmtChannel, -1);
}

void neoPixelOff() {
  neoPixelShow(0, 0, 0);
}

void blinkColor(uint8_t r, uint8_t g, uint8_t b, int times, int onMs = 150, int offMs = 150) {
  for (int i = 0; i < times; i++) {
    neoPixelShow(r, g, b);
    delay(onMs);
    neoPixelOff();
    delay(offMs);
  }
}

// Halts here blinking red forever -- this IS the failure report when there
// is no serial monitor to read.
void failForever(const char *reason) {
  Serial.print("FATAL: ");
  Serial.println(reason);
  while (true) {
    blinkColor(COLOR_FAIL_R, COLOR_FAIL_G, COLOR_FAIL_B, 3, 200, 200);
    delay(600);
  }
}

void setup() {
  Serial.begin(115200);
  delay(3000);

  Serial.println();
  Serial.println("=== SideEye checkpoint 1: master hardware test ===");

  // ---- NeoPixel comes up first so it can report everything after it ----
  neoPixelBegin();
  neoPixelOff();
  blinkColor(255, 255, 255, 1, 400, 0);  // white: "board is alive" flash

  // ---- IMU ----
  Wire.begin(D4, D5);
  Wire.setClock(100000);

  bool present = i2cPresent(BNO_ADDR);
  check("BNO055 i2c ack at 0x29", present);
  if (!present) {
    failForever("nothing at 0x29 -- check SDA/SCL wiring, ADD pin must float");
  }

  bool began = bno.begin();
  check("BNO055 begin()", began);
  if (!began) {
    failForever("BNO055 begin() failed");
  }

  delay(1000);  // let power-on self test settle
  // do NOT call setExtCrystalUse(true) -- see xiao-i2c-sensors skill pitfall 2
  uint8_t selfTest = 0, sysStat = 0, sysErr = 0;
  bno.getSystemStatus(&sysStat, &selfTest, &sysErr);
  char d[64];
  snprintf(d, sizeof(d), "ST=0x%02X stat=%u err=%u", selfTest, sysStat, sysErr);
  bool fusionOk = (sysStat == 5);
  check("BNO055 fusion mode active (stat=5)", fusionOk, d);
  if (!fusionOk) {
    failForever("fusion not running -- likely setExtCrystalUse or wiring issue");
  }

  imu::Vector<3> g = bno.getVector(Adafruit_BNO055::VECTOR_GRAVITY);
  float mag = sqrtf(g.x() * g.x() + g.y() * g.y() + g.z() * g.z());
  snprintf(d, sizeof(d), "|g| = %.2f m/s^2", mag);
  bool gravityOk = (mag > 9.0f && mag < 10.5f);
  check("BNO055 gravity magnitude 9.0..10.5", gravityOk, d);
  if (!gravityOk) {
    failForever("gravity magnitude out of range -- IMU not reading correctly");
  }

  Serial.println("IMU healthy -- NeoPixel will blink GREEN x3");
  blinkColor(COLOR_OK_R, COLOR_OK_G, COLOR_OK_B, 3, 250, 250);

  // ---- turn-signal color demo (visual only, PDR color contract) ----
  Serial.println("MANUAL CHECK: watch the NeoPixel -- orange x5 (left) then yellow x5 (right)");
  delay(500);
  blinkColor(COLOR_LEFT_R, COLOR_LEFT_G, COLOR_LEFT_B, 5);
  delay(500);
  blinkColor(COLOR_RIGHT_R, COLOR_RIGHT_G, COLOR_RIGHT_B, 5);
  neoPixelOff();

  // ---- Buzzer ----
  pinMode(BUZZER_PIN, OUTPUT);
  tone(BUZZER_PIN, 2000, 200);
  check("buzzer tone() call completes (GND not wired -> silent by design)", true);

  Serial.println();
  Serial.printf("RESULT: %d/%d automated checks passed\n", testsPassed, testsRun);
  Serial.println("From here: NeoPixel blinks BLUE every 2s = board alive and IMU still healthy.");
  Serial.println("If it ever switches to RED blinking, the IMU read failed at runtime.");
}

void loop() {
  // Runtime health heartbeat -- this is the ongoing "is it working" signal
  // for when nobody is watching the serial monitor.
  imu::Vector<3> g = bno.getVector(Adafruit_BNO055::VECTOR_GRAVITY);
  float mag = sqrtf(g.x() * g.x() + g.y() * g.y() + g.z() * g.z());
  bool ok = (mag > 9.0f && mag < 10.5f) && !isnan(mag);

  if (ok) {
    blinkColor(COLOR_HEARTBEAT_R, COLOR_HEARTBEAT_G, COLOR_HEARTBEAT_B, 1, 120, 0);
  } else {
    blinkColor(COLOR_FAIL_R, COLOR_FAIL_G, COLOR_FAIL_B, 1, 100, 100);
  }
  delay(2000);
}
