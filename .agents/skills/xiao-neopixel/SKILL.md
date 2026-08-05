---
name: xiao-neopixel
description: Use this skill whenever a Seeed XIAO ESP32S3 or XIAO ESP32S3 Sense drives a WS2812/WS2812B/NeoPixel ring or strip, especially WCMCU-2812B-12, D1/GPIO2 wiring, a dark ring, an Adafruit_NeoPixel RMT error, or NeoPixel color testing. It provides the hardware-verified direct-RMT workflow for this project.
compatibility: Windows, PowerShell 5.1, arduino-cli, ESP32 Arduino core 3.x
---

# XIAO ESP32S3 NeoPixel

Use the project-tested direct ESP-IDF RMT implementation rather than starting
with `Adafruit_NeoPixel`. It was visually verified on a WCMCU-2812B-12 ring
with a XIAO ESP32S3 Sense on D1/GPIO2.

Read `NEOPIXEL_DEBUGGING.md` before changing a working setup. It records the
actual hardware result and the reason the Adafruit RMT path failed on core
3.2.0.

## Verified Wiring

| Ring pin | XIAO connection |
|---|---|
| `DI` | D1 / GPIO2 |
| `5V` / `VCC` | VBUS / 5V |
| `GND` | GND |
| `DO` | Unconnected |

- Put an optional 330-470 ohm resistor in the data line (`D1 -> resistor -> DI`), never in the 5V line.
- The ring has 12 pixels: set the count to `12`.
- BNO055 uses D4/D5 and the XIAO Sense camera does not use D1, so neither
  conflicts with the ring.
- 3.3V data into a 5V WS2812B is marginal by specification. The verified ring
  works directly; use a 74AHCT125/74HCT buffer if another ring remains dark.

## Start With The Verified Test

The source of truth is:

```text
firmware/master/neopixel_direct_rmt_test/neopixel_direct_rmt_test.ino
```

It does not use a third-party NeoPixel library. It initializes
`rmt_tx_channel_config_t` to zero and explicitly sets `allow_pd = 0`, then
sends all 12 GRB pixels with the ESP-IDF RMT peripheral.

```powershell
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 "firmware/master/neopixel_direct_rmt_test"
arduino-cli upload -p <PORT> --fqbn esp32:esp32:XIAO_ESP32S3 "firmware/master/neopixel_direct_rmt_test"
```

Expected visual result: all 12 LEDs cycle magenta, red, green, and blue once
per second. Expected serial result names the same four colors without an RMT
error.

## Diagnose A Dark Ring

1. Check `DI`, not `DO`, is connected to D1.
2. Check ring VCC is on VBUS/5V and GND is shared with the XIAO.
3. Do not diagnose WS2812 timing with a multimeter. Flash
   `firmware/master/d1_gpio_output_test/d1_gpio_output_test.ino` instead.
4. Disconnect only ring DI, measure D1 to GND in DC-voltage mode. It must
   alternate about 3.3V and 0V every five seconds.
5. If D1 passes but the direct RMT test is still dark, use a 74AHCT125/74HCT
   level shifter and verify the ring is not wired backwards.

Never measure D1-to-GND in the meter's current/mA mode.

## Avoid These Paths

- Do not use `light_ws2812`: its AVR-specific timing implementation is not
  appropriate for ESP32-S3.
- On the installed ESP32 core 3.2.0, do not rely on the observed
  `Adafruit_NeoPixel` RMT path after it logs `not able to power down in light
  sleep`. Use direct RMT instead.
- Do not move to D2/D3 until the D1 DC-output test fails. A confirmed D1
  high/low cycle proves the pin itself is healthy.

## Integrating Into SideEye

Copy the RMT setup, GRB frame builder, and `showColor()` behavior from the
verified test into `firmware/master/checkpoint1_hw_test`. Keep the BNO055 and
buzzer paths unchanged. Apply SideEye's color contract:

- Left lean: orange (`0xFF8000`)
- Right lean: yellow (`0xFFFF00`)
- Idle: off
- Hardware-test boot/health/error states: white/green-or-blue/red

Compile the integrated checkpoint sketch with:

```powershell
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 --libraries "C:\Dev\WearableProjectEX\.arduino\libraries" "C:\Dev\WearableProjectEX\firmware\master\checkpoint1_hw_test"
```

Use a bounded serial reader for agent verification. A physical color change is
the final proof, so ask the user to confirm it after upload.
