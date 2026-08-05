/*
 * WCMCU-2812B-12 direct ESP-IDF RMT test for XIAO ESP32S3.
 *
 * Wiring: VCC -> VBUS/5V, GND -> GND, DI -> D1/GPIO2.
 * This bypasses Adafruit_NeoPixel and Arduino core 3.2.0's RMT setup.
 */

#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_err.h"

#define NEOPIXEL_PIN 2
#define NEOPIXEL_COUNT 12

constexpr uint32_t RMT_RESOLUTION_HZ = 10 * 1000 * 1000;
constexpr size_t SYMBOLS_PER_PIXEL = 24;

rmt_channel_handle_t rmtChannel = nullptr;
rmt_encoder_handle_t copyEncoder = nullptr;
rmt_symbol_word_t frame[NEOPIXEL_COUNT * SYMBOLS_PER_PIXEL];

void stopWithError(const char *step, esp_err_t error) {
  Serial.printf("ERROR: %s: %s (0x%X)\n", step, esp_err_to_name(error), static_cast<unsigned>(error));
  while (true) {
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
  }
}

void fillFrame(uint8_t red, uint8_t green, uint8_t blue) {
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
}

void showColor(uint8_t red, uint8_t green, uint8_t blue) {
  fillFrame(red, green, blue);

  rmt_transmit_config_t transmitConfig = {};
  esp_err_t error = rmt_transmit(rmtChannel, copyEncoder, frame, sizeof(frame), &transmitConfig);
  if (error != ESP_OK) {
    stopWithError("rmt_transmit", error);
  }

  error = rmt_tx_wait_all_done(rmtChannel, -1);
  if (error != ESP_OK) {
    stopWithError("rmt_tx_wait_all_done", error);
  }
}

void setup() {
  Serial.begin(115200);
  delay(3000);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);  // Built-in LED off (it is active-low).

  rmt_tx_channel_config_t channelConfig = {};
  channelConfig.gpio_num = static_cast<gpio_num_t>(NEOPIXEL_PIN);
  channelConfig.clk_src = RMT_CLK_SRC_DEFAULT;
  channelConfig.resolution_hz = RMT_RESOLUTION_HZ;
  channelConfig.mem_block_symbols = 64;
  channelConfig.trans_queue_depth = 1;
  channelConfig.flags.allow_pd = 0;  // ESP32-S3 cannot retain RMT through light sleep.

  esp_err_t error = rmt_new_tx_channel(&channelConfig, &rmtChannel);
  if (error != ESP_OK) {
    stopWithError("rmt_new_tx_channel", error);
  }

  rmt_copy_encoder_config_t encoderConfig = {};
  error = rmt_new_copy_encoder(&encoderConfig, &copyEncoder);
  if (error != ESP_OK) {
    stopWithError("rmt_new_copy_encoder", error);
  }

  error = rmt_enable(rmtChannel);
  if (error != ESP_OK) {
    stopWithError("rmt_enable", error);
  }

  Serial.println("Direct RMT WS2812B test started on D1/GPIO2");
}

void loop() {
  digitalWrite(LED_BUILTIN, LOW);
  showColor(64, 0, 64);  // magenta
  Serial.println("All 12: magenta");
  delay(1000);

  digitalWrite(LED_BUILTIN, HIGH);
  showColor(64, 0, 0);  // red
  Serial.println("All 12: red");
  delay(1000);

  digitalWrite(LED_BUILTIN, LOW);
  showColor(0, 64, 0);  // green
  Serial.println("All 12: green");
  delay(1000);

  digitalWrite(LED_BUILTIN, HIGH);
  showColor(0, 0, 64);  // blue
  Serial.println("All 12: blue");
  delay(1000);
}
