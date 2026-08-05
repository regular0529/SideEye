/*
 * SideEye — IMU data collector for Edge Impulse (checkpoint 3 prep).
 *
 * Streams BNO055 linear acceleration + gyroscope as CSV over serial at a
 * fixed ~50Hz. No labeling/windowing logic here -- the PC-side script
 * (imu_harness/collect_and_upload_imu.py) owns timing windows and labels,
 * so this firmware never needs to change between idle/left/right sessions.
 *
 * Mounting reference (must match the final helmet mount for the model to
 * transfer -- PDR_SideEye.md section 3.1): BNO055 pin header side against
 * the back of the head ("뒤통수"), same orientation used at training time
 * must be used at deployment time.
 *
 * Wiring: BNO055 VIN->3.3V-OUT, GND->GND, SCL->D5/GPIO6, SDA->D4/GPIO5, ADD floating.
 *
 * CSV line format: millis,ax,ay,az,gx,gy,gz
 *   ax/ay/az: linear acceleration (m/s^2, gravity removed)
 *   gx/gy/gz: gyroscope (deg/s)
 *
 * Build:  arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 -u -p <PORT> imu_collector
 */
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>

#define BNO_ADDR 0x29
constexpr uint32_t SAMPLE_INTERVAL_MS = 20;  // ~50Hz

Adafruit_BNO055 bno(55, BNO_ADDR, &Wire);
uint32_t lastSampleAt = 0;

void setup() {
  Serial.begin(115200);
  delay(3000);

  Wire.begin(D4, D5);
  Wire.setClock(100000);

  if (!bno.begin()) {
    Serial.println("ERROR: BNO055 begin() failed -- check wiring");
    while (true) delay(1000);
  }
  delay(1000);  // power-on self test
  // do NOT call setExtCrystalUse(true) -- see xiao-i2c-sensors skill pitfall 2

  Serial.println("READY");  // PC script waits for this line before sampling
}

void loop() {
  uint32_t now = millis();
  if (now - lastSampleAt < SAMPLE_INTERVAL_MS) {
    return;
  }
  lastSampleAt = now;

  imu::Vector<3> accel = bno.getVector(Adafruit_BNO055::VECTOR_LINEARACCEL);
  imu::Vector<3> gyro = bno.getVector(Adafruit_BNO055::VECTOR_GYROSCOPE);

  Serial.printf("%lu,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
                now,
                accel.x(), accel.y(), accel.z(),
                gyro.x(), gyro.y(), gyro.z());
}
