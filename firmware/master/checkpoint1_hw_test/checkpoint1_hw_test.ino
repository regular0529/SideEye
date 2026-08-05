/*
 * SideEye — checkpoint 1: master board hardware bring-up
 * (PDR_SideEye.md section 8, row "1. 하드웨어 구성")
 *
 * Verifies BNO055 IMU is alive and fusing, then exercises the NeoPixel
 * (left/right turn-signal colors) and the buzzer signal line.
 *
 * Wiring (jumper wires, no PCB — PDR_SideEye.md section 4):
 *   BNO055  VIN -> 3.3V-OUT   GND -> GND   SCL -> D5/GPIO6   SDA -> D4/GPIO5
 *           ADD -> floating (address stays 0x29)
 *   NeoPixel VCC -> 3.3V-OUT  GND -> GND   DIN -> D1/GPIO2
 *   Buzzer   signal -> D0/GPIO1   GND -> intentionally NOT connected yet
 *            (classroom — keeps the buzzer silent while still exercising
 *             the tone() call path; connect GND before the real demo)
 *
 * Libraries:
 *   arduino-cli lib install "Adafruit BNO055"
 *   arduino-cli lib install "Adafruit NeoPixel"
 *
 * Build:  arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 -u -p <PORT> checkpoint1_hw_test
 * Watch:  mon.ps1 -Port <PORT>
 */
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <Adafruit_NeoPixel.h>

#define BNO_ADDR       0x29
#define NEOPIXEL_PIN   D1
#define NEOPIXEL_COUNT 1
#define BUZZER_PIN     D0

// PDR_SideEye.md section 3 color contract
#define COLOR_LEFT  0xFF8000  // orange
#define COLOR_RIGHT 0xFFFF00  // yellow

Adafruit_BNO055 bno(55, BNO_ADDR, &Wire);
Adafruit_NeoPixel pixel(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

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

// Colors used purely as a pass/fail signal -- no serial monitor required.
#define COLOR_BOOT       0xFFFFFF  // white:  sketch started
#define COLOR_OK         0x00FF00  // green:  a check passed
#define COLOR_FAIL       0xFF0000  // red:    a check failed / IMU unhealthy
#define COLOR_HEARTBEAT  0x0000FF  // blue:   alive, looping normally

bool bnoHealthy = false;

void blinkColor(uint32_t color, int times, int onMs = 150, int offMs = 150) {
  for (int i = 0; i < times; i++) {
    pixel.setPixelColor(0, color);
    pixel.show();
    delay(onMs);
    pixel.clear();
    pixel.show();
    delay(offMs);
  }
}

// Halts here blinking red forever -- this IS the failure report when there
// is no serial monitor to read.
void failForever(const char *reason) {
  Serial.print("FATAL: ");
  Serial.println(reason);
  while (true) {
    blinkColor(COLOR_FAIL, 3, 200, 200);
    delay(600);
  }
}

void setup() {
  Serial.begin(115200);
  delay(3000);

  Serial.println();
  Serial.println("=== SideEye checkpoint 1: master hardware test ===");

  // ---- NeoPixel comes up first so it can report everything after it ----
  pixel.begin();
  pixel.setBrightness(50);
  pixel.clear();
  pixel.show();
  blinkColor(COLOR_BOOT, 1, 400, 0);  // "board is alive" flash

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

  bnoHealthy = true;
  Serial.println("IMU healthy -- NeoPixel will blink GREEN x3");
  blinkColor(COLOR_OK, 3, 250, 250);

  // ---- turn-signal color demo (visual only, PDR color contract) ----
  Serial.println("MANUAL CHECK: watch the NeoPixel -- orange x5 then yellow x5");
  blinkColor(COLOR_LEFT, 5);
  delay(500);
  blinkColor(COLOR_RIGHT, 5);
  pixel.clear();
  pixel.show();

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

  blinkColor(ok ? COLOR_HEARTBEAT : COLOR_FAIL, 1, 120, 0);
  delay(2000);
}
