/*
 * SideEye — buzzer volume test.
 *
 * A passive piezo buzzer has no built-in volume control; tone() just drives
 * a full-swing square wave. This uses the ESP32 LEDC PWM peripheral instead
 * so the duty cycle acts as a volume knob (lower duty = quieter, since the
 * piezo gets less average driving power).
 *
 * Wiring: signal -> D0/GPIO1, GND -> buzzer GND (PDR_SideEye.md section 4).
 */
#define BUZZER_PIN D0
#define TONE_FREQ_HZ 2000
constexpr int VOLUME = 50;        // 0..255, duty cycle out of 255 (8-bit)
constexpr int BEEP_MS = 300;
constexpr int GAP_MS = 400;

void setup() {
  Serial.begin(115200);
  delay(1000);
  ledcAttach(BUZZER_PIN, TONE_FREQ_HZ, 8);  // 8-bit duty resolution (0-255)
  Serial.printf("Buzzer volume test: freq=%dHz volume=%d/255\n", TONE_FREQ_HZ, VOLUME);
}

void loop() {
  ledcWrite(BUZZER_PIN, VOLUME);
  Serial.println("beep");
  delay(BEEP_MS);
  ledcWrite(BUZZER_PIN, 0);
  delay(GAP_MS);
}
