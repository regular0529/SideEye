# NeoPixel Debugging And Verified Setup

**Date:** 2026-08-05
**Board:** Seeed XIAO ESP32S3 Sense (`COM13`)
**Module:** WCMCU-2812B-12, WS2812B-compatible 12-pixel ring

## Current Result

The 12-pixel ring is verified working. The final direct-RMT test on D1 cycles
all 12 LEDs through magenta, red, green, and blue every second.

Verified source:

```text
firmware/master/neopixel_direct_rmt_test/neopixel_direct_rmt_test.ino
```

The source compiles and uploads with:

```powershell
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 "firmware/master/neopixel_direct_rmt_test"
arduino-cli upload -p COM13 --fqbn esp32:esp32:XIAO_ESP32S3 "firmware/master/neopixel_direct_rmt_test"
```

## Final Wiring

| WCMCU-2812B-12 pin | XIAO ESP32S3 Sense connection |
|---|---|
| `DI` | `D1` / GPIO2 |
| `5V` / `VCC` | `VBUS` / 5V |
| `GND` | `GND` |
| `DO` | Leave unconnected |

- An optional 330-470 ohm resistor belongs **only between D1 and DI**.
- Do **not** put the resistor in the 5V supply line. That caused the ring's
  supply voltage to fall to about 3.4V and prevented normal operation.
- Use `VBUS` rather than 3.3V for the ring supply.
- A 5V WS2812B can have a 3.5V input-high threshold. This particular ring
  accepts the XIAO's 3.3V D1 signal, but use a 74AHCT125/74HCT buffer if a
  future ring does not respond reliably.

## Pin And Peripheral Checks

| Item | Result |
|---|---|
| D1 / GPIO2 DC-output test | Passed: multimeter measured 3.3V then 0V, alternating every 5 seconds |
| BNO055 IMU | Not a conflict: it uses I2C D4/GPIO5 (SDA) and D5/GPIO6 (SCL) |
| XIAO Sense camera | Not a conflict: the camera does not use GPIO2/D1 |
| Ring pixel count | 12, not 1 |

The D1 meter diagnostic is retained here:

```text
firmware/master/d1_gpio_output_test/d1_gpio_output_test.ino
```

To repeat it, temporarily disconnect the ring's `DI` wire, set the meter to
DC voltage mode, place black probe on GND and red probe on D1. Never use the
meter's current/mA mode across D1 and GND.

## Why The Initial Adafruit NeoPixel Test Failed

The first test used `Adafruit_NeoPixel` 1.15.5 with ESP32 Arduino core 3.2.0.
Its serial output included:

```text
E (...) rmt: rmt_new_tx_channel(...): not able to power down in light sleep
```

Arduino core 3.2.0's RMT setup creates `rmt_tx_channel_config_t` without
zero-initializing every newer flag. On the ESP32-S3, a nonzero `allow_pd` flag
is rejected because the RMT peripheral cannot retain state through light
sleep. The resulting RMT initialization failure prevents the WS2812 frame
from being transmitted correctly.

The working implementation bypasses that path and uses ESP-IDF RMT directly:

```cpp
rmt_tx_channel_config_t channelConfig = {};
channelConfig.gpio_num = static_cast<gpio_num_t>(2);  // D1 / GPIO2
channelConfig.clk_src = RMT_CLK_SRC_DEFAULT;
channelConfig.resolution_hz = 10 * 1000 * 1000;
channelConfig.mem_block_symbols = 64;
channelConfig.trans_queue_depth = 1;
channelConfig.flags.allow_pd = 0;
```

It sends 24 GRB bits per pixel at 10MHz RMT resolution:

| WS2812B bit | High | Low |
|---|---:|---:|
| `0` | 0.4 us | 0.9 us |
| `1` | 0.8 us | 0.5 us |

The direct implementation uses only ESP32 core headers:

```cpp
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_err.h"
```

No third-party NeoPixel library is required for the verified path.

## Library Decision

| Option | Decision |
|---|---|
| `Adafruit_NeoPixel` | Do not use with the currently installed ESP32 core 3.2.0 for this ring; it emitted the RMT initialization error above |
| `light_ws2812` | Do not use; it is designed around AVR timing/assembly and is not the right driver for ESP32-S3 |
| Direct ESP-IDF RMT | Use this; it was compiled, uploaded, and visually verified on the actual ring |

## SideEye Integration Target

Integrate the verified RMT functions into:

```text
firmware/master/checkpoint1_hw_test/checkpoint1_hw_test.ino
```

That sketch currently contains the temporary built-in-LED fallback. Replace
the fallback with the direct RMT sender and retain the BNO055 and buzzer code.

Use the SideEye color contract at USB-safe brightness (`64` out of `255`):

| State | Color |
|---|---|
| Boot | White |
| IMU healthy | Green |
| Left lean | Orange (`0xFF8000`) |
| Right lean | Yellow (`0xFFFF00`) |
| Normal heartbeat | Blue |
| Failure | Red |
| Idle in final turn-signal integration | Off |

The checkpoint sketch is a hardware test. The future master integration must
keep orange/yellow continuously on while the IMU state is left/right, then
turn the ring off when the IMU state returns to idle.

## Hand-Off Checklist

1. Preserve D1/GPIO2 for the ring; do not move it to D2/D3.
2. Keep the BNO055 on D4/D5 and the camera connected; neither conflicts.
3. Copy the direct RMT implementation from `neopixel_direct_rmt_test` into the
   SideEye master sketch instead of reintroducing `Adafruit_NeoPixel`.
4. Compile the checkpoint sketch with the local libraries directory:

```powershell
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 --libraries "C:\Dev\WearableProjectEX\.arduino\libraries" "C:\Dev\WearableProjectEX\firmware\master\checkpoint1_hw_test"
```

5. Upload to `COM13`, then capture a bounded serial log. Physical LED color
   changes remain a manual visual check.

## Evidence

- D1 DC high/low was measured with a multimeter.
- The final direct-RMT test uploaded successfully to XIAO ESP32S3 Sense on
  `COM13`.
- Serial log reported repeated `All 12: magenta`, `red`, `green`, and `blue`.
- The user visually confirmed the ring works.
