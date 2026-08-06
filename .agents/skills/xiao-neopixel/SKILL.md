---
name: xiao-neopixel
description: Use this skill whenever a Seeed XIAO ESP32S3 or XIAO ESP32S3 Sense drives a WS2812/WS2812B/NeoPixel ring or strip, especially a dark ring, an Adafruit_NeoPixel RMT error ("not able to power down in light sleep"), or general NeoPixel color testing on arduino-esp32 core 3.x.
compatibility: Windows, PowerShell 5.1, arduino-cli, ESP32 Arduino core 3.x
---

# XIAO ESP32S3 NeoPixel

Drive the ring with ESP-IDF's RMT peripheral directly rather than
`Adafruit_NeoPixel`. On arduino-esp32 core 3.2.0, `Adafruit_NeoPixel`'s RMT
path can leave the `allow_pd` flag uninitialized; ESP32-S3 rejects a nonzero
`allow_pd` because RMT can't retain state through light sleep, and the ring
goes completely dark (not flickering, not wrong colors — nothing) even
though wiring is correct. The symptom in serial output is `not able to power
down in light sleep`. This is a toolchain quirk, not a hardware fault —
verified on a WCMCU-2812B-12 (12-pixel) ring on D1/GPIO2.

## Verified wiring

| Ring pin | XIAO connection |
|---|---|
| `DI` (data in, not `DO`) | Any free GPIO (D1/GPIO2 verified) |
| `5V` / `VCC` | VBUS / 5V — **not** 3.3V-OUT, WS2812B needs it |
| `GND` | GND |
| `DO` | Unconnected (only used to chain to a second ring) |

- An optional 330-470 ohm resistor goes in the data line only
  (`GPIO -> resistor -> DI`), never in the 5V line — putting it on 5V drops
  the ring's supply enough to cause erratic/no operation.
- 3.3V data into a 5V WS2812B is marginal by spec but works in practice on
  the verified ring; add a 74AHCT125/74HCT level shifter if another ring
  stays dark despite passing every check below.
- A ring drawing full brightness on all pixels can exceed USB current
  limits — cap brightness (e.g. scale to ~25% of max) rather than debugging
  phantom resets/brownouts that are actually a power budget problem.

## Working driver (drop-in, no third-party library)

```cpp
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"

#define NEOPIXEL_PIN   2   // change to your GPIO
#define NEOPIXEL_COUNT 12  // change to your pixel count

constexpr uint32_t RMT_RESOLUTION_HZ = 10 * 1000 * 1000;
constexpr size_t SYMBOLS_PER_PIXEL = 24;
constexpr uint8_t BRIGHTNESS_SCALE = 64;  // out of 255 -- keep low, see USB current note above
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
  channelConfig.flags.allow_pd = 0;  // <-- the actual fix; ESP32-S3 can't retain RMT through light sleep
  rmt_new_tx_channel(&channelConfig, &rmtChannel);
  rmt_copy_encoder_config_t encoderConfig = {};
  rmt_new_copy_encoder(&encoderConfig, &copyEncoder);
  rmt_enable(rmtChannel);
}

// perPixel[i] = {r,g,b} for pixel i -- lets callers light only some pixels
void neoPixelShowPixels(const uint8_t perPixel[NEOPIXEL_COUNT][3]) {
  size_t symbolIndex = 0;
  for (int pixel = 0; pixel < NEOPIXEL_COUNT; pixel++) {
    uint8_t r = (uint16_t)perPixel[pixel][0] * BRIGHTNESS_SCALE / 255;
    uint8_t g = (uint16_t)perPixel[pixel][1] * BRIGHTNESS_SCALE / 255;
    uint8_t b = (uint16_t)perPixel[pixel][2] * BRIGHTNESS_SCALE / 255;
    const uint8_t grb[] = {g, r, b};  // WS2812 wants GRB order, not RGB
    for (int component = 0; component < 3; component++) {
      for (int bit = 7; bit >= 0; bit--) {
        const bool one = grb[component] & (1 << bit);
        rmt_symbol_word_t &symbol = frame[symbolIndex++];
        symbol.level0 = 1;
        symbol.duration0 = one ? 8 : 4;   // 0.8us : 0.4us high, at 10MHz resolution
        symbol.level1 = 0;
        symbol.duration1 = one ? 5 : 9;   // 0.5us : 0.9us low
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
```

Call `neoPixelBegin()` once in `setup()`, then `neoPixelShow(r, g, b)` (whole
ring one color) or `neoPixelShowPixels(...)` (per-pixel, e.g. only half the
ring) whenever you need to change what's lit. A minimal test sketch: call
`neoPixelBegin()`, then cycle `neoPixelShow()` through a few colors with
`delay()` between them — all 12 (or however many) pixels should change
together with no RMT error in serial output.

## Diagnosing a dark ring

1. Confirm `DI`, not `DO`, is wired to your data GPIO.
2. Confirm ring `VCC` is on `VBUS`/5V and `GND` is shared with the board —
   not on 3.3V.
3. Don't diagnose WS2812 timing with a multimeter (it can't see
   microsecond-scale pulses). Instead, flash a trivial sketch that just
   toggles the data GPIO with `digitalWrite(pin, HIGH); delay(n);
   digitalWrite(pin, LOW); delay(n);` and confirm with a meter in DC-voltage
   mode (never current/mA mode) that the pin alternates ~3.3V/0V — this
   proves the GPIO itself is healthy before you suspect the RMT driver.
4. If that passes but the direct-RMT driver above still produces a dark
   ring, try a 74AHCT125/74HCT level shifter and double-check the ring
   isn't wired backwards (DI vs DO swapped).
5. Don't move to a different GPIO until the toggle test above fails — a
   confirmed clean high/low cycle means the pin is fine and the problem is
   elsewhere (driver, ring itself, or power).

## Avoid these paths

- Don't use `light_ws2812` — its timing implementation targets AVR, not
  ESP32-S3.
- Don't keep debugging `Adafruit_NeoPixel` once you see `not able to power
  down in light sleep` in serial output on core 3.2.0+ — switch straight to
  the direct RMT driver above instead of chasing library workarounds.
