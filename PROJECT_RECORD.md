# XIAO ESP32S3 Edge Impulse Project Record

> ⚠️ **이 문서는 SideEye의 최신 지침이 아닙니다.** 여기 나오는 `background`/`mouse`/`tumbler` 클래스와 Edge Impulse 프로젝트(`1078233`, 키 `EI_API_KEY`)는 SideEye 이전의 옛날 카메라 데모 기록입니다. 이 문서는 PSRAM/카메라 배포 관련 하드웨어 트러블슈팅 참고용으로만 남겨둔 것이고, **실제 작업 지침은 반드시 `PDR_SideEye.md`를 보세요**:
> - SideEye 카메라 클래스: `vehicle` / `background` (2클래스, PDR 2절)
> - SideEye Edge Impulse 프로젝트: `1079933` (https://studio.edgeimpulse.com/studio/1079933), 키 `SideEYE_EI_API_KEY`
>
> 아래 내용을 SideEye 비전 학습에 그대로 따라 하지 마세요.

## Goal

Classify the camera view from a Seeed XIAO ESP32S3 Sense into three labels on-device:

- `background`
- `mouse`
- `tumbler`

The model runs locally on the board without an Internet connection. A browser dashboard shows the latest camera frame and class probabilities on the local network.

## Dataset

| Label | Images |
| --- | ---: |
| `background` | 150 |
| `mouse` | 150 |
| `tumbler` | 150 |
| Total | 450 |

Dataset folders:

- `dataset/background/`
- `dataset/mouse/`
- `dataset/tumbler/`

## Model Choice

The initial Edge Impulse project was set to object detection. Object detection requires a bounding box for each object image, so it could not train correctly from class-only image folders.

The project was changed to single-label image classification. This model predicts which class best describes the full camera image; it does not draw boxes around objects.

Edge Impulse configuration:

- Input: `96x96` image, squash resize
- DSP block: Image
- Learning block: Transfer learning
- Deployment: Arduino library, EON Compiler, int8 quantized model
- Best validation accuracy recorded during training: `94.59%`

## Edge Impulse Automation

The project used the Edge Impulse REST API instead of `edge-impulse-cli`.

- The project API key is stored privately in `.env` as `EI_API_KEY`.
- Do not commit or publish `.env`.
- Dataset upload helper: `vision_harness/upload_edge_impulse.py`
- Edge Impulse project ID: `1078233`

The final downloaded Arduino library is:

- `ei-regular0529-esp32-example-arduino-1.0.5-impulse-#1.zip`

Installed library location:

- `.arduino/libraries/regular0529-ESP32_example_inferencing/`

## ESP32-S3 Deployment

Board settings:

- Board: Seeed XIAO ESP32S3 Sense
- Port: `COM13`
- FQBN: `esp32:esp32:XIAO_ESP32S3`
- PSRAM build option: `PSRAM=opi`

Firmware source:

- `CameraInference/CameraInference.ino`

Compile and upload commands:

```powershell
arduino-cli compile -j 4 --fqbn esp32:esp32:XIAO_ESP32S3 --board-options PSRAM=opi --libraries .arduino\libraries CameraInference
arduino-cli upload -p COM13 --fqbn esp32:esp32:XIAO_ESP32S3 --board-options PSRAM=opi CameraInference
```

The firmware compiled successfully and was uploaded to `COM13`.

## PSRAM Fixes

The initial firmware crashed during inference because the TensorFlow Lite tensor arena attempted to use constrained internal SRAM.

Applied fixes:

- Compile and upload with `PSRAM=opi`.
- Override Edge Impulse `ei_malloc`, `ei_calloc`, and `ei_free` in `CameraInference.ino` to allocate in `MALLOC_CAP_SPIRAM` first.
- Allocate image buffers and the browser JPEG cache in PSRAM.
- Changed `EI_MAX_OVERFLOW_BUFFER_COUNT` from `30` to `2048` in:

```text
.arduino/libraries/regular0529-ESP32_example_inferencing/src/edge-impulse-sdk/porting/ei_classifier_porting.h
```

This library edit must be reapplied if the Edge Impulse Arduino library is replaced by a newly downloaded ZIP.

## Camera and Web Dashboard

The inference firmware uses:

- Native camera capture: `240x240` JPEG
- Model input: downsampled to `96x96` RGB
- JPEG cache in PSRAM for fast `/jpg` responses
- Preview capture interval: `100 ms`
- Inference interval: `1000 ms`

Last known dashboard address:

```text
http://192.168.0.180
```

The local IP can change after reconnecting to Wi-Fi. Read the serial boot output if the address stops working.

Dashboard endpoints:

- `/` - browser dashboard
- `/jpg` - latest camera frame
- `/results` - latest classification JSON

Observed successful on-device inference before the final UI optimization:

```text
DSP: 5 ms, classification: about 172-175 ms
background: 0.992
mouse: 0.004
tumbler: 0.004
```

## Important Limitations

- This is image classification, not object detection.
- The model selects one of the three classes for the complete frame.
- To add bounding boxes later, collect or label bounding-box annotations for `mouse` and `tumbler`, then create an Edge Impulse object-detection impulse.
- The firmware contains local Wi-Fi credentials for the classroom network. Remove them before sharing the sketch outside the local environment.
- Opening `COM13` with a serial monitor can reset the XIAO ESP32S3 through USB DTR/RTS. Prefer the dashboard for normal demonstrations.

## Related Files

- `CameraCollector/CameraCollector.ino` - original dataset camera collector
- `CameraInference/CameraInference.ino` - deployed camera classification dashboard
- `vision_harness/upload_edge_impulse.py` - Edge Impulse dataset uploader
- `ei-regular0529-esp32-example-arduino-1.0.5-impulse-#1.zip` - final downloaded Arduino library
- `edgeimpulse_project_1078233_arduino.zip` - earlier API-generated Arduino library
