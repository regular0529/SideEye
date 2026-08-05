/*
 * Isolated NeoPixel test -- no IMU, no other peripherals.
 * Module is a WCMCU-2812B-12 12-pixel ring -- lights ALL 12 so
 * nothing is missed if only pixel 0 were lit near the DI input.
 * Solid magenta at a USB-safe brightness immediately after boot.
 * If this stays dark, the problem is NeoPixel wiring/power/part, not code.
 *
 * Wiring: NeoPixel VCC -> 5V(VBUS), GND -> GND, DIN -> D1/GPIO2.
 * Put an optional 330-470 ohm resistor in the DIN line, never in the 5V line.
 */
#include <Adafruit_NeoPixel.h>

#define NEOPIXEL_PIN   2   // GPIO2 / XIAO D1
#define NEOPIXEL_COUNT 12
#define NEOPIXEL_BRIGHTNESS 64

Adafruit_NeoPixel pixel(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  Serial.begin(115200);
  delay(3000);  // Allow USB CDC to enumerate before startup output.
  pinMode(LED_BUILTIN, OUTPUT);
  pixel.begin();
  pixel.setBrightness(NEOPIXEL_BRIGHTNESS);
  for (int i = 0; i < NEOPIXEL_COUNT; i++) {
    pixel.setPixelColor(i, pixel.Color(255, 0, 255));  // magenta, hard to miss
  }
  pixel.show();
}

void loop() {
  // Sanity check: if this LED blinks, the sketch is definitely running.
  digitalWrite(LED_BUILTIN, LOW);   // inverted: LOW = on
  Serial.println("built-in LED ON, neopixel should be solid magenta");
  delay(500);
  digitalWrite(LED_BUILTIN, HIGH);  // off
  delay(500);
}
