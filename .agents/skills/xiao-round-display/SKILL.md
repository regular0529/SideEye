---
name: xiao-round-display
description: Use this skill whenever the user works on this project's Seeed Studio Round Display for XIAO, GC9A01, BOARD_SCREEN_COMBO 501, CHSC6X touch, RoundDisplayDemo, RoundDisplayTimeSeries, display rotation, or a sensor-driven round display UI. Follow the verified XIAO ESP32-S3 wiring, I2C, rendering, orientation, compile/upload, and diagnostic workflow.
compatibility: Windows PowerShell, Arduino CLI, ESP32 Arduino core 3.2.0, XIAO ESP32-S3, Seeed Studio Round Display
---

# XIAO Round Display

Use this as the project-specific operating guide for the Seeed Studio Round
Display connected to a XIAO ESP32-S3. The board and sensor stack have been
verified on `COM12`; detect the port again before every upload because it can
change after a reset or reconnect.

## Source Of Truth

Read the relevant files before changing behavior:

- `ROUND_DISPLAY_CODING_GUIDE.md`: verified hardware setup, diagnostics, and
  the final orientation calibration.
- `RoundDisplayTimeSeries/RoundDisplayTimeSeries.ino`: current full-screen
  48 x 48 sensor-driven fluid firmware.
- `RoundDisplayTimeSeries/driver.h`: required GC9A01 board selection.
- `RoundDisplayTimeSeries/README.md`: current user-facing behavior and CLI.
- `RoundDisplayDemo/RoundDisplayDemo.ino`: minimal LCD and touch proof.

Do not restore the old inner-circle vessel, bottom navigation, or text into
the active AURA art screen unless the user explicitly requests a new UI.

## Hardware Contract

| Function | XIAO pin | GPIO |
| --- | --- | --- |
| LCD SPI SCK | D8 | GPIO7 |
| LCD SPI MISO | D9 | GPIO8 |
| LCD SPI MOSI | D10 | GPIO9 |
| LCD CS | D1 | GPIO2 |
| LCD DC | D3 | GPIO4 |
| Backlight | D6 | GPIO43 |
| Touch interrupt | D7 | GPIO44 |
| I2C SDA | D4 | GPIO5 |
| I2C SCL | D5 | GPIO6 |
| Sense SD CS | D2 | GPIO3 |

Known I2C devices on the verified assembly:

| Address | Device |
| --- | --- |
| `0x23` | BH1750 light sensor |
| `0x29` | BNO055 IMU |
| `0x2E` | CHSC6X touch controller |
| `0x51` | Round Display RTC |
| `0x76` | BME280 environment sensor |

## Required Display Setup

Keep `driver.h` beside the sketch and do not replace it with a generic TFT
setup:

```cpp
#define BOARD_SCREEN_COMBO 501 // Seeed Studio Round Display for XIAO (GC9A01)
```

Before `tft.init()`, enable the backlight and disable the shared Sense SD
card chip select:

```cpp
pinMode(D6, OUTPUT);
digitalWrite(D6, HIGH);
pinMode(D2, OUTPUT);
digitalWrite(D2, HIGH);

tft.init();
tft.setRotation(1);
```

`setRotation(1)` is the verified display orientation for this assembly. Do
not assume that a screen rotation also dictates the IMU transform. Confirm
visual orientation first, then test physical tilt independently.

The physical panel is already round. For full-screen art, render the complete
240 x 240 frame with a black background and let the panel provide the circular
mask. Do not draw a second circle, rim, or decorative vessel unless asked.

## I2C And Sensors

Always select the physical XIAO I2C pins explicitly:

```cpp
Wire.begin(D4, D5);
Wire.setClock(100000);
```

For a missing or suspect device, scan the bus before changing driver code.
Use the verified scanner from the `xiao-i2c-sensors` skill when a standalone
hardware check is needed. Expected BNO055 evidence is:

```text
0x29 chip id = 0xA0 -> BNO055
bno055: status=5 selftest=0x0F error=0
```

Create the BNO055 with the verified address and bus:

```cpp
Adafruit_BNO055 bno(55, 0x29, &Wire);
```

Leave the BNO055 on its internal oscillator. Do not call
`setExtCrystalUse(true)` unless the installed module has a confirmed 32 kHz
crystal. If initialization can race the sensor at boot, retry the probe and
`begin()` rather than permanently disabling pose input.

For the verified `setRotation(1)` assembly, use raw accelerometer X/Y directly
as the display gravity vector after normalization. `+Y` is the resting down
direction. Use `VECTOR_ACCELEROMETER` for the pooling direction and
`VECTOR_LINEARACCEL` for shake and wave energy.

## Touch Reliability

CHSC6X uses I2C address `0x2E` and D7 active-low interrupt. It can leave INT
low before a valid five-byte frame is ready, causing an occasional I2C NACK.
Rate-limit touch reads and back off for about one second after a failed read.
Do not flood the I2C bus with failed touch requests because that can make IMU
debugging misleading.

## Flicker-Free Pixel Art

Use a 240 x 240 16-bit `TFT_eSprite` for animated scenes:

```cpp
TFT_eSprite artCanvas = TFT_eSprite(&tft);
artCanvas.setColorDepth(16);
artCanvas.createSprite(240, 240);
// Draw a complete frame into artCanvas.
artCanvas.pushSprite(0, 0);
```

The active AURA implementation uses a 48 x 48 density grid. Each logical cell
occupies 5 x 5 display pixels and lights a 4 x 4 square, preserving a one-pixel
black grid gap. Keep the renderer full-screen, single-hue per frame, and free
of labels or navigation chrome unless the user asks otherwise.

Sensor mapping in the established art behavior:

- BH1750: fluid brightness and D6 backlight PWM.
- BME280 temperature: muted cobalt blue when cold through vermilion red when hot.
- BME280 humidity: lower contrast and softer surface at higher humidity.
- BNO055 raw gravity: pooled direction.
- BNO055 linear acceleration: low-viscosity slosh and wave energy.

## Compile, Upload, And Verify

Use the XIAO ESP32-S3 FQBN. Compile before upload and use bounded serial reads;
`arduino-cli monitor` blocks indefinitely in automated work.

```powershell
arduino-cli board list
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 RoundDisplayTimeSeries
arduino-cli upload -p COM12 --fqbn esp32:esp32:XIAO_ESP32S3 RoundDisplayTimeSeries
powershell -File .agents\skills\xiao-esp32s3\scripts\read_serial.ps1 -Port COM12 -Seconds 10
```

Opening the serial port resets the XIAO, so `rst:0x15 (USB_UART_CHIP_RESET)` is
normal. A successful sensor boot should include framebuffer, BNO055, BME280,
BH1750, and sample logs. Do not claim that pose behavior is verified from a
compile alone; test by lifting each physical edge and observing the fluid pool
toward the low side.

## Troubleshooting Order

1. Black LCD: check the display power switch, board orientation, `driver.h`,
   D6 backlight, and D2 SD CS before changing graphics code.
2. No sensor data: run the I2C scanner and verify D4/D5, `0x29`, and BNO055
   chip ID `0xA0` before changing axis math.
3. BNO055 shows `status` other than `5`: keep the internal oscillator, retry
   initialization, and inspect the bounded serial boot log.
4. Screen looks rotated: adjust `tft.setRotation()` only, upload, and inspect
   the visual orientation before touching IMU code.
5. Fluid pools in the wrong direction: retain the confirmed screen rotation,
   then adjust one IMU axis sign or axis order at a time and test a physical
   edge lift after each upload.
6. Animation flickers: render into the sprite and push only completed frames.
7. Repeated touch NACKs: use the retry backoff; do not continuously poll a
   stuck-low interrupt line.

## Completion Report

When changing this system, report:

1. Files changed and the visual/sensor behavior changed.
2. Detected board port and exact FQBN.
3. Compile and upload result.
4. Relevant serial evidence, including BNO055 status when IMU behavior changed.
5. Any physical test the user should perform, especially display orientation
   and low-side fluid pooling.
