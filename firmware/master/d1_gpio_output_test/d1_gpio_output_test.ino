/*
 * XIAO ESP32S3 D1 (GPIO2) output test for a multimeter.
 *
 * Disconnect NeoPixel DIN from D1 before measuring. Keep the meter in DC
 * voltage mode: black probe to XIAO GND, red probe to D1.
 *
 * Expected result: approximately 3.3 V for five seconds, then 0 V for five
 * seconds, repeating. Do not use the current/mA meter setting.
 */

#define TEST_PIN 2  // XIAO D1 / GPIO2

void setup() {
  Serial.begin(115200);
  delay(3000);  // Allow USB CDC to enumerate before printing.

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(TEST_PIN, OUTPUT);
  digitalWrite(TEST_PIN, LOW);
  digitalWrite(LED_BUILTIN, HIGH);  // Built-in LED off (it is active-low).

  Serial.println("D1/GPIO2 DC output test started");
}

void loop() {
  digitalWrite(TEST_PIN, HIGH);
  digitalWrite(LED_BUILTIN, LOW);
  Serial.println("D1 HIGH: measure about 3.3 V");
  delay(5000);

  digitalWrite(TEST_PIN, LOW);
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.println("D1 LOW: measure about 0 V");
  delay(5000);
}
